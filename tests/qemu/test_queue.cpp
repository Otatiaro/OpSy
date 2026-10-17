/**
 ******************************************************************************
 * @file    test_queue.cpp
 * @brief   On-target tests for @c opsy::queue : order, a full queue, blocking
 *          on either end, an interrupt handler on either end, and elements
 *          that own something.
 *
 *          Each case has its queue of its own: a side is bound to the first
 *          context that uses it, so a queue an interrupt handler fed in one
 *          case cannot be fed by the runner in another.
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

opsy::task<1024> g_consumer;
opsy::task<1024> g_producer;

std::atomic<int> g_got = 0;
std::atomic<uint32_t> g_values[16]{};
std::atomic<int> g_state = 0;

void stop_all()
{
	(void) g_consumer.kill();
	(void) g_producer.kill();
	for (int guard = 0; guard < 200 && (g_consumer.is_started() || g_producer.is_started()); ++guard)
		opsy::sleep_for(1ms);
}

// An interrupt at the most urgent priority OpSy may be called from, running
// whatever the case installs.
unsigned g_irq = qemu_test::no_irq;
void (*g_isr_body)() = nullptr;

void queue_test_irq_handler()
{
	if (g_isr_body != nullptr)
		g_isr_body();
}

bool install_irq(void (*body)())
{
	if (g_irq == qemu_test::no_irq)
		g_irq = qemu_test::find_usable_irq();
	if (g_irq == qemu_test::no_irq)
		return false;

	g_isr_body = body;
	opsy::cortex_m::set_priority(g_irq, opsy::scheduler::os_callable_priority);
	opsy::cortex_m::set_isr_handler(g_irq, &queue_test_irq_handler);
	opsy::cortex_m::enable_interrupt(g_irq);
	return true;
}

/** @brief Raises the interrupt, and returns once its handler has run. */
void fire()
{
	qemu_test::raise(g_irq);
	opsy::sleep_for(1ms);
}

} // namespace

// ───────────────────────────────── basics ──────────────────────────────────

namespace { opsy::queue<uint32_t, 4> g_order_queue; }

OPSY_QEMU_TEST(elements_come_out_in_the_order_they_went_in)
{
	for (uint32_t round = 0; round < 3; ++round)     // around the ring more than once
	{
		CHECK(g_order_queue.try_push(round * 10 + 1) == opsy::conqueue_status::success);
		CHECK(g_order_queue.try_push(round * 10 + 2) == opsy::conqueue_status::success);
		CHECK(g_order_queue.try_push(round * 10 + 3) == opsy::conqueue_status::success);
		CHECK(g_order_queue.size() == 3);
		CHECK(g_order_queue.try_pop() == round * 10 + 1);
		CHECK(g_order_queue.try_pop() == round * 10 + 2);
		CHECK(g_order_queue.try_pop() == round * 10 + 3);
		CHECK(g_order_queue.empty());
		CHECK(g_order_queue.try_pop().error() == opsy::conqueue_status::empty);
	}
}

namespace { opsy::queue<uint32_t, 4> g_full_queue; }

OPSY_QEMU_TEST(a_full_queue_refuses_and_counts_it)
{
	for (uint32_t i = 0; i < 4; ++i)
		CHECK(g_full_queue.try_push(i) == opsy::conqueue_status::success);
	CHECK(g_full_queue.size() == 4);

	CHECK(g_full_queue.try_push(99u) == opsy::conqueue_status::full);
	CHECK(g_full_queue.try_emplace(98u) == opsy::conqueue_status::full);
	CHECK(g_full_queue.dropped() == 2);
	CHECK(g_full_queue.high_water() == 4);

	for (uint32_t i = 0; i < 4; ++i)
		CHECK(g_full_queue.try_pop() == i);          // the refused ones are not in it
	CHECK(g_full_queue.empty());
	CHECK(g_full_queue.high_water() == 4);           // remembered once emptied
}

namespace { opsy::queue<uint32_t, 8> g_water_queue; }

OPSY_QEMU_TEST(high_water_is_the_fullest_it_got)
{
	CHECK(g_water_queue.high_water() == 0);
	for (uint32_t i = 0; i < 3; ++i)
		CHECK(g_water_queue.try_push(i) == opsy::conqueue_status::success);
	for (int i = 0; i < 3; ++i)
		(void) g_water_queue.try_pop();
	CHECK(g_water_queue.try_push(7u) == opsy::conqueue_status::success);
	(void) g_water_queue.try_pop();
	CHECK(g_water_queue.high_water() == 3);
	CHECK(g_water_queue.dropped() == 0);
}

// ─────────────────────────────── blocking ──────────────────────────────────

namespace { opsy::queue<uint32_t, 4> g_pop_queue; }

OPSY_QEMU_TEST(pop_waits_for_the_producer)
{
	g_got = 0;
	CHECK(g_consumer.start([] { g_values[0] = *g_pop_queue.pop(); g_got = 1; }, "consumer"));
	g_consumer.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_got == 0);                 // nothing to take: waiting

	CHECK(g_pop_queue.try_push(42u) == opsy::conqueue_status::success);  // the consumer outranks the runner: runs here
	CHECK(g_got == 1);
	CHECK(g_values[0] == 42u);

	stop_all();
}

namespace { opsy::queue<uint32_t, 4> g_push_queue; }

OPSY_QEMU_TEST(push_waits_for_a_free_place)
{
	g_state = 0;
	CHECK(g_producer.start([]
	{
		for (uint32_t i = 0; i < 5; ++i)
			g_push_queue.push(i);    // the fifth waits
		g_state = 1;
	}, "producer"));
	g_producer.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_state == 0);
	CHECK(g_push_queue.size() == 4);

	CHECK(g_push_queue.try_pop() == 0u);   // a place frees: the producer runs here
	CHECK(g_state == 1);
	CHECK(g_push_queue.size() == 4);
	CHECK(g_push_queue.dropped() == 0);    // waiting is not refusing

	for (uint32_t i = 1; i < 5; ++i)
		CHECK(g_push_queue.try_pop() == i);

	stop_all();
}

namespace { opsy::queue<uint32_t, 2> g_timeout_queue; }

OPSY_QEMU_TEST(waits_that_run_out_report_it)
{
	const auto start = opsy::scheduler::now();
	CHECK(g_timeout_queue.pop_for(10ms).error() == opsy::conqueue_status::empty);
	CHECK(opsy::scheduler::now() - start >= 10ms);

	CHECK(g_timeout_queue.try_push(1u) == opsy::conqueue_status::success);
	CHECK(g_timeout_queue.try_push(2u) == opsy::conqueue_status::success);
	CHECK(g_timeout_queue.push_for(3u, 10ms) == opsy::conqueue_status::full);
	CHECK(g_timeout_queue.dropped() == 0);   // a wait that ran out is not a refusal
	CHECK(g_timeout_queue.size() == 2);

	CHECK(g_timeout_queue.pop_for(10ms) == 1u);
	CHECK(g_timeout_queue.pop_for(10ms) == 2u);
}

// ─────────────────────── an interrupt handler on one end ───────────────────

namespace
{
opsy::queue<uint32_t, 4> g_from_isr;
std::atomic<uint32_t> g_isr_next = 0;
}

OPSY_QEMU_TEST(an_interrupt_handler_feeds_a_waiting_task)
{
	g_got = 0;
	g_isr_next = 100;
	CHECK(install_irq([] { (void) g_from_isr.try_push(g_isr_next++); }));

	CHECK(g_consumer.start([]
	{
		for (int i = 0; i < 6; ++i)
		{
			g_values[i] = *g_from_isr.pop();
			g_got = i + 1;
		}
	}, "consumer"));
	g_consumer.priority(opsy::task_priority::highest);

	for (int i = 0; i < 6; ++i)
		fire();

	for (int guard = 0; guard < 50 && g_got != 6; ++guard)
		opsy::sleep_for(1ms);
	CHECK(g_got == 6);
	for (int i = 0; i < 6; ++i)
		CHECK(g_values[i] == 100u + static_cast<uint32_t>(i));
	CHECK(g_from_isr.dropped() == 0);

	g_isr_body = nullptr;
	stop_all();
}

namespace { opsy::queue<uint32_t, 4> g_burst; }

OPSY_QEMU_TEST(a_burst_beyond_capacity_from_an_interrupt_handler_is_counted)
{
	// Ten in one handler run, into four places, with nobody draining it.
	CHECK(install_irq([] { for (uint32_t i = 0; i < 10; ++i) (void) g_burst.try_push(i); }));
	fire();

	CHECK(g_burst.size() == 4);
	CHECK(g_burst.dropped() == 6);
	CHECK(g_burst.high_water() == 4);
	for (uint32_t i = 0; i < 4; ++i)
		CHECK(g_burst.try_pop() == i);    // the first four, in order

	g_isr_body = nullptr;
}

namespace
{
opsy::queue<uint32_t, 4> g_to_isr;
std::atomic<int> g_isr_got = 0;
std::atomic<uint32_t> g_isr_values[8]{};
}

OPSY_QEMU_TEST(a_task_feeds_an_interrupt_handler)
{
	g_isr_got = 0;
	CHECK(install_irq([]
	{
		while (auto value = g_to_isr.try_pop())
			g_isr_values[g_isr_got++] = *value;
	}));

	CHECK(g_to_isr.try_push(7u) == opsy::conqueue_status::success);
	CHECK(g_to_isr.try_push(8u) == opsy::conqueue_status::success);
	fire();
	CHECK(g_to_isr.try_push(9u) == opsy::conqueue_status::success);
	fire();

	CHECK(g_isr_got == 3);
	CHECK(g_isr_values[0] == 7u);
	CHECK(g_isr_values[1] == 8u);
	CHECK(g_isr_values[2] == 9u);
	CHECK(g_to_isr.empty());

	g_isr_body = nullptr;
}

// ───────────────────────── elements that own something ─────────────────────

namespace
{

std::atomic<int> g_alive = 0;

struct owner
{
	explicit owner(uint32_t v) noexcept : value(v) { ++g_alive; }
	owner(owner&& other) noexcept : value(other.value) { ++g_alive; }
	owner(const owner&) = delete;
	owner& operator=(const owner&) = delete;
	owner& operator=(owner&&) = delete;
	~owner() { --g_alive; }

	uint32_t value;
};

}

OPSY_QEMU_TEST(elements_are_moved_in_moved_out_and_destroyed)
{
	g_alive = 0;
	{
		opsy::queue<owner, 4> local;
		CHECK(local.try_emplace(1u) == opsy::conqueue_status::success);
		CHECK(local.try_push(owner{ 2u }) == opsy::conqueue_status::success);
		CHECK(local.try_emplace(3u) == opsy::conqueue_status::success);
		CHECK(g_alive == 3);                 // the temporary is gone, the three stored remain

		{
			auto first = local.try_pop();
			CHECK(first.has_value() && first->value == 1u);
			CHECK(g_alive == 3);             // moved out: one in hand, two stored
		}
		CHECK(g_alive == 2);
	}
	CHECK(g_alive == 0);                     // what was left is destroyed with the queue
}

namespace
{
opsy::queue<opsy::callback<void()>, 4> g_work;
std::atomic<int> g_jobs_done = 0;
}

OPSY_QEMU_TEST(work_posted_by_an_interrupt_handler_runs_in_a_task)
{
	g_jobs_done = 0;
	CHECK(install_irq([] { (void) g_work.try_push([] { ++g_jobs_done; }); }));

	CHECK(g_consumer.start([]
	{
		for (int i = 0; i < 3; ++i)
			(*g_work.pop())();
	}, "worker"));
	g_consumer.priority(opsy::task_priority::highest);

	fire();
	fire();
	fire();

	for (int guard = 0; guard < 50 && g_jobs_done != 3; ++guard)
		opsy::sleep_for(1ms);
	CHECK(g_jobs_done == 3);

	g_isr_body = nullptr;
	stop_all();
}

// ───────────────────────────────── close ───────────────────────────────────

namespace { opsy::queue<uint32_t, 4> g_closed_queue; }

OPSY_QEMU_TEST(a_closed_queue_refuses_pushes_and_hands_out_what_it_still_holds)
{
	CHECK(!g_closed_queue.is_closed());
	CHECK(g_closed_queue.try_push(1u) == opsy::conqueue_status::success);
	CHECK(g_closed_queue.try_push(2u) == opsy::conqueue_status::success);

	g_closed_queue.close();
	CHECK(g_closed_queue.is_closed());
	CHECK(g_closed_queue.try_push(3u) == opsy::conqueue_status::closed);
	CHECK(!g_closed_queue.push(4u));
	CHECK(g_closed_queue.dropped() == 0);           // a closed queue does not count as full

	CHECK(g_closed_queue.try_pop() == 1u);          // what was in it is still popped
	CHECK(g_closed_queue.pop() == 2u);
	CHECK(g_closed_queue.try_pop().error() == opsy::conqueue_status::closed);
	CHECK(!g_closed_queue.pop().has_value());       // closed and empty: does not wait
	g_closed_queue.close();                         // and closing twice is harmless
}

namespace { opsy::queue<uint32_t, 4> g_close_wakes_consumer; }

OPSY_QEMU_TEST(closing_wakes_a_consumer_waiting_on_an_empty_queue)
{
	g_state = 0;
	CHECK(g_consumer.start([] { g_state = g_close_wakes_consumer.pop().has_value() ? 1 : 2; }, "consumer"));
	g_consumer.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_state == 0);                  // waiting

	g_close_wakes_consumer.close();       // the consumer outranks the runner: runs here
	CHECK(g_state == 2);                  // woken with nothing

	stop_all();
}

namespace { opsy::queue<uint32_t, 2> g_close_wakes_producer; }

OPSY_QEMU_TEST(closing_wakes_a_producer_waiting_on_a_full_queue)
{
	g_state = 0;
	CHECK(g_producer.start([]
	{
		(void) g_close_wakes_producer.push(1u);
		(void) g_close_wakes_producer.push(2u);
		g_state = g_close_wakes_producer.push(3u) ? 1 : 2;   // waits for a place
	}, "producer"));
	g_producer.priority(opsy::task_priority::highest);

	opsy::sleep_for(5ms);
	CHECK(g_state == 0);
	CHECK(g_close_wakes_producer.size() == 2);

	g_close_wakes_producer.close();
	CHECK(g_state == 2);                  // gave up, stored nothing
	CHECK(g_close_wakes_producer.size() == 2);

	stop_all();
}
