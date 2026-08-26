/**
 ******************************************************************************
 * @file    test_ellipsoid_fit.cpp
 * @brief   Numerical tests for @c opsy::algorithms::ellipsoid_fit .
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "host_test.hpp"

#include <algorithms/ellipsoid_fit.hpp>

#include <cmath>
#include <numbers>

namespace
{

using opsy::algorithms::ellipsoid_fit;
using opsy::utility::vector;

constexpr double pi = std::numbers::pi_v<double>;

/** @brief Feeds a full sweep over an axis-aligned ellipsoid. */
void feed_ellipsoid(ellipsoid_fit<double>& subject,
                    const double (&semi_axes)[3], const double (&centre)[3],
                    int steps = 40)
{
	for (int i = 1; i < steps; ++i)
		for (int j = 0; j < steps; ++j)
		{
			const double theta = pi * i / steps;
			const double phi   = 2.0 * pi * j / steps;
			subject.feed(vector<3, double>{
				centre[0] + semi_axes[0] * std::sin(theta) * std::cos(phi),
				centre[1] + semi_axes[1] * std::sin(theta) * std::sin(phi),
				centre[2] + semi_axes[2] * std::cos(theta)});
		}
}

double magnitude(const vector<3, double>& v)
{
	return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// ──────────────────── regression: soft_iron was inverted ───────────────────
// Step 7 built V·diag(1/sqrt(lambda))·Vᵀ = N^(-1/2), which maps the unit
// sphere onto the ellipsoid — the opposite of what correct() needs. lambda is
// the inverse squared semi-axis, so the matrix wanted is N^(1/2).
//
// The Cortex-M suite cannot catch this: every sample it feeds sits on the unit
// sphere, where sqrt(lambda) == 1 / sqrt(lambda) == 1.

OPSY_TEST(ellipsoid_fit_maps_a_known_ellipsoid_onto_the_unit_sphere)
{
	const double semi_axes[3]{2.0, 3.0, 4.0};
	const double centre[3]{1.0, -2.0, 3.0};

	ellipsoid_fit<double> subject;
	feed_ellipsoid(subject, semi_axes, centre);

	const auto calibration = subject.fit();
	CHECK(calibration.has_value());
	if (!calibration)
		return;

	CHECK_NEAR(calibration->hard_iron[0], centre[0], 1e-6);
	CHECK_NEAR(calibration->hard_iron[1], centre[1], 1e-6);
	CHECK_NEAR(calibration->hard_iron[2], centre[2], 1e-6);

	// The diagonal is 1 / semi-axis. Inverted, it came back as 2 / 3 / 4.
	CHECK_NEAR(calibration->soft_iron(0, 0), 1.0 / semi_axes[0], 1e-6);
	CHECK_NEAR(calibration->soft_iron(1, 1), 1.0 / semi_axes[1], 1e-6);
	CHECK_NEAR(calibration->soft_iron(2, 2), 1.0 / semi_axes[2], 1e-6);

	// The property that actually matters: every sample lands on |q| == 1.
	// With the inverted matrix this ranged over [4, 16].
	for (int i = 1; i < 20; ++i)
		for (int j = 0; j < 20; ++j)
		{
			const double theta = pi * i / 20;
			const double phi   = 2.0 * pi * j / 20;
			const vector<3, double> sample{
				centre[0] + semi_axes[0] * std::sin(theta) * std::cos(phi),
				centre[1] + semi_axes[1] * std::sin(theta) * std::sin(phi),
				centre[2] + semi_axes[2] * std::cos(theta)};
			CHECK_NEAR(magnitude(calibration->correct(sample)), 1.0, 1e-6);
		}
}

OPSY_TEST(ellipsoid_fit_handles_a_sphere_off_the_origin)
{
	const double semi_axes[3]{5.0, 5.0, 5.0};
	const double centre[3]{-1.5, 0.25, 7.0};

	ellipsoid_fit<double> subject;
	feed_ellipsoid(subject, semi_axes, centre);

	const auto calibration = subject.fit();
	CHECK(calibration.has_value());
	if (!calibration)
		return;

	CHECK_NEAR(calibration->hard_iron[0], centre[0], 1e-6);
	CHECK_NEAR(calibration->hard_iron[1], centre[1], 1e-6);
	CHECK_NEAR(calibration->hard_iron[2], centre[2], 1e-6);
	CHECK_NEAR(calibration->soft_iron(0, 0), 1.0 / 5.0, 1e-6);
}

// ──────────────── regression: degenerate input produced NaN ────────────────
// fit() divided by a pivot, a determinant and a constant term without checking
// any of them, and rooted eigenvalues that go negative on a partial sweep.
// In a debug build it tripped the symmetry assert inside the eigen
// decomposition (NaN != NaN); under NDEBUG it returned an all-NaN calibration
// that would then be persisted to non-volatile storage.

OPSY_TEST(ellipsoid_fit_rejects_an_empty_accumulator)
{
	ellipsoid_fit<double> subject;
	CHECK(subject.count() == 0);
	CHECK(!subject.fit().has_value());
}

OPSY_TEST(ellipsoid_fit_rejects_too_few_samples)
{
	ellipsoid_fit<double> subject;
	for (std::size_t i = 0; i < ellipsoid_fit<double>::minimum_samples - 1; ++i)
		subject.feed(vector<3, double>{static_cast<double>(i), 1.0, 2.0});

	CHECK(subject.count() == ellipsoid_fit<double>::minimum_samples - 1);
	CHECK(!subject.fit().has_value());
}

OPSY_TEST(ellipsoid_fit_rejects_coplanar_samples)
{
	ellipsoid_fit<double> subject;
	for (int i = 0; i < 400; ++i)
	{
		const double angle = 2.0 * pi * i / 400.0;
		subject.feed(vector<3, double>{std::cos(angle), std::sin(angle), 0.0});
	}
	CHECK(!subject.fit().has_value());
}

OPSY_TEST(ellipsoid_fit_rejects_collinear_samples)
{
	ellipsoid_fit<double> subject;
	for (int i = 0; i < 400; ++i)
		subject.feed(vector<3, double>{i * 0.01, 0.0, 0.0});
	CHECK(!subject.fit().has_value());
}

OPSY_TEST(ellipsoid_fit_reset_clears_the_sample_count)
{
	const double semi_axes[3]{1.0, 2.0, 3.0};
	const double centre[3]{0.0, 0.0, 0.0};

	ellipsoid_fit<double> subject;
	feed_ellipsoid(subject, semi_axes, centre, 12);
	CHECK(subject.count() > 0);
	CHECK(subject.fit().has_value());

	subject.reset();
	CHECK(subject.count() == 0);
	CHECK(!subject.fit().has_value());   // and the accumulators went with it
}

} // namespace
