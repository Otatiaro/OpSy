/**
 ******************************************************************************
 * @file    test_biquad.cpp
 * @brief   Numerical tests for @c opsy::utility::biquad .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <utility/biquad.hpp>

namespace
{

using opsy::utility::biquad;
using opsy::utility::filter_type;

using filter = biquad<double, double>;

constexpr double sampling = 48000.0;
constexpr double cut      = 1000.0;

/** @brief Steady-state output after feeding a constant input. */
double steady_state(filter& subject, double input, double initial)
{
	subject.reset(initial);
	for (int i = 0; i < 4000; ++i)
		subject.feed(input);
	return subject.value();
}

// b1 and b2 are defined in terms of b0, and the constructor reads b_[0] while
// initialising b_. That is well defined and these tests pin it: C++17
// guaranteed copy elision means the braced std::array prvalue initialises b_
// directly rather than through a temporary, so the aggregate elements are
// initialised in order and b_[0] is already written. The static_assert below
// is the actual proof — an uninitialised read would make the expression
// non-constant and fail to compile.

struct ordering_probe
{
	constexpr ordering_probe() : values({first(), second(), third()}) {}
	constexpr int first() const { return 10; }
	constexpr int second() const { return 2 * values[0]; }
	constexpr int third() const { return values[0]; }
	std::array<int, 3> values;
};

static_assert(ordering_probe{}.values[0] == 10);
static_assert(ordering_probe{}.values[1] == 20, "aggregate elements must initialise in order");
static_assert(ordering_probe{}.values[2] == 10);

OPSY_TEST(biquad_low_pass_has_unit_dc_gain)
{
	// (b0 + b1 + b2) / (1 + a1 + a2) == 1 only if b1 == 2*b0 and b2 == b0.
	filter subject{sampling, cut, filter_type::low_pass};
	CHECK_NEAR(steady_state(subject, 1.0, 1.0), 1.0, 1e-6);
	CHECK_NEAR(steady_state(subject, 0.25, 0.25), 0.25, 1e-6);
}

OPSY_TEST(biquad_high_pass_rejects_dc)
{
	filter subject{sampling, cut, filter_type::high_pass};
	CHECK_NEAR(steady_state(subject, 1.0, 0.0), 0.0, 1e-6);
}

OPSY_TEST(biquad_notch_passes_dc)
{
	filter subject{sampling, cut, filter_type::notch};
	CHECK_NEAR(steady_state(subject, 1.0, 1.0), 1.0, 1e-6);
}

OPSY_TEST(biquad_band_pass_rejects_dc)
{
	filter subject{sampling, cut, filter_type::band_pass};
	CHECK_NEAR(steady_state(subject, 1.0, 0.0), 0.0, 1e-6);
}

OPSY_TEST(biquad_reset_reaches_the_requested_output_immediately)
{
	filter subject{sampling, cut, filter_type::low_pass};
	subject.reset(3.5);
	CHECK_NEAR(subject.value(), 3.5, 1e-6);
}

OPSY_TEST(biquad_static_storage_behaves_like_automatic_storage)
{
	// Static storage is the case a mis-ordered initialisation would break
	// differently, since the pre-dynamic-init value is a well-defined zero.
	static filter static_subject{sampling, cut, filter_type::low_pass};
	filter automatic_subject{sampling, cut, filter_type::low_pass};
	CHECK_NEAR(steady_state(static_subject, 1.0, 1.0),
	           steady_state(automatic_subject, 1.0, 1.0), 1e-12);
}

} // namespace
