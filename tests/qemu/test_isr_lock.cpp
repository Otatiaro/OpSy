/**
 ******************************************************************************
 * @file    test_mutex.cpp
 * @brief   On-target tests for @c opsy::isr_lock .
 *
 *          The ISR-facing lock: a mask, with no owner and no blocking. The
 *          blocking task-to-task mutex is covered by test_mutex.cpp.
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

opsy::task<1024> g_first;
opsy::task<1024> g_second;

opsy::isr_lock g_plain;                                // no isr_priority: task-to-task masking only
opsy::isr_lock g_with_priority{ opsy::isr_priority{0x80} }; // also locks out ISRs

std::atomic<int> g_stage       = 0;
std::atomic<int> g_first_ran   = 0;
std::atomic<int> g_second_ran  = 0;
std::atomic<int> g_inside      = 0;   // tasks currently inside the guarded region
std::atomic<int> g_max_inside  = 0;
std::atomic<int> g_iterations  = 0;

void stop_all()
{
	(void) g_first.kill();
	(void) g_second.kill();
	for (int guard = 0; guard < 200 && (g_first.is_started() || g_second.is_started()); ++guard)
		opsy::sleep_for(1ms);
}

/**
 * @brief Body shared by the two contending tasks.
 *
 *        A named function rather than a lambda object: task::start takes its
 *        callback by rvalue reference, so one const lambda cannot be handed to
 *        both tasks. Each start() gets its own trivial wrapper instead.
 */
void contend_on_isr_lock()
{
	while (g_stage == 0)
	{
		g_plain.lock();

		const int inside = g_inside + 1;
		g_inside = inside;
		if (inside > g_max_inside)
			g_max_inside = inside;

		busy_wait(100);

		g_inside = inside - 1;
		++g_iterations;

		g_plain.unlock();
		opsy::sleep_for(1ms);
	}
}

OPSY_QEMU_TEST(an_isr_lock_blocks_a_higher_priority_task_until_released)
{
	g_first_ran = 0;
	g_plain.lock();

	CHECK(g_first.start([]
	{
		g_plain.lock();
		g_first_ran = 1;
		g_plain.unlock();
	}, "waiter"));
	g_first.priority(opsy::task_priority::highest);

	// It outranks the runner, so the only thing keeping it out is the mutex.
	CHECK(g_first_ran == 0);

	g_plain.unlock();
	opsy::sleep_for(5ms);
	CHECK(g_first_ran == 1);

	stop_all();
}

OPSY_QEMU_TEST(an_isr_lock_can_be_relocked_after_release)
{
	for (int round = 0; round < 4; ++round)
	{
		g_plain.lock();
		g_plain.unlock();
	}

	// Still usable by somebody else afterwards.
	g_first_ran = 0;
	CHECK(g_first.start([] { g_plain.lock(); g_first_ran = 1; g_plain.unlock(); }, "relock"));
	g_first.priority(opsy::task_priority::highest);
	CHECK(g_first_ran == 1);

	stop_all();
}

OPSY_QEMU_TEST(an_isr_lock_actually_excludes_two_contending_tasks)
{
	g_inside     = 0;
	g_max_inside = 0;
	g_iterations = 0;
	g_stage      = 0;

	CHECK(g_first.start([] { contend_on_isr_lock(); }, "contender-a"));
	CHECK(g_second.start([] { contend_on_isr_lock(); }, "contender-b"));
	g_first.priority(opsy::task_priority::normal);
	g_second.priority(opsy::task_priority::normal);

	opsy::sleep_for(60ms);
	g_stage = 1;
	opsy::sleep_for(10ms);

	CHECK(g_iterations > 2);      // both really ran
	CHECK(g_max_inside == 1);     // and never overlapped
	CHECK(g_inside == 0);

	stop_all();
}

OPSY_QEMU_TEST(a_priority_carrying_isr_lock_reports_its_priority)
{
	CHECK(g_with_priority.priority().has_value());
	CHECK(g_with_priority.priority().value_or(opsy::isr_priority{0}).value() == 0x80);
	CHECK(!g_plain.priority().has_value());
}

OPSY_QEMU_TEST(a_priority_carrying_isr_lock_locks_and_unlocks)
{
	// Locking one raises BASEPRI to the mutex's priority for the guarded
	// region, so this exercises the ISR-masking path rather than just the
	// task-switch one. No sleeping under it: a mutex with a non-zero priority
	// takes the scheduler's critical section too (scheduler_inl.hpp:123), and
	// the sleep service call asserts it is not held.
	for (int round = 0; round < 4; ++round)
	{
		g_with_priority.lock();
		busy_wait(200);
		g_with_priority.unlock();
	}

	g_first_ran = 0;
	CHECK(g_first.start([]
	{
		g_with_priority.lock();
		g_first_ran = 1;
		g_with_priority.unlock();
	}, "prio-waiter"));
	g_first.priority(opsy::task_priority::highest);
	CHECK(g_first_ran == 1);

	stop_all();
}

// A held mutex is not std::mutex: for a mutex with no isr_priority, lock()
// takes the scheduler's critical section, and do_switch refuses to switch
// while that is held. So holding one suspends task switching altogether
// rather than merely excluding tasks that want the same mutex — and sleeping
// under one is forbidden outright (the sleep service call asserts on it).

OPSY_QEMU_TEST(a_held_isr_lock_suspends_task_switching)
{
	g_second_ran = 0;
	g_stage      = 0;

	CHECK(g_second.start([]
	{
		while (g_stage == 0)
		{
			++g_second_ran;
			opsy::sleep_for(1ms);
		}
	}, "ticker"));
	g_second.priority(opsy::task_priority::highest);   // above the runner

	// It outranks us, so it has already run at least once.
	opsy::sleep_for(5ms);
	CHECK(g_second_ran > 0);

	g_plain.lock();
	const int under_lock = g_second_ran;
	busy_wait(20000);                 // long enough for several SysTicks
	const int still = g_second_ran;
	g_plain.unlock();

	// Even a higher-priority task did not get in: the critical section the
	// mutex holds blocks the switch, not just the mutex itself.
	CHECK(still == under_lock);

	// And everything resumes once it is released.
	opsy::sleep_for(5ms);
	CHECK(g_second_ran > still);

	g_stage = 1;
	stop_all();
}

OPSY_QEMU_TEST(time_keeps_advancing_while_an_isr_lock_is_held)
{
	// The switch is suspended, but SysTick still runs: the clock must not
	// stall under a held mutex.
	g_plain.lock();
	const auto before = opsy::scheduler::now();
	busy_wait(20000);
	const auto after = opsy::scheduler::now();
	g_plain.unlock();

	CHECK(after >= before);
}

} // namespace
