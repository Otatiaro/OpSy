/**
 ******************************************************************************
 * @file    test_critical_section.cpp
 * @brief   On-target tests for @c scheduler::try_critical_section .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>

namespace
{

using namespace std::chrono_literals;

/** @brief A short deliberate delay, wide enough for a preemption to land in. */
void busy_wait(int rounds)
{
	for (int i = 0; i < rounds; ++i)
		opsy::cortex_m::nop();
}

opsy::task<1024> g_contender;

std::atomic<int> g_holders     = 0;   // how many tasks believe they hold it right now
std::atomic<int> g_max_holders = 0;   // the worst overlap observed
std::atomic<int> g_acquired    = 0;   // successful acquisitions, to prove the loop ran
std::atomic<bool> g_stop       = false;

/**
 * @brief Takes the section, records the overlap, releases.
 *
 *        If try_critical_section were a plain load-then-store, a preemption
 *        landing between the two would let a second task take the section
 *        while this one still holds it, and g_max_holders would exceed 1.
 */
void contend_once()
{
	auto section = opsy::scheduler::try_critical_section();
	if (!section)
		return;

	const int holders = g_holders + 1;
	g_holders = holders;
	if (holders > g_max_holders)
		g_max_holders = holders;

	// Widen the window the other task has to get it wrong.
	busy_wait(50);

	g_holders = holders - 1;
	++g_acquired;
}

OPSY_QEMU_TEST(a_second_request_while_held_comes_back_invalid)
{
	auto outer = opsy::scheduler::try_critical_section();
	CHECK(static_cast<bool>(outer));

	{
		auto inner = opsy::scheduler::try_critical_section();
		CHECK(!static_cast<bool>(inner));   // already held: must not claim it
	}

	// The invalid inner handle going out of scope must not have released the
	// section the outer one still holds.
	{
		auto another = opsy::scheduler::try_critical_section();
		CHECK(!static_cast<bool>(another));
	}
}

OPSY_QEMU_TEST(the_section_is_available_again_after_release)
{
	{
		auto section = opsy::scheduler::try_critical_section();
		CHECK(static_cast<bool>(section));
	}

	auto again = opsy::scheduler::try_critical_section();
	CHECK(static_cast<bool>(again));
}

OPSY_QEMU_TEST(a_moved_from_handle_does_not_release_the_section)
{
	auto original = opsy::scheduler::try_critical_section();
	CHECK(static_cast<bool>(original));

	{
		auto moved = std::move(original);
		CHECK(static_cast<bool>(moved));
		CHECK(!static_cast<bool>(original));   // ownership transferred out
	}

	// `moved` released it at scope exit, so it is free again.
	auto again = opsy::scheduler::try_critical_section();
	CHECK(static_cast<bool>(again));
}

/**
 * @brief Two tasks hammering try_critical_section at once.
 *
 * @warning This is a probabilistic check, not a proof. The window the atomic
 *          read-modify-write closes is a couple of instructions wide, and QEMU
 *          schedules deterministically, so a run that stays clean does not
 *          establish that the section is race-free — only that it is not
 *          grossly broken. The narrow interleaving is called out as uncovered
 *          in tests/README.md.
 */
OPSY_QEMU_TEST(two_tasks_never_hold_the_section_at_once)
{
	g_holders     = 0;
	g_max_holders = 0;
	g_acquired    = 0;
	g_stop        = false;

	CHECK(g_contender.start([]
	{
		while (!g_stop)
		{
			// Bounded bursts, then yield: a free-running loop here starves the
			// emulator rather than the scheduler — every acquisition masks and
			// unmasks, and QEMU ends a translation block on each barrier.
			for (int burst = 0; burst < 16 && !g_stop; ++burst)
				contend_once();
			opsy::sleep_for(1ms);
		}
	}, "contender"));
	g_contender.priority(opsy::task_priority::normal);   // below the runner

	const auto deadline = opsy::scheduler::now() + 30ms;
	while (opsy::scheduler::now() < deadline)
	{
		contend_once();
		opsy::sleep_for(1ms);   // let the contender in, and let SysTick land
	}

	g_stop = true;
	for (int guard = 0; guard < 100 && g_contender.is_started(); ++guard)
		opsy::sleep_for(1ms);
	(void) g_contender.stop();

	CHECK(g_acquired > 1);        // the loop really ran on both sides
	CHECK(g_max_holders == 1);    // and never overlapped
	CHECK(g_holders == 0);        // balanced acquire/release
}

} // namespace
