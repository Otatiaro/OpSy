/**
 ******************************************************************************
 * @file    test_semaphore.cpp
 * @brief   On-target tests for @c opsy::counting_semaphore : counting, blocking,
 *          release from an interrupt handler, timeouts and the order waiters
 *          are released in.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>
#include <cstdint>

namespace
{

using namespace std::chrono_literals;

opsy::task<1024> g_first;
opsy::task<1024> g_second;
opsy::task<1024> g_third;

opsy::counting_semaphore<8> g_semaphore{ 0 };

std::atomic<int> g_order_index = 0;
std::atomic<int> g_order[4]{};
std::atomic<int> g_acquired = 0;

void record(int who)
{
	const int index = g_order_index;
	if (index < 4)
	{
		g_order[index] = who;
		g_order_index = index + 1;
	}
}

void reset()
{
	g_order_index = 0;
	g_acquired = 0;
	while (g_semaphore.try_acquire()) {}
}

void stop_all()
{
	(void) g_first.kill();
	(void) g_second.kill();
	(void) g_third.kill();
	for (int guard = 0; guard < 200 &&
	     (g_first.is_started() || g_second.is_started() || g_third.is_started()); ++guard)
		opsy::sleep_for(1ms);
	while (g_semaphore.try_acquire()) {}
}

// An interrupt that releases the semaphore, at the most urgent priority
// OpSy may be called from.
uint32_t g_irq = qemu_test::no_irq;

void semaphore_test_irq_handler()
{
	g_semaphore.release();
}

bool install_irq()
{
	if (g_irq == qemu_test::no_irq)
		g_irq = qemu_test::find_usable_irq();
	if (g_irq == qemu_test::no_irq)
		return false;

	opsy::cortex_m::set_priority(g_irq, opsy::scheduler::os_callable_priority);
	opsy::cortex_m::set_isr_handler(g_irq, &semaphore_test_irq_handler);
	opsy::cortex_m::enable_interrupt(g_irq);
	return true;
}

} // namespace

OPSY_QEMU_TEST(a_semaphore_holds_its_initial_count)
{
	opsy::counting_semaphore<4> semaphore{ 3 };
	CHECK(semaphore.try_acquire());
	CHECK(semaphore.try_acquire());
	CHECK(semaphore.try_acquire());
	CHECK(!semaphore.try_acquire());
	CHECK(opsy::counting_semaphore<4>::max() == 4);
}

OPSY_QEMU_TEST(a_release_lets_exactly_that_many_through)
{
	reset();
	g_semaphore.release(2);
	CHECK(g_semaphore.try_acquire());
	CHECK(g_semaphore.try_acquire());
	CHECK(!g_semaphore.try_acquire());
}

OPSY_QEMU_TEST(acquire_on_a_count_that_is_there_does_not_block)
{
	reset();
	g_semaphore.release();
	g_semaphore.acquire();          // the runner itself: returning is the check
	CHECK(!g_semaphore.try_acquire());
}

OPSY_QEMU_TEST(acquire_blocks_until_another_task_releases)
{
	reset();

	CHECK(g_first.start([] { g_semaphore.acquire(); g_acquired = 1; }, "waiter"));
	g_first.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_acquired == 0);         // nothing to take: blocked

	g_semaphore.release();          // the waiter outranks the runner: it runs here
	CHECK(g_acquired == 1);
	CHECK(!g_semaphore.try_acquire());   // and it took the count

	stop_all();
}

OPSY_QEMU_TEST(an_interrupt_handler_releasing_wakes_a_waiting_task)
{
	reset();
	CHECK(install_irq());

	CHECK(g_first.start([] { g_semaphore.acquire(); g_acquired = 1; }, "waiter"));
	g_first.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_acquired == 0);

	qemu_test::raise(g_irq);
	for (int guard = 0; guard < 50 && g_acquired == 0; ++guard)
		opsy::sleep_for(1ms);
	CHECK(g_acquired == 1);
	CHECK(!g_semaphore.try_acquire());

	stop_all();
}

OPSY_QEMU_TEST(an_interrupt_handler_releasing_with_nobody_waiting_leaves_the_count)
{
	reset();
	CHECK(install_irq());

	qemu_test::raise(g_irq);
	qemu_test::raise(g_irq);   // pending is a flag: may coalesce into one
	opsy::sleep_for(2ms);
	CHECK(g_semaphore.try_acquire());

	while (g_semaphore.try_acquire()) {}
}

OPSY_QEMU_TEST(try_acquire_for_times_out_when_nothing_is_released)
{
	reset();
	const auto start = opsy::scheduler::now();
	CHECK(!g_semaphore.try_acquire_for(10ms));
	CHECK(opsy::scheduler::now() - start >= 10ms);
}

OPSY_QEMU_TEST(try_acquire_for_takes_a_release_that_comes_in_time)
{
	reset();

	CHECK(g_first.start([] { g_acquired = g_semaphore.try_acquire_for(200ms) ? 1 : 2; }, "waiter"));
	g_first.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_acquired == 0);
	g_semaphore.release();
	CHECK(g_acquired == 1);

	stop_all();
}

OPSY_QEMU_TEST(waiters_are_released_most_important_first)
{
	reset();

	// Both below the runner, so neither runs until it sleeps: what decides
	// which one goes first is the semaphore's order, not who woke first.
	CHECK(g_first.start([] { g_semaphore.acquire(); record(1); }, "low"));
	g_first.priority(opsy::task_priority::low);
	CHECK(g_second.start([] { g_semaphore.acquire(); record(2); }, "normal"));
	g_second.priority(opsy::task_priority::normal);

	opsy::sleep_for(5ms);
	CHECK(g_order_index == 0);

	g_semaphore.release();
	opsy::sleep_for(5ms);
	CHECK(g_order_index == 1);
	CHECK(g_order[0] == 2);

	g_semaphore.release();
	opsy::sleep_for(5ms);
	CHECK(g_order_index == 2);
	CHECK(g_order[1] == 1);

	stop_all();
}

OPSY_QEMU_TEST(a_release_of_several_lets_as_many_waiters_through)
{
	reset();

	CHECK(g_first.start([] { g_semaphore.acquire(); record(1); }, "a"));
	g_first.priority(opsy::task_priority::low);
	CHECK(g_second.start([] { g_semaphore.acquire(); record(2); }, "b"));
	g_second.priority(opsy::task_priority::low);
	CHECK(g_third.start([] { g_semaphore.acquire(); record(3); }, "c"));
	g_third.priority(opsy::task_priority::low);

	opsy::sleep_for(5ms);
	CHECK(g_order_index == 0);

	g_semaphore.release(2);
	opsy::sleep_for(5ms);
	CHECK(g_order_index == 2);     // two through, one still waiting
	CHECK(!g_semaphore.try_acquire());

	g_semaphore.release();
	opsy::sleep_for(5ms);
	CHECK(g_order_index == 3);

	stop_all();
}

OPSY_QEMU_TEST(a_killed_waiter_does_not_consume_a_release)
{
	reset();

	CHECK(g_first.start([] { g_semaphore.acquire(); g_acquired = 1; }, "waiter"));
	g_first.priority(opsy::task_priority::normal);

	opsy::sleep_for(5ms);
	CHECK(g_first.kill());
	CHECK(!g_first.is_started());

	g_semaphore.release();
	CHECK(g_acquired == 0);
	CHECK(g_semaphore.try_acquire());   // the count is still there

	stop_all();
}

OPSY_QEMU_TEST(a_waiter_raised_while_waiting_is_released_first)
{
	reset();

	CHECK(g_first.start([] { g_semaphore.acquire(); record(1); }, "raised"));
	g_first.priority(opsy::task_priority::lowest);
	CHECK(g_second.start([] { g_semaphore.acquire(); record(2); }, "other"));
	g_second.priority(opsy::task_priority::low);

	opsy::sleep_for(5ms);
	g_first.priority(opsy::task_priority::normal);   // now ahead of the other

	g_semaphore.release();
	opsy::sleep_for(5ms);
	CHECK(g_order_index == 1);
	CHECK(g_order[0] == 1);

	g_semaphore.release();
	opsy::sleep_for(5ms);
	stop_all();
}
