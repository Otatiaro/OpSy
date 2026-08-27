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

/** @brief A deliberate delay that does not yield, so priority decides who runs. */
void busy_wait(int rounds)
{
	for (int i = 0; i < rounds; ++i)
		opsy::cortex_m::nop();
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


// ──────────────── priority inheritance: five corrected defects ─────────────
// Five defects that the cases above do not reach. Each needs a priority
// change while a mutex holder is running at an inherited priority, or a task
// killed at a particular moment, so nothing simpler exposes them.
//
// A test for a fixed defect is only worth what it catches, so each of the
// five was run against the code as it stood before the correction, at commit
// 3e7ee7b. Two of them fail there, and so genuinely pin their defect:
//
//   a_priority_change_is_not_lost_while_the_task_is_boosted
//   killing_a_holder_runs_the_woken_waiter_immediately
//
// The other three pass on both versions. They state the intended behaviour
// and will catch a future change that breaks it outright, but they do not
// reproduce the defect they were written for: that one shows up as a
// difference in scheduling order, and only when the holder is running on the
// CPU at the exact instant its priority changes -- a moment neither busy
// waiting nor sleeping reaches reliably under emulation. So a green run on
// those three is not evidence that the behaviour underneath is correct.
// What that rests on is the reasoning in docs/architecture.md.

// priority() used to compare the requested value against the *effective* one,
// so a genuine change was silently dropped while the task was boosted.
OPSY_QEMU_TEST(a_priority_change_is_not_lost_while_the_task_is_boosted)
{
	g_low_ran = 0;
	g_stop = false;

	CHECK(g_low.start([]
	{
		g_a.lock();
		g_low_ran = 1;
		while (!g_stop)
			opsy::sleep_for(1ms);
		g_a.unlock();
	}, "holder"));
	g_low.priority(opsy::task_priority::low);

	opsy::sleep_for(5ms);
	CHECK(g_low_ran == 1);

	// Boost it: a highest-priority task blocks on the mutex it holds.
	CHECK(g_high.start([] { g_a.lock(); g_a.unlock(); }, "waiter"));
	g_high.priority(opsy::task_priority::highest);
	opsy::sleep_for(5ms);

	// Now request exactly the priority it is *effectively* running at. The
	// early-out used to see "no change" and drop it on the floor.
	g_low.priority(opsy::task_priority::highest);
	CHECK(g_low.priority() == opsy::task_priority::highest);

	g_stop = true;
	opsy::sleep_for(20ms);

	// And it survives the release, which resets to the base priority.
	CHECK(g_low.priority() == opsy::task_priority::highest);

	stop_all();
}

// update_priority used to assign the requested priority straight onto the
// effective one, throwing away an inheritance and re-opening the inversion.
OPSY_QEMU_TEST(lowering_a_boosted_holder_does_not_drop_its_inherited_priority)
{
	g_low_ran = 0;
	g_middle_ran = 0;
	g_high_ran = 0;
	g_order_index = 0;
	g_stop = false;

	CHECK(g_low.start([]
	{
		g_a.lock();
		g_low_ran = 1;
		// Busy, not asleep: a holder that yields hands the CPU over whatever
		// its priority is, and the test would pass with or without the
		// donation. It has to be the scheduler that decides.
		busy_wait(30000);
		record(1);
		g_a.unlock();
	}, "holder"));
	g_low.priority(opsy::task_priority::low);
	opsy::sleep_for(2ms);
	CHECK(g_low_ran == 1);

	CHECK(g_high.start([] { g_a.lock(); record(3); g_a.unlock(); g_high_ran = 1; }, "waiter"));
	g_high.priority(opsy::task_priority::high);

	// A third party re-asserts the holder's base priority — intended as a
	// no-op. It must not cancel the donation it is currently owed.
	g_low.priority(opsy::task_priority::low);

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

// update_priority's ready_ branch did not know about blocked_on_, so changing
// the priority of a mutex-blocked task made it runnable without the mutex.
OPSY_QEMU_TEST(changing_the_priority_of_a_blocked_task_leaves_it_blocked)
{
	g_high_ran = 0;
	g_a.lock();

	CHECK(g_high.start([] { g_a.lock(); g_high_ran = 1; g_a.unlock(); }, "blocked"));
	g_high.priority(opsy::task_priority::normal);
	CHECK(g_high_ran == 0);

	// Re-prioritising it must not schedule it: it does not own the mutex.
	g_high.priority(opsy::task_priority::highest);
	opsy::sleep_for(10ms);
	CHECK(g_high_ran == 0);
	CHECK(g_a.is_locked());

	g_a.unlock();
	opsy::sleep_for(10ms);
	CHECK(g_high_ran == 1);

	stop_all();
}

// Killing a holder woke the waiters but never asked for a switch, so a
// higher-priority waiter sat until some unrelated switch point came along.
OPSY_QEMU_TEST(killing_a_holder_runs_the_woken_waiter_immediately)
{
	g_high_ran = 0;
	g_stop = false;

	CHECK(g_low.start([]
	{
		g_a.lock();
		while (!g_stop)
			opsy::sleep_for(1ms);
	}, "holder"));
	g_low.priority(opsy::task_priority::low);
	opsy::sleep_for(5ms);

	CHECK(g_high.start([] { g_a.lock(); g_high_ran = 1; g_a.unlock(); }, "waiter"));
	g_high.priority(opsy::task_priority::highest);
	CHECK(g_high_ran == 0);

	// No sleep after this: the waiter outranks us, so it must run before
	// kill() returns control here.
	CHECK(g_low.kill());
	CHECK(g_high_ran == 1);

	stop_all();
}

// Killing a *waiter* left its donation on the holder, which kept running
// raised for the rest of its hold.
OPSY_QEMU_TEST(killing_a_waiter_returns_the_holder_to_its_own_priority)
{
	g_low_ran = 0;
	g_middle_ran = 0;
	g_stop = false;

	CHECK(g_low.start([]
	{
		g_a.lock();
		g_low_ran = 1;
		// Busy rather than asleep: only then does the effective priority
		// decide who runs, which is the whole point of the check below.
		while (!g_stop)
			busy_wait(200);
		g_a.unlock();
	}, "holder"));
	g_low.priority(opsy::task_priority::low);
	opsy::sleep_for(5ms);
	CHECK(g_low_ran == 1);

	// Boost the holder, then kill the task doing the boosting.
	CHECK(g_high.start([] { g_a.lock(); g_a.unlock(); }, "waiter"));
	g_high.priority(opsy::task_priority::highest);
	opsy::sleep_for(5ms);
	CHECK(g_high.kill());

	// With the donation gone, a normal-priority task must be able to preempt
	// the holder again — it is back to `low`.
	CHECK(g_middle.start([]
	{
		while (!g_stop)
		{
			g_middle_ran = 1;
			opsy::sleep_for(1ms);
		}
	}, "normal"));
	g_middle.priority(opsy::task_priority::normal);

	opsy::sleep_for(20ms);
	CHECK(g_middle_ran == 1);

	g_stop = true;
	stop_all();
}

} // namespace
