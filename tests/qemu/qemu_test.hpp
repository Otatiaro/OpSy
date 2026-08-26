/**
 ******************************************************************************
 * @file    qemu_test.hpp
 * @brief   Test harness for the on-target (QEMU) OpSy suite.
 *
 *          Unlike the host suite, these cases cannot run from @c main : the
 *          behaviour under test only exists once the scheduler owns the CPU.
 *          Every case therefore runs from inside an OpSy task, and may block,
 *          sleep, and start or stop other tasks.
 *
 *          Cases run one after another on the same runner task, so each one
 *          must leave the system as it found it — no task left started, no
 *          mutex left locked.
 *
 * @warning Do not use @c assert : OpSy installs its own, which routes to
 *          @c opsy::trap . Use @ref CHECK.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

namespace qemu_test
{

using test_fn = void (*)();

/** @brief One registered case; constructing it links it into the run list. */
struct test_case
{
	test_case(const char* case_name, test_fn body);

	const char* name;
	test_fn     run;
	test_case*  next;
};

test_case*& registry();

void report_failure(const char* file, int line, const char* expression);

/** @brief Runs every case, then ends the emulation with the right exit code. */
[[noreturn]] void run_all_and_exit();

} // namespace qemu_test

#define OPSY_QEMU_TEST(name)                                                   \
	static void name();                                                        \
	static ::qemu_test::test_case name##_registration{#name, &name};           \
	static void name()

#define CHECK(expression)                                                      \
	do                                                                         \
	{                                                                          \
		if (!(expression))                                                     \
			::qemu_test::report_failure(__FILE__, __LINE__, #expression);      \
	} while (false)
