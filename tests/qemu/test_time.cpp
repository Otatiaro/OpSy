/**
 ******************************************************************************
 * @file    test_time.cpp
 * @brief   On-target tests for the OpSy clock and the sleep primitives.
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

opsy::task<1024> g_reader;

std::atomic<bool> g_stop        = false;
std::atomic<int>  g_reads       = 0;
std::atomic<int>  g_regressions = 0;   // times now() went backwards

void stop_all()
{
	g_stop = true;
	(void) g_reader.stop();
	for (int guard = 0; guard < 200 && g_reader.is_started(); ++guard)
		opsy::sleep_for(1ms);
	g_stop = false;
}

OPSY_QEMU_TEST(now_advances_and_never_goes_backwards)
{
	auto previous = opsy::scheduler::now();

	for (int round = 0; round < 200; ++round)
	{
		const auto current = opsy::scheduler::now();
		CHECK(current >= previous);
		previous = current;
	}

	opsy::sleep_for(5ms);
	CHECK(opsy::scheduler::now() > previous);
}

/**
 * @brief now() read continuously from two tasks while SysTick ticks under them.
 *
 *        now() reads a 64-bit counter that SysTick writes, which is two loads
 *        on Cortex-M; the read masks SysTick to keep the halves consistent.
 *
 * @warning The half that actually tears is the 32-bit low word wrapping, which
 *          needs ~49.7 days of ticks to reach — so this checks monotonicity
 *          under preemption, not the wrap itself. tests/README.md says so.
 */
OPSY_QEMU_TEST(now_stays_monotonic_while_another_task_reads_it)
{
	g_stop        = false;
	g_reads       = 0;
	g_regressions = 0;

	CHECK(g_reader.start([]
	{
		auto previous = opsy::scheduler::now();
		while (!g_stop)
		{
			// Bounded bursts rather than one tight spin: now() masks SysTick
			// around the read, so it issues an ISB every call, and QEMU ends
			// its translation block on each one. A free-running loop makes the
			// emulator crawl badly enough to look like a hang.
			for (int burst = 0; burst < 32 && !g_stop; ++burst)
			{
				const auto current = opsy::scheduler::now();
				if (current < previous)
					++g_regressions;
				previous = current;
				++g_reads;
			}
			opsy::sleep_for(1ms);
		}
	}, "clock-reader"));
	g_reader.priority(opsy::task_priority::normal);

	const auto deadline = opsy::scheduler::now() + 30ms;
	auto previous = opsy::scheduler::now();
	while (opsy::scheduler::now() < deadline)
	{
		const auto current = opsy::scheduler::now();
		CHECK(current >= previous);
		previous = current;
		opsy::sleep_for(1ms);
	}

	g_stop = true;
	opsy::sleep_for(5ms);

	CHECK(g_reads > 100);        // the reader really spun
	CHECK(g_regressions == 0);   // and never saw time move backwards

	stop_all();
}

OPSY_QEMU_TEST(sleep_for_waits_at_least_the_requested_time)
{
	for (const auto requested : { 1ms, 5ms, 20ms })
	{
		const auto before = opsy::scheduler::now();
		opsy::sleep_for(requested);
		const auto elapsed = opsy::scheduler::now() - before;

		CHECK(elapsed >= requested);
		CHECK(elapsed < requested + 50ms);   // and is not wildly over
	}
}

OPSY_QEMU_TEST(sleep_for_zero_returns_without_hanging)
{
	// Zero is the documented way to yield: it costs a tick, but must come back.
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(0ms);
	CHECK(opsy::scheduler::now() - before < 50ms);
}

OPSY_QEMU_TEST(sleep_for_a_negative_duration_returns_immediately)
{
	// It reaches sleep_for from sleep_until with an elapsed deadline; the
	// service call would otherwise trip assert(delta.count() >= 0).
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(-5ms);
	CHECK(opsy::scheduler::now() - before < 5ms);
}

OPSY_QEMU_TEST(sleep_until_a_future_deadline_lands_on_it)
{
	const auto deadline = opsy::scheduler::now() + 20ms;
	opsy::sleep_until(deadline);

	CHECK(opsy::scheduler::now() >= deadline);
	CHECK(opsy::scheduler::now() - deadline < 50ms);
}

OPSY_QEMU_TEST(sleep_until_an_elapsed_deadline_returns_immediately)
{
	const auto before = opsy::scheduler::now();
	opsy::sleep_until(before - 50ms);
	CHECK(opsy::scheduler::now() - before < 5ms);
}

OPSY_QEMU_TEST(repeated_sleep_until_keeps_a_steady_cadence)
{
	// The pattern a periodic task uses: absolute deadlines, so the period does
	// not drift with the time spent working.
	auto next = opsy::scheduler::now() + 5ms;
	const auto start = opsy::scheduler::now();

	for (int period = 0; period < 6; ++period)
	{
		opsy::sleep_until(next);
		CHECK(opsy::scheduler::now() >= next);
		next += 5ms;
	}

	const auto total = opsy::scheduler::now() - start;
	CHECK(total >= 30ms);
	CHECK(total < 130ms);   // no cumulative drift
}

OPSY_QEMU_TEST(the_clock_agrees_with_the_chrono_interface)
{
	const auto direct = opsy::scheduler::now();
	const auto through_clock = opsy::opsy_clock::now();

	CHECK(through_clock >= direct);
	CHECK(through_clock - direct < 5ms);
}

} // namespace
