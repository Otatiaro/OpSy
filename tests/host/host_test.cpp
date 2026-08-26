/**
 ******************************************************************************
 * @file    host_test.cpp
 * @brief   Runner for the host-side OpSy test suite, plus the @c opsy::trap
 *          definition every OpSy header expects at link time.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <cstdio>
#include <cstdlib>

namespace opsy
{

/**
 * @brief OpSy's assert macro routes here, and opsy_assert.hpp only declares it
 *        — the default definition lives in config.hpp, which a host build that
 *        includes a single utility header never pulls in.
 *
 *        Failing loudly is the right behaviour for a test binary: a tripped
 *        internal assert is a defect, not something to keep running past.
 */
[[noreturn]] void trap()
{
	std::fprintf(stderr, "\n*** opsy::trap() reached: an OpSy internal assert failed ***\n");
	std::fflush(stderr);
	std::abort();
}

} // namespace opsy

namespace host_test
{
namespace
{

const char* g_current_test = "<none>";
int         g_failures     = 0;

} // namespace

test_case*& registry()
{
	// Function-local static: the registration objects are constructed in an
	// unspecified order across translation units, and a namespace-scope
	// pointer could still be zero-initialised after the first one runs.
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
	std::printf("  FAIL %s\n       %s:%d\n       CHECK(%s)\n", g_current_test, file, line, expression);
	++g_failures;
}

void report_mismatch(const char* file, int line, const char* expression,
                     double actual, double expected, double tolerance)
{
	std::printf("  FAIL %s\n       %s:%d\n       %s = %.9g, expected %.9g +/- %.9g\n",
	            g_current_test, file, line, expression, actual, expected, tolerance);
	++g_failures;
}

int run_all()
{
	int cases = 0;
	for (auto* current = registry(); current != nullptr; current = current->next)
	{
		g_current_test = current->name;
		const int before = g_failures;
		current->run();
		++cases;
		if (g_failures == before)
			std::printf("  ok   %s\n", current->name);
	}

	std::printf("\n%d test case(s), %d failure(s)\n", cases, g_failures);
	return g_failures == 0 ? 0 : 1;
}

} // namespace host_test

int main()
{
	std::printf("OpSy host test suite\n\n");
	return host_test::run_all();
}
