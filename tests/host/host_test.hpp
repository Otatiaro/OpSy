/**
 ******************************************************************************
 * @file    host_test.hpp
 * @brief   Minimal test harness for the host-side OpSy test suite.
 *
 *          The Cortex-M suite next door is compile-only: it is cross-compiled
 *          into a static library and never linked or run, so it can assert
 *          about types but not about behaviour. Everything in OpSy that is
 *          plain C++ — the containers, the allocator, the numerics — can be
 *          built and executed natively, and that is what this harness is for.
 *
 *          Each fixed bug gets a case here that fails on the old code, which
 *          is the property that makes the suite worth running.
 *
 *          Tests register themselves at static-init time through an intrusive
 *          list, so adding a file to CMakeLists.txt is all it takes; there is
 *          no central list to keep in sync.
 *
 * @warning Do not use @c assert in tests. OpSy headers install their own
 *          @c assert macro (see opsy_assert.hpp) which routes to
 *          @c opsy::trap and aborts the whole run. Use @ref CHECK.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstddef>

namespace host_test
{

using test_fn = void (*)();

/**
 * @brief One registered test case. Constructing it links it into the list the
 *        runner walks, so a file-scope instance is the whole registration.
 */
struct test_case
{
	test_case(const char* case_name, test_fn body);

	const char* name;
	test_fn     run;
	test_case*  next;
};

/** @brief Head of the intrusive registration list. */
test_case*& registry();

/** @brief Records a failed check against the currently running test. */
void report_failure(const char* file, int line, const char* expression);

/** @brief Records a failed near-equality check, with both values. */
void report_mismatch(const char* file, int line, const char* expression,
                     double actual, double expected, double tolerance);

/**
 * @brief Runs every registered test.
 * @return 0 if all passed, 1 otherwise (process exit code).
 */
int run_all();

} // namespace host_test

/**
 * @brief Defines and registers a test case.
 *
 *        Usage: @c OPSY_TEST(name) @c { @c ... @c }
 */
#define OPSY_TEST(name)                                                        \
	static void name();                                                        \
	static ::host_test::test_case name##_registration{#name, &name};           \
	static void name()

/** @brief Fails the current test if @p expression is false, and carries on. */
#define CHECK(expression)                                                      \
	do                                                                         \
	{                                                                          \
		if (!(expression))                                                     \
			::host_test::report_failure(__FILE__, __LINE__, #expression);      \
	} while (false)

/** @brief Fails the current test if @p actual is not within @p tolerance of @p expected. */
#define CHECK_NEAR(actual, expected, tolerance)                                \
	do                                                                         \
	{                                                                          \
		const double check_actual_    = static_cast<double>(actual);           \
		const double check_expected_  = static_cast<double>(expected);         \
		const double check_tolerance_ = static_cast<double>(tolerance);        \
		const double check_delta_     = check_actual_ - check_expected_;       \
		if (!((check_delta_ < 0 ? -check_delta_ : check_delta_) <= check_tolerance_)) \
			::host_test::report_mismatch(__FILE__, __LINE__, #actual,          \
			                             check_actual_, check_expected_,       \
			                             check_tolerance_);                    \
	} while (false)
