/**
 ******************************************************************************
 * @file    test_condition_variable.cpp
 * @brief   On-target tests for @c opsy::condition_variable .
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

opsy::task<1024> g_waiter_a;
opsy::task<1024> g_waiter_b;
opsy::task<1024> g_notifier;

opsy::condition_variable g_condition;
opsy::isr_lock g_isr_lock;
opsy::mutex    g_task_mutex;

std::atomic<int> g_woken_a = 0;
std::atomic<int> g_woken_b = 0;
std::atomic<int> g_woken   = 0;
std::atomic<int> g_ready   = 0;

void stop_all()
{
	(void) g_waiter_a.kill();
	(void) g_waiter_b.kill();
	(void) g_notifier.kill();
	for (int guard = 0; guard < 200 &&
	     (g_waiter_a.is_started() || g_waiter_b.is_started() || g_notifier.is_started()); ++guard)
		opsy::sleep_for(1ms);
}

OPSY_QEMU_TEST(notify_one_wakes_exactly_one_of_two_waiters)
{
	g_woken_a = 0;
	g_woken_b = 0;
	g_ready   = 0;

	CHECK(g_waiter_a.start([] { ++g_ready; g_condition.wait(); g_woken_a = 1; }, "waiter-a"));
	CHECK(g_waiter_b.start([] { ++g_ready; g_condition.wait(); g_woken_b = 1; }, "waiter-b"));
	g_waiter_a.priority(opsy::task_priority::highest);
	g_waiter_b.priority(opsy::task_priority::highest);

	// Both raised above the runner, so both are already parked on the wait.
	CHECK(g_ready == 2);
	CHECK(g_woken_a == 0);
	CHECK(g_woken_b == 0);

	g_condition.notify_one();
	opsy::sleep_for(5ms);
	CHECK((g_woken_a + g_woken_b) == 1);   // exactly one

	g_condition.notify_one();
	opsy::sleep_for(5ms);
	CHECK((g_woken_a + g_woken_b) == 2);   // then the other

	stop_all();
}

OPSY_QEMU_TEST(notify_all_wakes_every_waiter)
{
	g_woken_a = 0;
	g_woken_b = 0;
	g_ready   = 0;

	CHECK(g_waiter_a.start([] { ++g_ready; g_condition.wait(); g_woken_a = 1; }, "waiter-a"));
	CHECK(g_waiter_b.start([] { ++g_ready; g_condition.wait(); g_woken_b = 1; }, "waiter-b"));
	g_waiter_a.priority(opsy::task_priority::highest);
	g_waiter_b.priority(opsy::task_priority::highest);
	CHECK(g_ready == 2);

	g_condition.notify_all();
	opsy::sleep_for(5ms);

	CHECK(g_woken_a == 1);
	CHECK(g_woken_b == 1);

	stop_all();
}

OPSY_QEMU_TEST(notify_on_an_empty_condition_variable_is_harmless)
{
	g_condition.notify_one();
	g_condition.notify_all();

	// And the scheduler is still fine afterwards.
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(5ms);
	CHECK(opsy::scheduler::now() > before);
}

OPSY_QEMU_TEST(wait_for_returns_no_timeout_when_notified_in_time)
{
	g_woken = 0;

	CHECK(g_notifier.start([]
	{
		opsy::sleep_for(5ms);
		g_condition.notify_one();
	}, "notifier"));
	g_notifier.priority(opsy::task_priority::highest);

	const auto before = opsy::scheduler::now();
	const auto status = g_condition.wait_for(500ms);
	const auto elapsed = opsy::scheduler::now() - before;

	CHECK(status == opsy::cv_status::no_timeout);
	CHECK(elapsed < 100ms);   // woken by the notify, not by the timeout

	stop_all();
}

OPSY_QEMU_TEST(wait_for_times_out_with_no_notification)
{
	const auto before = opsy::scheduler::now();
	const auto status = g_condition.wait_for(15ms);
	const auto elapsed = opsy::scheduler::now() - before;

	CHECK(status == opsy::cv_status::timeout);
	CHECK(elapsed >= 15ms);
}

OPSY_QEMU_TEST(a_timed_out_waiter_can_wait_again)
{
	// The timeout path unlinks the task from the condition variable and clears
	// waiting_; getting that wrong leaves the second wait corrupting the list.
	for (int round = 0; round < 3; ++round)
	{
		const auto status = g_condition.wait_for(5ms);
		CHECK(status == opsy::cv_status::timeout);
	}

	// And the variable still works for a real notification afterwards.
	g_woken = 0;
	CHECK(g_notifier.start([] { opsy::sleep_for(3ms); g_condition.notify_one(); }, "late-notifier"));
	g_notifier.priority(opsy::task_priority::highest);

	CHECK(g_condition.wait_for(500ms) == opsy::cv_status::no_timeout);

	stop_all();
}

OPSY_QEMU_TEST(waiting_with_a_mutex_releases_it_and_takes_it_back)
{
	g_woken = 0;
	g_ready = 0;

	// The waiter holds the mutex, then waits: OpSy must release it atomically
	// with the sleep, or the notifier below could never take it.
	CHECK(g_waiter_a.start([]
	{
		g_isr_lock.lock();
		g_ready = 1;
		(void) g_condition.wait_for(g_isr_lock, 500ms);
		g_woken = 1;
		g_isr_lock.unlock();
	}, "mutex-waiter"));
	g_waiter_a.priority(opsy::task_priority::highest);

	CHECK(g_ready == 1);
	CHECK(g_woken == 0);

	// If the mutex were still held by the parked waiter, this would block.
	g_isr_lock.lock();
	g_isr_lock.unlock();

	g_condition.notify_one();
	opsy::sleep_for(10ms);
	CHECK(g_woken == 1);

	// And the waiter released it on its way out.
	g_isr_lock.lock();
	g_isr_lock.unlock();

	stop_all();
}

OPSY_QEMU_TEST(wait_until_a_future_deadline_times_out_at_that_deadline)
{
	const auto deadline = opsy::scheduler::now() + 15ms;
	const auto status = g_condition.wait_until(deadline);

	CHECK(status == opsy::cv_status::timeout);
	CHECK(opsy::scheduler::now() >= deadline);
}

OPSY_QEMU_TEST(several_waiters_with_different_timeouts_expire_in_order)
{
	g_woken_a = 0;
	g_woken_b = 0;

	// Two timeouts on the same condition variable, inserted out of order into
	// the scheduler's timeout list: the later one is started first.
	CHECK(g_waiter_a.start([] { (void) g_condition.wait_for(40ms); g_woken_a = 1; }, "late"));
	g_waiter_a.priority(opsy::task_priority::highest);

	CHECK(g_waiter_b.start([] { (void) g_condition.wait_for(10ms); g_woken_b = 1; }, "early"));
	g_waiter_b.priority(opsy::task_priority::highest);

	opsy::sleep_for(20ms);
	CHECK(g_woken_b == 1);   // the shorter timeout fired
	CHECK(g_woken_a == 0);   // the longer one has not

	opsy::sleep_for(40ms);
	CHECK(g_woken_a == 1);

	stop_all();
}


// ─────────────── waiting with each of the two lock types ───────────────────
// cv.wait must accept both an isr_lock and a mutex: the first is released by
// dropping BASEPRI, the second by handing ownership to a waiter. The task
// records which kind it holds before the service call, so the handler reads a
// typed variant rather than guessing from a pointer.

OPSY_QEMU_TEST(waiting_with_a_task_mutex_releases_it_and_takes_it_back)
{
	g_woken = 0;
	g_ready = 0;

	CHECK(g_waiter_a.start([]
	{
		g_task_mutex.lock();
		g_ready = 1;
		(void) g_condition.wait_for(g_task_mutex, 500ms);
		g_woken = 1;
		CHECK(g_task_mutex.is_locked());   // owned again on wake
		g_task_mutex.unlock();
	}, "mutex-waiter"));
	g_waiter_a.priority(opsy::task_priority::highest);

	CHECK(g_ready == 1);
	CHECK(g_woken == 0);

	// Released while it sleeps, so this must not block.
	CHECK(g_task_mutex.try_lock());
	g_task_mutex.unlock();

	g_condition.notify_one();
	opsy::sleep_for(10ms);
	CHECK(g_woken == 1);
	CHECK(!g_task_mutex.is_locked());   // and released on the way out

	stop_all();
}

OPSY_QEMU_TEST(a_waiter_that_wakes_to_a_taken_mutex_blocks_on_it)
{
	g_woken = 0;
	g_ready = 0;

	CHECK(g_waiter_a.start([]
	{
		g_task_mutex.lock();
		g_ready = 1;
		(void) g_condition.wait_for(g_task_mutex, 500ms);
		g_woken = 1;                       // only once it owns it again
		g_task_mutex.unlock();
	}, "mutex-waiter"));
	g_waiter_a.priority(opsy::task_priority::highest);
	CHECK(g_ready == 1);

	// Take the mutex ourselves, then notify: waking cannot mean running, the
	// task has to block on the mutex until we release it.
	g_task_mutex.lock();
	g_condition.notify_one();
	opsy::sleep_for(10ms);
	CHECK(g_woken == 0);                   // woken, but blocked on the mutex

	g_task_mutex.unlock();
	opsy::sleep_for(10ms);
	CHECK(g_woken == 1);

	stop_all();
}

OPSY_QEMU_TEST(a_task_mutex_wait_that_times_out_still_takes_the_mutex_back)
{
	g_ready = 0;
	g_woken = 0;

	CHECK(g_waiter_a.start([]
	{
		g_task_mutex.lock();
		g_ready = 1;
		const auto status = g_condition.wait_for(g_task_mutex, 10ms);
		CHECK(status == opsy::cv_status::timeout);
		CHECK(g_task_mutex.is_locked());
		g_woken = 1;
		g_task_mutex.unlock();
	}, "timing-out"));
	g_waiter_a.priority(opsy::task_priority::highest);
	CHECK(g_ready == 1);

	opsy::sleep_for(30ms);
	CHECK(g_woken == 1);
	CHECK(!g_task_mutex.is_locked());

	stop_all();
}

} // namespace
