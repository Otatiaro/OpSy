/**
 ******************************************************************************
 * @file    test_routine.cpp
 * @brief   Host tests for @c opsy::utility::routine — resumable interrupt routines.
 *
 *          Tested here rather than on target because none of it is
 *          Cortex-M specific: a routine is a compiler transformation and a
 *          frame in storage the caller owns. What needs a target is the
 *          integration — being resumed from a real interrupt handler — and
 *          that is covered by the QEMU suite.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <utility/routine.hpp>

#include <cstdint>
#include <utility>

namespace
{

// Stands in for a peripheral register the routine reads when it resumes.
volatile uint32_t g_status = 0;

using storage = opsy::utility::routine_storage<256>;

storage g_storage;
storage g_other_storage;

/** @brief Records how far a routine got, so a test can see where it stopped. */
int g_progress = 0;

opsy::utility::routine three_steps(storage&)
{
	g_progress = 1;
	co_await std::suspend_always{};
	g_progress = 2;
	co_await std::suspend_always{};
	g_progress = 3;
}

} // namespace

OPSY_TEST(a_routine_does_nothing_until_it_is_resumed)
{
	g_progress = 0;
	auto routine = three_steps(g_storage);

	CHECK(static_cast<bool>(routine));
	CHECK(g_progress == 0);          // created, not started
	CHECK(!routine.done());
}

OPSY_TEST(each_resume_runs_to_the_next_suspension)
{
	g_progress = 0;
	auto routine = three_steps(g_storage);

	routine.resume();
	CHECK(g_progress == 1);

	routine.resume();
	CHECK(g_progress == 2);

	routine.resume();
	CHECK(g_progress == 3);
	CHECK(routine.done());
}

OPSY_TEST(resuming_a_finished_routine_is_harmless)
{
	g_progress = 0;
	auto routine = three_steps(g_storage);

	for (int i = 0; i < 10; ++i)
		routine.resume();

	// A handler can call resume() on every interrupt without testing first,
	// which is what makes the handler a single line.
	CHECK(g_progress == 3);
	CHECK(routine.done());
}

namespace
{

/** @brief A routine whose locals have to survive every suspension. */
opsy::utility::routine sum_bytes(storage&, const uint8_t* data, std::size_t length, uint32_t* total)
{
	uint32_t running = 0;

	for (std::size_t i = 0; i < length; ++i)
	{
		running += data[i];
		co_await std::suspend_always{};
	}

	*total = running;
}

} // namespace

OPSY_TEST(locals_and_parameters_survive_a_suspension)
{
	static constexpr uint8_t payload[] { 10, 20, 30, 40 };
	uint32_t total = 0;

	auto routine = sum_bytes(g_storage, payload, sizeof(payload), &total);

	while (!routine.done())
		routine.resume();

	// The loop index, the accumulator and all three parameters came back
	// intact across four suspensions.
	CHECK(total == 100);
}

namespace
{

opsy::utility::routine stops_on_error(storage&, int* reached)
{
	*reached = 1;

	if ((co_await opsy::utility::suspended<uint32_t>{ &g_status }) != 0)
		co_return;                    // early return, mid-routine

	*reached = 2;
	co_await std::suspend_always{};
	*reached = 3;
}

} // namespace

OPSY_TEST(co_return_ends_the_routine_wherever_it_is)
{
	int reached = 0;
	g_status = 0;

	auto routine = stops_on_error(g_storage, &reached);
	routine.resume();
	CHECK(reached == 1);

	g_status = 1;                     // the error the routine bails out on
	routine.resume();

	CHECK(reached == 1);              // it did not go on to 2
	CHECK(routine.done());
}

OPSY_TEST(a_routine_reads_the_value_it_resumes_with_not_the_one_it_suspended_on)
{
	int reached = 0;
	g_status = 0;                     // what it would read at suspension time

	auto routine = stops_on_error(g_storage, &reached);
	routine.resume();

	g_status = 7;                     // changes while the routine is suspended
	routine.resume();

	// Had co_await evaluated at suspension time it would have seen 0 and gone
	// on to step 2. Reading at resumption is what lets a routine look at a
	// status register that only means something once the interrupt fired.
	CHECK(reached == 1);
	CHECK(routine.done());
}

OPSY_TEST(two_routines_with_their_own_storage_do_not_interfere)
{
	int first_reached = 0;
	int second_reached = 0;
	g_status = 0;

	auto first  = stops_on_error(g_storage, &first_reached);
	auto second = stops_on_error(g_other_storage, &second_reached);

	CHECK(static_cast<bool>(first));
	CHECK(static_cast<bool>(second));

	first.resume();
	CHECK(first_reached == 1);
	CHECK(second_reached == 0);       // untouched by the first one running

	second.resume();
	CHECK(second_reached == 1);

	g_status = 0;
	first.resume();
	CHECK(first_reached == 2);
	CHECK(second_reached == 1);       // still where it was
}

OPSY_TEST(a_moved_from_routine_is_empty_and_the_target_carries_on)
{
	g_progress = 0;
	auto routine = three_steps(g_storage);
	routine.resume();
	CHECK(g_progress == 1);

	auto moved = std::move(routine);

	CHECK(!static_cast<bool>(routine));   // the source holds nothing now
	CHECK(routine.done());                // and reports done, so a stale
	                                      // handler calling resume() is safe
	CHECK(static_cast<bool>(moved));

	moved.resume();
	CHECK(g_progress == 2);               // resumed where the original stopped
}

OPSY_TEST(a_default_constructed_routine_is_safe_to_resume)
{
	opsy::utility::routine none;

	CHECK(!static_cast<bool>(none));
	CHECK(none.done());

	// A handler declared before its routine is assigned would do exactly this
	// on any interrupt arriving in between.
	none.resume();
}

OPSY_TEST(the_frame_fits_in_the_storage_and_reports_its_size)
{
	auto routine = three_steps(g_storage);

	CHECK(static_cast<bool>(routine));
	CHECK(g_storage.used > 0);
	CHECK(g_storage.used <= sizeof(g_storage.bytes));

	// Sizing storage is not guesswork: used says what this routine needed, so
	// a project can start generous, look, and trim.
	CHECK(g_storage.used < sizeof(g_storage.bytes));
}

// Not covered here: a routine whose frame does not fit its storage. That path
// asserts before returning an empty routine, and the host harness makes a
// tripped assert abort the process -- correctly, since a test binary has no
// way to carry on. Checking it would need a build with NDEBUG, where the
// assert is gone and operator bool is what reports the failure.

namespace
{

/** @brief Counts objects alive inside a frame, to see destructors run. */
int g_alive = 0;

struct tracked
{
	tracked() { ++g_alive; }
	tracked(const tracked&) { ++g_alive; }
	~tracked() { --g_alive; }
};

opsy::utility::routine holds_an_object(storage&)
{
	const tracked held;
	co_await std::suspend_always{};
}

} // namespace

OPSY_TEST(storage_is_free_again_once_the_routine_using_it_is_released)
{
	g_alive = 0;

	{
		auto routine = holds_an_object(g_storage);
		routine.resume();

		CHECK(g_alive == 1);              // the object lives in the frame
		CHECK(g_storage.used > 0);        // and the storage says it is taken
	}

	// Releasing the routine destroyed the frame, which ran the destructors of
	// what it held, and gave the storage back.
	CHECK(g_alive == 0);
	CHECK(g_storage.used == 0);
}

OPSY_TEST(a_storage_can_be_reused_once_released)
{
	g_alive = 0;

	// What a driver retrying a transfer does: run one routine to its end,
	// release it, start another in the same storage.
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		auto routine = holds_an_object(g_storage);

		CHECK(static_cast<bool>(routine));
		routine.resume();
		CHECK(g_alive == 1);

		routine.resume();                 // off the end
		CHECK(routine.done());
	}

	// No frame outlived its routine, and none was built over a live one.
	CHECK(g_alive == 0);
	CHECK(g_storage.used == 0);
}

// Not covered here: building a routine into storage that still holds a live
// one. That is what the assert in operator new refuses, and a tripped assert
// aborts this binary -- correctly, since a test harness cannot carry on past
// one. What it prevents was reproduced by hand before the assert existed: the
// second frame was built over the first, the first's destructors never ran,
// and destroying the first handle tore down the second routine.
