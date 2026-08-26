/**
 ******************************************************************************
 * @file    test_scheduler.cpp
 * @brief   On-target behavioural tests for the OpSy scheduler.
 *
 *          These are the cases neither the compile-only cross build nor the
 *          host suite can run: they need a scheduler that actually owns the
 *          CPU, with SysTick firing and context switches happening.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

namespace
{

using namespace std::chrono_literals;

// Helper tasks, reused across cases. Each case must leave them stopped.
opsy::task<1024> g_helper;
opsy::task<1024> g_second_helper;
opsy::condition_variable g_condition;
opsy::mutex g_mutex;

volatile int g_helper_ran = 0;
volatile int g_second_ran = 0;

/** @brief Stops a task and waits for it to actually leave the scheduler. */
void ensure_stopped(opsy::task<1024>& task)
{
	(void) task.stop();
	for (int guard = 0; guard < 100 && task.is_started(); ++guard)
		opsy::sleep_for(1ms);
}

// ───────────────────────────── basic liveness ──────────────────────────────

OPSY_QEMU_TEST(scheduler_is_running_and_time_advances)
{
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(10ms);
	const auto after = opsy::scheduler::now();

	CHECK(after > before);
	CHECK((after - before) >= 10ms);
}

OPSY_QEMU_TEST(a_higher_priority_task_preempts_the_runner)
{
	g_helper_ran = 0;

	CHECK(g_helper.start([] { g_helper_ran = 1; }, "helper"));
	g_helper.priority(opsy::task_priority::high);

	// The helper outranks the runner, so it must have run to completion
	// before start() even returns control here.
	CHECK(g_helper_ran == 1);

	ensure_stopped(g_helper);
}

OPSY_QEMU_TEST(a_lower_priority_task_runs_only_when_we_yield)
{
	g_helper_ran = 0;

	CHECK(g_helper.start([] { g_helper_ran = 1; }, "helper"));
	g_helper.priority(opsy::task_priority::lowest);
	CHECK(g_helper_ran == 0);   // still waiting behind us

	opsy::sleep_for(5ms);       // yield
	CHECK(g_helper_ran == 1);

	ensure_stopped(g_helper);
}

// ─────────────── regression: wait_until with an elapsed deadline ───────────
// wait_until computed wait_for(deadline - now()), negative once the deadline
// had passed — and the service call reads a negative duration as the "no
// timeout" sentinel, so the task blocked with no timer at all. The standard
// predicate loop below never terminated on its second iteration.

OPSY_QEMU_TEST(wait_until_an_elapsed_deadline_reports_timeout_at_once)
{
	const auto deadline = opsy::scheduler::now() - 10ms;   // already in the past

	const auto before = opsy::scheduler::now();
	const auto status = g_condition.wait_until(deadline);
	const auto after = opsy::scheduler::now();

	CHECK(status == opsy::cv_status::timeout);
	CHECK((after - before) < 5ms);   // returned without blocking
}

OPSY_QEMU_TEST(the_standard_wait_until_predicate_loop_terminates)
{
	const auto deadline = opsy::scheduler::now() + 5ms;
	bool predicate = false;
	int iterations = 0;

	while (!predicate)
	{
		++iterations;
		if (g_condition.wait_until(deadline) == opsy::cv_status::timeout)
			break;
		CHECK(iterations < 100);
	}

	CHECK(!predicate);
	CHECK(iterations >= 1);
}

OPSY_QEMU_TEST(wait_for_a_negative_duration_reports_timeout_at_once)
{
	const auto before = opsy::scheduler::now();
	const auto status = g_condition.wait_for(-1ms);
	const auto after = opsy::scheduler::now();

	CHECK(status == opsy::cv_status::timeout);
	CHECK((after - before) < 5ms);
}

// ──────────────────────── condition variable, nominal ──────────────────────

OPSY_QEMU_TEST(wait_for_times_out_when_nobody_notifies)
{
	const auto before = opsy::scheduler::now();
	const auto status = g_condition.wait_for(10ms);
	const auto after = opsy::scheduler::now();

	CHECK(status == opsy::cv_status::timeout);
	CHECK((after - before) >= 10ms);
}

OPSY_QEMU_TEST(notify_one_wakes_a_waiter_before_its_timeout)
{
	g_helper_ran = 0;

	// A higher-priority helper notifies us after we have gone to sleep.
	CHECK(g_helper.start([]
	{
		opsy::sleep_for(5ms);
		g_helper_ran = 1;
		g_condition.notify_one();
	}, "notifier"));
	g_helper.priority(opsy::task_priority::high);

	const auto status = g_condition.wait_for(500ms);

	CHECK(status == opsy::cv_status::no_timeout);
	CHECK(g_helper_ran == 1);

	ensure_stopped(g_helper);
}

// ───────── regression: a terminated task was left linked into ready_ ───────
// The terminate service call erased the task from all_tasks_, timeouts_ and
// the condition variable list, but never from ready_ — the list holding every
// runnable-but-not-running task. The task stayed linked, and the next switch
// popped a task already reported terminated.

OPSY_QEMU_TEST(stopping_a_runnable_task_removes_it_from_the_ready_list)
{
	g_helper_ran = 0;

	// Lower priority than the runner, so it sits in ready_ without running.
	CHECK(g_helper.start([] { g_helper_ran = 1; while (true) opsy::sleep_for(1ms); }, "victim"));
	g_helper.priority(opsy::task_priority::lowest);
	CHECK(g_helper_ran == 0);

	CHECK(g_helper.stop());       // terminate it while it is in ready_
	CHECK(!g_helper.is_started());

	// If it were still linked, the scheduler would resume it here.
	opsy::sleep_for(20ms);
	CHECK(g_helper_ran == 0);

	// And the scheduler must still be sane afterwards.
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(5ms);
	CHECK(opsy::scheduler::now() > before);
}

OPSY_QEMU_TEST(stopping_a_ready_task_leaves_other_tasks_runnable)
{
	g_helper_ran = 0;
	g_second_ran = 0;

	CHECK(g_helper.start([] { while (true) { g_helper_ran = 1; opsy::sleep_for(1ms); } }, "keeper"));
	g_helper.priority(opsy::task_priority::lowest);

	CHECK(g_second_helper.start([] { while (true) { g_second_ran = 1; opsy::sleep_for(1ms); } }, "victim"));
	g_second_helper.priority(opsy::task_priority::lowest);

	CHECK(g_second_helper.stop());   // drop the second while both are in ready_
	opsy::sleep_for(20ms);

	// Erasing one must not have unlinked the other: this is the failure mode
	// the embedded_list guard covers, seen from the scheduler side.
	CHECK(g_helper_ran == 1);
	CHECK(g_helper.is_started());

	ensure_stopped(g_helper);
	ensure_stopped(g_second_helper);
}

// ──────── regression: waiting_ left dangling by the SysTick timeout ────────
// systick_handler removed a timed-out task from the condition variable's list
// but left task.waiting_ pointing at it. A later stop() then took the
// "waiting_ != nullptr" branch and erased the task from the condition
// variable's list while it was linked into ready_, corrupting both.

OPSY_QEMU_TEST(stopping_a_task_that_timed_out_waiting_leaves_the_lists_intact)
{
	g_helper_ran = 0;

	CHECK(g_helper.start([]
	{
		// Time out on the wait, then stay alive and runnable.
		(void) g_condition.wait_for(5ms);
		g_helper_ran = 1;
		while (true)
			opsy::sleep_for(1ms);
	}, "timed-out"));
	g_helper.priority(opsy::task_priority::high);

	opsy::sleep_for(20ms);
	CHECK(g_helper_ran == 1);        // the timeout fired and it resumed

	// Stop it now that waiting_ is stale.
	CHECK(g_helper.stop());
	CHECK(!g_helper.is_started());

	// The condition variable must still work for somebody else.
	const auto status = g_condition.wait_for(10ms);
	CHECK(status == opsy::cv_status::timeout);

	opsy::sleep_for(5ms);
}

OPSY_QEMU_TEST(a_priority_change_after_a_wait_timeout_is_harmless)
{
	// Same stale waiting_ pointer, reached through update_priority instead of
	// through terminate: it took the same branch and pushed the task into the
	// condition variable's list while it was in ready_.
	g_helper_ran = 0;

	CHECK(g_helper.start([]
	{
		(void) g_condition.wait_for(5ms);
		g_helper_ran = 1;
		while (true)
			opsy::sleep_for(1ms);
	}, "reprioritised"));
	g_helper.priority(opsy::task_priority::high);

	opsy::sleep_for(20ms);
	CHECK(g_helper_ran == 1);

	g_helper.priority(opsy::task_priority::low);
	g_helper.priority(opsy::task_priority::high);

	opsy::sleep_for(10ms);
	CHECK(g_helper.is_started());

	ensure_stopped(g_helper);
}

// ─────────────────────────── mutex and sleeping ────────────────────────────

OPSY_QEMU_TEST(a_mutex_serialises_two_tasks)
{
	g_helper_ran = 0;

	g_mutex.lock();

	CHECK(g_helper.start([]
	{
		g_mutex.lock();      // blocks until the runner releases
		g_helper_ran = 1;
		g_mutex.unlock();
	}, "contender"));
	g_helper.priority(opsy::task_priority::high);

	// The helper outranks us but must be stuck on the mutex.
	CHECK(g_helper_ran == 0);

	g_mutex.unlock();
	opsy::sleep_for(5ms);
	CHECK(g_helper_ran == 1);

	ensure_stopped(g_helper);
}

OPSY_QEMU_TEST(sleep_until_an_absolute_deadline_waits_the_right_amount)
{
	const auto deadline = opsy::scheduler::now() + 15ms;
	opsy::sleep_until(deadline);
	CHECK(opsy::scheduler::now() >= deadline);
}

OPSY_QEMU_TEST(sleep_until_a_past_deadline_returns_promptly)
{
	const auto before = opsy::scheduler::now();
	opsy::sleep_until(before - 10ms);
	CHECK((opsy::scheduler::now() - before) < 5ms);
}

// ─────────────────────── task lifecycle and enumeration ────────────────────

OPSY_QEMU_TEST(a_task_that_returns_terminates_itself)
{
	g_helper_ran = 0;

	CHECK(g_helper.start([] { g_helper_ran = 1; }, "short-lived"));
	g_helper.priority(opsy::task_priority::high);
	CHECK(g_helper_ran == 1);

	// Returning from the entry callback must unwind through terminate_task.
	for (int guard = 0; guard < 100 && g_helper.is_started(); ++guard)
		opsy::sleep_for(1ms);
	CHECK(!g_helper.is_started());

	// And the slot must be reusable.
	g_helper_ran = 0;
	CHECK(g_helper.start([] { g_helper_ran = 1; }, "reused"));
	g_helper.priority(opsy::task_priority::high);
	CHECK(g_helper_ran == 1);
	ensure_stopped(g_helper);
}

OPSY_QEMU_TEST(all_tasks_enumerates_through_the_const_interface)
{
	// scheduler::all_tasks() returns a const reference, and iterating it is
	// the documented way to enumerate tasks — which did not compile before
	// the embedded_list const-iterator fix.
	std::size_t count = 0;
	for (const auto& task : opsy::scheduler::all_tasks())
	{
		(void) task;
		++count;
	}

	CHECK(count >= 1);   // at least the runner
}

OPSY_QEMU_TEST(starting_an_already_started_task_is_refused)
{
	CHECK(g_helper.start([] { while (true) opsy::sleep_for(1ms); }, "once"));
	g_helper.priority(opsy::task_priority::lowest);

	CHECK(!g_helper.start([] {}, "twice"));   // must not restart it

	ensure_stopped(g_helper);
}

// ─────────────────────────── critical section ──────────────────────────────
// try_critical_section's contract — that a second, nested request comes back
// invalid — is not observable: critical_section exposes no public way to ask
// whether it is valid, and its only effect is releasing the flag on
// destruction. So this only pins that taking and releasing one leaves the
// scheduler running. The race the exchange() fix closes is not reachable from
// here either; see tests/README.md.

OPSY_QEMU_TEST(taking_and_releasing_a_critical_section_leaves_the_scheduler_running)
{
	{
		auto section = opsy::scheduler::try_critical_section();
		(void) section;

		{
			auto nested = opsy::scheduler::try_critical_section();
			(void) nested;
		}
	}

	const auto before = opsy::scheduler::now();
	opsy::sleep_for(5ms);
	CHECK(opsy::scheduler::now() > before);
}

} // namespace
