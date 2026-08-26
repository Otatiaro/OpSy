/**
 ******************************************************************************
 * @file    qemu_test.cpp
 * @brief   Runner for the on-target OpSy suite: brings the scheduler up, runs
 *          every registered case from a task, and reports the verdict.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"
#include "semihosting.hpp"

#include <opsy.hpp>

namespace qemu_test
{
namespace
{

const char* g_current_test = "<none>";
int         g_failures     = 0;
int         g_cases        = 0;

} // namespace

test_case*& registry()
{
	static test_case* head = nullptr;
	return head;
}

test_case::test_case(const char* case_name, test_fn body) :
		name(case_name), run(body), next(registry())
{
	registry() = this;
}

void report_failure(const char* file, int line, const char* expression)
{
	qemu::write("  FAIL ");
	qemu::write(g_current_test);
	qemu::write("\n       ");
	qemu::write(file);
	qemu::write(":");
	qemu::write_unsigned(static_cast<uint32_t>(line));
	qemu::write("\n       CHECK(");
	qemu::write(expression);
	qemu::write(")\n");
	++g_failures;
}

[[noreturn]] void run_all_and_exit()
{
	for (auto* current = registry(); current != nullptr; current = current->next)
	{
		g_current_test = current->name;
		const int before = g_failures;

		// Announced before it runs, not after: a case that hangs or faults would
		// otherwise leave no trace of which one it was.
		qemu::write("  .... ");
		qemu::write(current->name);
		qemu::write("\n");
		current->run();
		++g_cases;

		if (g_failures == before)
		{
			qemu::write("  ok   ");
			qemu::write(current->name);
			qemu::write("\n");
		}
	}

	qemu::write("\n");
	qemu::write_unsigned(static_cast<uint32_t>(g_cases));
	qemu::write(" test case(s), ");
	qemu::write_unsigned(static_cast<uint32_t>(g_failures));
	qemu::write(" failure(s)\n");

	// The CI step greps for this line: QEMU collapses every non-zero
	// application exit code to a process status of 1, so the status alone
	// cannot tell a failed assertion from a crashed image.
	qemu::write(g_failures == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
	qemu::exit(g_failures == 0 ? 0 : 1);
}

} // namespace qemu_test

// opsy::trap is not defined here on purpose: config.hpp already provides the
// default inline definition, and no <opsy_config.hpp> overrides it. It expands
// to __builtin_trap(), which raises an undefined instruction and escalates to
// HardFault -- caught by startup.cpp's handler, which prints and exits
// non-zero. A tripped internal assert therefore still fails the run loudly.

namespace
{

opsy::task<2048>      g_runner;
opsy::idle_task<256>  g_idle;

} // namespace

int opsy_test_main()
{
	qemu::write("OpSy on-target test suite\n\n");
	// The runner raises itself once it is running: priority() goes through the
	// scheduler, which is not up yet at this point in main.
	//
	// It sits above the default so the cases are deterministic: tasks start at
	// task_priority::lowest, so a freshly started helper cannot run until the
	// runner blocks -- which is what lets a case observe a helper before it has
	// had any chance to execute. A helper that must preempt the runner is
	// raised to highest explicitly.
	if (!g_runner.start([]
	{
		g_runner.priority(opsy::task_priority::high);
		qemu_test::run_all_and_exit();
	}, "runner"))
	{
		qemu::write("*** could not start the runner task ***\nRESULT: FAIL\n");
		qemu::exit(1);
	}

	opsy::scheduler::start(g_idle);
}
