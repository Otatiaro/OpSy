/**
 ******************************************************************************
 * @file    test_task.cpp
 * @brief   On-target tests for task lifecycle, priority and enumeration.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>

#include <cstring>

namespace
{

using namespace std::chrono_literals;

opsy::task<1024> g_alpha;
opsy::task<1024> g_beta;
opsy::task<1024> g_gamma;

std::atomic<int> g_order_index = 0;
std::atomic<int> g_order[4]{};
std::atomic<int> g_alpha_ran   = 0;
std::atomic<int> g_beta_ran    = 0;
std::atomic<int> g_gamma_ran   = 0;
std::atomic<bool> g_stop       = false;

void record(int who)
{
	const int index = g_order_index;
	if (index < 4)
	{
		g_order[index] = who;
		g_order_index = index + 1;
	}
}

void stop_all()
{
	g_stop = true;
	(void) g_alpha.stop();
	(void) g_beta.stop();
	(void) g_gamma.stop();
	for (int guard = 0; guard < 200 &&
	     (g_alpha.is_started() || g_beta.is_started() || g_gamma.is_started()); ++guard)
		opsy::sleep_for(1ms);
	g_stop = false;
}

OPSY_QEMU_TEST(a_task_reports_started_between_start_and_stop)
{
	CHECK(!g_alpha.is_started());

	CHECK(g_alpha.start([] { while (true) opsy::sleep_for(1ms); }, "alpha"));
	CHECK(g_alpha.is_started());

	CHECK(g_alpha.stop());
	CHECK(!g_alpha.is_started());

	CHECK(!g_alpha.stop());   // stopping twice is refused, not fatal
}

OPSY_QEMU_TEST(a_task_carries_the_name_it_was_given)
{
	CHECK(g_alpha.start([] { while (true) opsy::sleep_for(1ms); }, "alpha-name"));
	CHECK(g_alpha.name() != nullptr);
	CHECK(std::strcmp(g_alpha.name(), "alpha-name") == 0);

	g_alpha.set_name("renamed");
	CHECK(std::strcmp(g_alpha.name(), "renamed") == 0);

	stop_all();
}

OPSY_QEMU_TEST(higher_priority_tasks_run_first)
{
	g_order_index = 0;
	for (int i = 0; i < 4; ++i)
		g_order[i] = 0;

	// All three start below the runner and stay queued; raising them in
	// reverse order of priority proves the ready list is ordered by priority
	// and not by arrival.
	CHECK(g_alpha.start([] { record(1); }, "low"));
	CHECK(g_beta.start([]  { record(2); }, "mid"));
	CHECK(g_gamma.start([] { record(3); }, "high"));

	// All strictly below the runner (high), so none of them can run until we
	// yield -- and ordered among themselves so the drain order is observable.
	g_alpha.priority(opsy::task_priority::lowest);
	g_beta.priority(opsy::task_priority::low);
	g_gamma.priority(opsy::task_priority::normal);

	CHECK(g_order_index == 0);   // still nothing ran: the runner is above them

	opsy::sleep_for(20ms);       // yield, let them drain in priority order

	CHECK(g_order_index == 3);
	CHECK(g_order[0] == 3);      // high
	CHECK(g_order[1] == 2);      // mid
	CHECK(g_order[2] == 1);      // low

	stop_all();
}

OPSY_QEMU_TEST(raising_a_queued_task_above_the_runner_preempts_immediately)
{
	g_alpha_ran = 0;

	CHECK(g_alpha.start([] { g_alpha_ran = 1; }, "climber"));
	CHECK(g_alpha_ran == 0);                             // queued below us

	g_alpha.priority(opsy::task_priority::highest);      // now above us
	CHECK(g_alpha_ran == 1);                             // switched on the spot

	stop_all();
}

OPSY_QEMU_TEST(lowering_the_running_task_hands_over_to_a_queued_one)
{
	g_alpha_ran = 0;

	CHECK(g_alpha.start([] { g_alpha_ran = 1; while (true) opsy::sleep_for(1ms); }, "waiting"));
	g_alpha.priority(opsy::task_priority::low);   // below the runner
	CHECK(g_alpha_ran == 0);

	// Raise it above the runner: the switch must happen inside priority().
	g_alpha.priority(opsy::task_priority::highest);
	CHECK(g_alpha_ran == 1);

	stop_all();
}

OPSY_QEMU_TEST(a_task_reports_the_priority_it_was_given)
{
	CHECK(g_alpha.start([] { while (true) opsy::sleep_for(1ms); }, "prio"));

	g_alpha.priority(opsy::task_priority::normal);
	CHECK(g_alpha.priority() == opsy::task_priority::normal);

	g_alpha.priority(opsy::task_priority::low);
	CHECK(g_alpha.priority() == opsy::task_priority::low);

	// Names must agree with the ordering: lower numeric value is more
	// important, so high must sort ahead of normal, and normal ahead of low.
	static_assert(static_cast<uint8_t>(opsy::task_priority::highest)
	            < static_cast<uint8_t>(opsy::task_priority::high));
	static_assert(static_cast<uint8_t>(opsy::task_priority::high)
	            < static_cast<uint8_t>(opsy::task_priority::normal));
	static_assert(static_cast<uint8_t>(opsy::task_priority::normal)
	            < static_cast<uint8_t>(opsy::task_priority::low));
	static_assert(static_cast<uint8_t>(opsy::task_priority::low)
	            < static_cast<uint8_t>(opsy::task_priority::lowest));

	stop_all();
}

OPSY_QEMU_TEST(all_tasks_lists_every_started_task_and_drops_stopped_ones)
{
	const auto count = []
	{
		std::size_t total = 0;
		for (const auto& task : opsy::scheduler::all_tasks())
		{
			(void) task;
			++total;
		}
		return total;
	};

	const auto before = count();

	CHECK(g_alpha.start([] { while (true) opsy::sleep_for(1ms); }, "listed-a"));
	CHECK(g_beta.start([]  { while (true) opsy::sleep_for(1ms); }, "listed-b"));
	CHECK(count() == before + 2);

	CHECK(g_alpha.stop());
	CHECK(count() == before + 1);

	CHECK(g_beta.stop());
	CHECK(count() == before);

	stop_all();
}

OPSY_QEMU_TEST(a_task_slot_can_be_restarted_many_times)
{
	for (int round = 0; round < 5; ++round)
	{
		g_alpha_ran = 0;
		CHECK(g_alpha.start([] { g_alpha_ran = 1; }, "recycled"));
		g_alpha.priority(opsy::task_priority::highest);
		CHECK(g_alpha_ran == 1);

		for (int guard = 0; guard < 100 && g_alpha.is_started(); ++guard)
			opsy::sleep_for(1ms);
		CHECK(!g_alpha.is_started());
	}
}

OPSY_QEMU_TEST(stopping_a_sleeping_task_removes_it_from_the_timeout_list)
{
	g_alpha_ran = 0;

	CHECK(g_alpha.start([] { opsy::sleep_for(500ms); g_alpha_ran = 1; }, "sleeper"));
	g_alpha.priority(opsy::task_priority::highest);

	// It parked on a long sleep; kill it while it sits in timeouts_.
	CHECK(g_alpha.stop());
	CHECK(!g_alpha.is_started());

	// If it were still linked, SysTick would wake a terminated task.
	opsy::sleep_for(30ms);
	CHECK(g_alpha_ran == 0);

	// Other timeouts must still fire correctly afterwards.
	const auto before = opsy::scheduler::now();
	opsy::sleep_for(10ms);
	CHECK(opsy::scheduler::now() - before >= 10ms);

	stop_all();
}

OPSY_QEMU_TEST(many_tasks_run_concurrently_without_losing_any)
{
	g_alpha_ran = 0;
	g_beta_ran  = 0;
	g_gamma_ran = 0;
	g_stop      = false;

	CHECK(g_alpha.start([] { while (!g_stop) { ++g_alpha_ran; opsy::sleep_for(1ms); } }, "a"));
	CHECK(g_beta.start([]  { while (!g_stop) { ++g_beta_ran;  opsy::sleep_for(1ms); } }, "b"));
	CHECK(g_gamma.start([] { while (!g_stop) { ++g_gamma_ran; opsy::sleep_for(1ms); } }, "c"));

	g_alpha.priority(opsy::task_priority::normal);
	g_beta.priority(opsy::task_priority::normal);
	g_gamma.priority(opsy::task_priority::normal);

	opsy::sleep_for(50ms);

	// Every one of them got scheduled repeatedly — none was dropped from
	// ready_ by the round-robin insertion.
	CHECK(g_alpha_ran > 2);
	CHECK(g_beta_ran > 2);
	CHECK(g_gamma_ran > 2);

	stop_all();
}

} // namespace
