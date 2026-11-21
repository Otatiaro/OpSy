/**
 ******************************************************************************
 * @file    test_before_start.cpp
 * @brief   A separate image: what a program does to its tasks between
 *          creating them and scheduler::start must work, in every build.
 *
 *          A program starts its tasks, then sets them up -- a priority, a
 *          name -- and only then starts the scheduler. The tasks are in
 *          ready_ by then, so a priority change re-sorts them there, and a
 *          task brought to the front asks for a switch. Before the scheduler
 *          runs there is nothing to switch from, and nothing to take the
 *          PendSV a switch pends: the vector table is not OpSy's yet.
 *
 *          Found by a project that set a task's priority from main, before
 *          start, and died in the assert of do_switch in debug.
 *
 *          Passing means both tasks run after start, the one raised to the
 *          front first, and each sees the priority it was given. Its own image
 *          because it is about the time before start: the suite in the main
 *          image needs a scheduler that started.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"
#include "semihosting.hpp"

#include <opsy.hpp>

namespace
{

opsy::idle_task<256> g_idle;
opsy::task<512>      g_first;
opsy::task<512>      g_second;

int g_turn = 0;

[[noreturn]] void fail(const char* what)
{
	qemu::write("FAIL: ");
	qemu::write(what);
	qemu::write("\nRESULT: FAIL\n");
	qemu::exit(1);
}

} // namespace

int opsy_test_main()
{
	qemu::write("OpSy before start: priorities set between task start and scheduler start\n\n");

	// Both start at the default priority, so the second one queues behind the
	// first. Raising it afterwards has to bring it to the front of ready_,
	// which is the move that asked for a switch.
	(void) g_first.start([]
	{
		if (g_turn++ != 1)
			fail("the task left at the default priority ran first");
		if (g_first.priority() != opsy::task_priority::normal)
			fail("the first task does not report the default priority");
		qemu::write("RESULT: PASS\n");
		qemu::exit(0);
	}, "first");

	(void) g_second.start([]
	{
		if (g_turn++ != 0)
			fail("the task raised before start did not run first");
		if (g_second.priority() != opsy::task_priority::high)
			fail("the raised task does not report its priority");
		g_second.set_name("second, raised");
		while (true)
			opsy::sleep_for(std::chrono::milliseconds(1));
	}, "second");

	g_second.priority(opsy::task_priority::high);
	g_first.priority(opsy::task_priority::low);
	g_first.priority(opsy::task_priority::normal);

	opsy::scheduler::start(g_idle);
}
