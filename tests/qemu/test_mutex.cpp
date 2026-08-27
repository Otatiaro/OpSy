/**
 ******************************************************************************
 * @file    test_mutex.cpp
 * @brief   On-target tests for @c opsy::mutex — the blocking, owning mutex.
 *
 *          What sets it apart from @ref opsy::isr_lock (covered next door) is
 *          everything these cases check: it blocks rather than declining, it
 *          has an owner, locks may be released in any order, and a holder is
 *          raised while a more important task waits behind it.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>
#include <mutex>

namespace
{

using namespace std::chrono_literals;

opsy::task<1024> g_low;
opsy::task<1024> g_middle;
opsy::task<1024> g_high;

opsy::mutex g_a;
opsy::mutex g_b;

std::atomic<int> g_order_index = 0;
std::atomic<int> g_order[6]{};
std::atomic<int> g_low_ran     = 0;
std::atomic<int> g_high_ran    = 0;
std::atomic<int> g_middle_ran  = 0;
std::atomic<int> g_inside      = 0;
std::atomic<int> g_max_inside  = 0;
std::atomic<int> g_iterations  = 0;
std::atomic<bool> g_stop       = false;

void record(int who)
{
	const int index = g_order_index;
	if (index < 6)
	{
		g_order[index] = who;
		g_order_index = index + 1;
	}
}

void stop_all()
{
	g_stop = true;
	(void) g_low.kill();
	(void) g_middle.kill();
	(void) g_high.kill();
	for (int guard = 0; guard < 200 &&
	     (g_low.is_started() || g_middle.is_started() || g_high.is_started()); ++guard)
		opsy::sleep_for(1ms);
	g_stop = false;
}

/**
 * @brief Body shared by the two contending tasks.
 *
 *        A named function: task::start takes its callback by rvalue reference,
 *        so one const lambda object cannot be handed to both tasks.
 */
void contend()
{
	while (!g_stop)
	{
		g_a.lock();

		const int inside = g_inside + 1;
		g_inside = inside;
		if (inside > g_max_inside)
			g_max_inside = inside;

		for (int i = 0; i < 50; ++i)
			opsy::cortex_m::nop();

		g_inside = inside - 1;
		++g_iterations;

		g_a.unlock();
		opsy::sleep_for(1ms);
	}
}

// ───────────────────────────── ownership ───────────────────────────────────

OPSY_QEMU_TEST(a_fresh_mutex_is_unlocked)
{
	opsy::mutex fresh;
	CHECK(!fresh.is_locked());
}

OPSY_QEMU_TEST(locking_and_unlocking_round_trips)
{
	CHECK(!g_a.is_locked());
	g_a.lock();
	CHECK(g_a.is_locked());
	g_a.unlock();
	CHECK(!g_a.is_locked());
}

OPSY_QEMU_TEST(try_lock_succeeds_on_a_free_mutex_and_fails_on_a_held_one)
{
	CHECK(g_a.try_lock());
	CHECK(g_a.is_locked());

	// A second task must be refused, without blocking.
	g_high_ran = 0;
	CHECK(g_high.start([]
	{
		g_high_ran = g_a.try_lock() ? 1 : 2;   // 2 == correctly refused
	}, "try-locker"));
	g_high.priority(opsy::task_priority::highest);

	CHECK(g_high_ran == 2);

	g_a.unlock();
	stop_all();
}

OPSY_QEMU_TEST(std_lock_guard_works_with_it)
{
	{
		std::lock_guard guard{g_a};
		CHECK(g_a.is_locked());
	}
	CHECK(!g_a.is_locked());
}

// No std::unique_lock case here on purpose: its unlock() builds a
// std::system_error on the failure path, which pulls in string allocation even
// under -fno-exceptions — the image then hits _sbrk and fails. mutex satisfies
// Lockable, so unique_lock does work; it is simply not linkable in a no-heap
// image. std::lock_guard has no such path and is the one to use here.

// ────────────────────────────── blocking ───────────────────────────────────
// The property isr_lock cannot have: a task that cannot take the mutex is
// suspended and resumes owning it, rather than being told no.

OPSY_QEMU_TEST(a_waiting_task_blocks_and_resumes_owning_the_mutex)
{
	g_high_ran = 0;
	g_a.lock();

	CHECK(g_high.start([]
	{
		g_a.lock();          // blocks: the runner holds it
		g_high_ran = 1;
		CHECK(g_a.is_locked());
		g_a.unlock();
	}, "waiter"));
	g_high.priority(opsy::task_priority::highest);

	// It outranks the runner and still has not run past the lock.
	CHECK(g_high_ran == 0);
	CHECK(g_a.is_locked());

	g_a.unlock();
	opsy::sleep_for(5ms);
	CHECK(g_high_ran == 1);
	CHECK(!g_a.is_locked());

	stop_all();
}

OPSY_QEMU_TEST(waiters_are_served_in_priority_order)
{
	g_order_index = 0;
	g_a.lock();

	// Started low-to-high; they must be served high-to-low.
	CHECK(g_low.start([] { g_a.lock(); record(1); g_a.unlock(); }, "low"));
	g_low.priority(opsy::task_priority::low);

	CHECK(g_middle.start([] { g_a.lock(); record(2); g_a.unlock(); }, "middle"));
	g_middle.priority(opsy::task_priority::normal);

	CHECK(g_high.start([] { g_a.lock(); record(3); g_a.unlock(); }, "high"));
	g_high.priority(opsy::task_priority::high);

	CHECK(g_order_index == 0);   // all three parked on the mutex

	g_a.unlock();
	opsy::sleep_for(30ms);

	CHECK(g_order_index == 3);
	CHECK(g_order[0] == 3);
	CHECK(g_order[1] == 2);
	CHECK(g_order[2] == 1);

	stop_all();
}

OPSY_QEMU_TEST(a_mutex_actually_excludes_two_contending_tasks)
{
	g_inside = 0;
	g_max_inside = 0;
	g_iterations = 0;
	g_stop = false;

	CHECK(g_low.start([] { contend(); }, "contender-a"));
	CHECK(g_middle.start([] { contend(); }, "contender-b"));
	g_low.priority(opsy::task_priority::normal);
	g_middle.priority(opsy::task_priority::normal);

	opsy::sleep_for(40ms);
	g_stop = true;
	opsy::sleep_for(10ms);

	CHECK(g_iterations > 2);
	CHECK(g_max_inside == 1);
	CHECK(g_inside == 0);

	stop_all();
}

// ─────────────────────── release order is free ─────────────────────────────
// isr_lock requires strictly LIFO release across locks; a real mutex does not,
// and std::mutex never did.

OPSY_QEMU_TEST(two_mutexes_can_be_released_in_any_order)
{
	g_a.lock();
	g_b.lock();

	g_a.unlock();            // out of order on purpose
	CHECK(!g_a.is_locked());
	CHECK(g_b.is_locked());  // b must still be held, and still exclusive

	// Prove b really still excludes: a higher-priority task must block on it.
	g_high_ran = 0;
	CHECK(g_high.start([] { g_b.lock(); g_high_ran = 1; g_b.unlock(); }, "on-b"));
	g_high.priority(opsy::task_priority::highest);
	CHECK(g_high_ran == 0);

	g_b.unlock();
	opsy::sleep_for(5ms);
	CHECK(g_high_ran == 1);

	stop_all();
}

// ─────────────────────── priority inheritance ──────────────────────────────
// The classic inversion: a low-priority task holds the mutex, a high-priority
// one wants it, and a middle-priority task would otherwise run ahead of the
// holder and starve it — leaving the high one waiting on the middle one.

OPSY_QEMU_TEST(a_holder_is_raised_while_a_more_important_task_waits)
{
	g_low_ran = 0;
	g_high_ran = 0;
	g_stop = false;

	// Low takes the mutex and keeps running.
	CHECK(g_low.start([]
	{
		g_a.lock();
		g_low_ran = 1;
		while (!g_stop)
			opsy::sleep_for(1ms);
		g_a.unlock();
		g_low_ran = 2;
	}, "holder"));
	g_low.priority(opsy::task_priority::low);

	opsy::sleep_for(5ms);
	CHECK(g_low_ran == 1);
	CHECK(g_a.is_locked());
	CHECK(g_low.priority() == opsy::task_priority::low);   // requested one, unchanged

	// High blocks on the mutex, which must raise the holder.
	CHECK(g_high.start([] { g_a.lock(); g_high_ran = 1; g_a.unlock(); }, "waiter"));
	g_high.priority(opsy::task_priority::high);

	opsy::sleep_for(5ms);
	CHECK(g_high_ran == 0);   // still blocked

	// priority() reports the requested priority, so it must not have moved,
	// even though the effective one has.
	CHECK(g_low.priority() == opsy::task_priority::low);

	g_stop = true;
	opsy::sleep_for(20ms);

	CHECK(g_low_ran == 2);
	CHECK(g_high_ran == 1);
	CHECK(g_low.priority() == opsy::task_priority::low);   // restored

	stop_all();
}

OPSY_QEMU_TEST(a_middle_priority_task_cannot_starve_a_raised_holder)
{
	g_low_ran = 0;
	g_middle_ran = 0;
	g_high_ran = 0;
	g_order_index = 0;
	g_stop = false;

	// Low holds the mutex and yields once, giving middle a chance to take over.
	CHECK(g_low.start([]
	{
		g_a.lock();
		g_low_ran = 1;
		opsy::sleep_for(10ms);
		record(1);
		g_a.unlock();
	}, "holder"));
	g_low.priority(opsy::task_priority::low);

	opsy::sleep_for(2ms);
	CHECK(g_low_ran == 1);

	// High blocks on the mutex: the holder inherits its priority.
	CHECK(g_high.start([] { g_a.lock(); record(3); g_a.unlock(); g_high_ran = 1; }, "waiter"));
	g_high.priority(opsy::task_priority::high);

	// Middle wants nothing but CPU. Without inheritance it would outrank the
	// holder and delay it — and therefore delay high, which outranks middle.
	CHECK(g_middle.start([]
	{
		while (!g_stop)
		{
			record(2);
			opsy::sleep_for(2ms);
		}
	}, "cpu-hog"));
	g_middle.priority(opsy::task_priority::normal);

	opsy::sleep_for(40ms);
	g_stop = true;
	opsy::sleep_for(10ms);

	CHECK(g_high_ran == 1);

	// The holder released before high ran, which is the point.
	int holder_at = -1, high_at = -1;
	for (int i = 0; i < g_order_index; ++i)
	{
		if (g_order[i] == 1 && holder_at < 0) holder_at = i;
		if (g_order[i] == 3 && high_at < 0) high_at = i;
	}
	CHECK(holder_at >= 0);
	CHECK(high_at > holder_at);

	stop_all();
}

// ─────────────────────── termination releases what it held ─────────────────

OPSY_QEMU_TEST(killing_a_holder_releases_its_mutexes_and_wakes_the_waiters)
{
	g_high_ran = 0;
	g_stop = false;

	CHECK(g_low.start([]
	{
		g_a.lock();
		g_b.lock();
		while (!g_stop)
			opsy::sleep_for(1ms);
	}, "holder"));
	g_low.priority(opsy::task_priority::low);

	opsy::sleep_for(5ms);
	CHECK(g_a.is_locked());
	CHECK(g_b.is_locked());

	CHECK(g_high.start([] { g_a.lock(); g_high_ran = 1; g_a.unlock(); }, "waiter"));
	g_high.priority(opsy::task_priority::high);
	CHECK(g_high_ran == 0);

	// kill() never unwinds, so without an explicit release both mutexes would
	// stay locked for good and the waiter would never run again.
	CHECK(g_low.kill());
	opsy::sleep_for(20ms);

	CHECK(g_high_ran == 1);
	CHECK(!g_b.is_locked());   // released too, even with nobody waiting on it

	stop_all();
}

} // namespace
