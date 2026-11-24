/**
 ******************************************************************************
 * @file    test_priority_hook.cpp
 * @brief   A separate image: hook::task_priority_changed reports every move of
 *          a task's effective priority, which current_priority() then reads.
 *
 *          A tracer follows a task's priority through this hook: it has to
 *          fire when a donation raises a holder, when the donation ends --
 *          the waiter got the mutex, or was killed -- and when a base
 *          priority moves the effective one; and not when a donation still
 *          outranks the base priority asked for.
 *
 *          Its own image because its opsy_hooks.hpp records the hook, and the
 *          main suite's image takes the hooks of whoever builds it.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>
#include <initializer_list>

namespace
{

using namespace std::chrono_literals;
using opsy::task_priority;

// The runner sits at task_priority::high, and every task here below it: a
// raised holder above the runner would starve it.
opsy::task<1024> g_holder;
opsy::task<1024> g_first;
opsy::task<1024> g_second;

opsy::mutex g_a;
opsy::mutex g_b;

std::atomic<int> g_step = 0;
std::atomic<int> g_holder_state = 0;

void forget_changes()
{
	opsy::priority_seen::count = 0;
}

/** @brief Whether the changes reported for @p task since the last forget_changes are @p expected, in order */
bool changes_of(const opsy::task_control_block& task, std::initializer_list<task_priority> expected)
{
	const auto count = opsy::priority_seen::count.load();
	if (count > opsy::priority_seen::capacity)
		return false;

	const auto* wanted = expected.begin();
	for (std::size_t index = 0; index < count; ++index)
	{
		const auto& change = opsy::priority_seen::changes[index];
		if (change.task != &task)
			continue;
		if (wanted == expected.end() || change.priority != *wanted)
			return false;
		++wanted;
	}
	return wanted == expected.end();
}

void wait_for_step(int step)
{
	while (g_step < step)
		opsy::sleep_for(1ms);
}

/** @brief Takes both mutexes, gives back g_a at step 1 and g_b at step 2 */
void hold_both()
{
	g_a.lock();
	g_b.lock();
	g_holder_state = 1;
	wait_for_step(1);
	g_a.unlock();
	wait_for_step(2);
	g_b.unlock();
	g_holder_state = 2;
}

void lock_a() { g_a.lock(); g_a.unlock(); }
void lock_b() { g_b.lock(); g_b.unlock(); }

void stop_all()
{
	g_step = 100;
	(void) g_holder.kill();
	(void) g_first.kill();
	(void) g_second.kill();
	for (int guard = 0; guard < 200 && (g_holder.is_started() || g_first.is_started() || g_second.is_started()); ++guard)
		opsy::sleep_for(1ms);
	g_step = 0;
	g_holder_state = 0;
}

/** @brief Starts the holder at @p priority and waits until it holds both mutexes */
bool start_holder(task_priority priority)
{
	if (!g_holder.start(hold_both))
		return false;
	g_holder.base_priority(priority);
	for (int guard = 0; guard < 200 && g_holder_state != 1; ++guard)
		opsy::sleep_for(1ms);
	return g_holder_state == 1;
}

/** @brief Starts @p waiter on @p body at @p priority, and lets it block */
bool start_waiter(opsy::task<1024>& waiter, void (*body)(), task_priority priority)
{
	if (!waiter.start(body))
		return false;
	waiter.base_priority(priority);
	opsy::sleep_for(5ms);
	return true;
}

} // namespace

OPSY_QEMU_TEST(a_donation_and_its_end_are_reported_with_the_effective_priority)
{
	CHECK(start_holder(task_priority::lowest));
	forget_changes();

	CHECK(start_waiter(g_first, lock_a, task_priority::normal));
	CHECK(changes_of(g_holder, {task_priority::normal}));

	g_step = 1;
	opsy::sleep_for(5ms);
	CHECK(changes_of(g_holder, {task_priority::normal, task_priority::lowest}));

	g_step = 2;
	opsy::sleep_for(5ms);
	CHECK(g_holder_state == 2);
	CHECK(changes_of(g_holder, {task_priority::normal, task_priority::lowest}));

	stop_all();
}

OPSY_QEMU_TEST(releasing_one_of_two_mutexes_reports_the_donation_still_owed)
{
	CHECK(start_holder(task_priority::lowest));
	forget_changes();

	CHECK(start_waiter(g_second, lock_b, task_priority::low));
	CHECK(start_waiter(g_first, lock_a, task_priority::normal));

	// g_a goes to its waiter; g_b's still donates low.
	g_step = 1;
	opsy::sleep_for(5ms);
	g_step = 2;
	opsy::sleep_for(5ms);
	CHECK(g_holder_state == 2);
	CHECK(changes_of(g_holder, {task_priority::low, task_priority::normal, task_priority::low, task_priority::lowest}));

	stop_all();
}

OPSY_QEMU_TEST(killing_the_donor_reports_the_holder_back_at_its_own_priority)
{
	CHECK(start_holder(task_priority::lowest));
	forget_changes();

	CHECK(start_waiter(g_first, lock_a, task_priority::normal));
	CHECK(g_first.kill());
	opsy::sleep_for(1ms);
	CHECK(changes_of(g_holder, {task_priority::normal, task_priority::lowest}));

	stop_all();
}

OPSY_QEMU_TEST(a_requested_priority_a_donation_outranks_is_not_reported)
{
	CHECK(start_holder(task_priority::lowest));
	forget_changes();

	CHECK(start_waiter(g_first, lock_a, task_priority::normal));

	// Effective stays normal: nothing to report. Then the donation ends, and
	// the holder drops to what it asked for meanwhile.
	g_holder.base_priority(task_priority::low);
	CHECK(changes_of(g_holder, {task_priority::normal}));

	g_step = 1;
	opsy::sleep_for(5ms);
	CHECK(changes_of(g_holder, {task_priority::normal, task_priority::low}));

	stop_all();
}

OPSY_QEMU_TEST(a_requested_priority_that_moves_the_effective_one_is_reported)
{
	CHECK(start_holder(task_priority::lowest));
	forget_changes();

	g_holder.base_priority(task_priority::low);
	g_holder.base_priority(task_priority::low);
	CHECK(changes_of(g_holder, {task_priority::low}));

	stop_all();
}
