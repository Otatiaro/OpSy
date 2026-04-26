/**
 ******************************************************************************
 * @file    utility_sanity.cpp
 * @brief   Compile-only smoke + constexpr test for the utility headers.
 *
 *          Instantiates each utility template with valid configurations to
 *          trigger the in-class @c static_assert checks, and adds extra
 *          @c static_assert checks on the @c constexpr surface (sizes,
 *          @c x()/@c y()/@c z()/@c w(), @c at, @c append, @c prepend,
 *          @c sub, arithmetic operators...). Runtime-only paths
 *          (@c std::sqrt, @c std::tan, etc.) are kept alive with
 *          @c [[gnu::used]] so the linker keeps them under
 *          @c -ffunction-sections / @c --gc-sections builds.
 ******************************************************************************
 */

#include <utility/allocator.hpp>
#include <utility/biquad.hpp>
#include <utility/slope.hpp>
#include <utility/vector.hpp>

namespace {

// ============================== allocator ==============================
// In-class static_asserts: N > 3, sizeof(element_type) == sizeof(void*).
// Constexpr surface: ctor, size(), available(), empty(), run_check().

static_assert(opsy::utility::allocator<10, true>{}.size() == 8 * sizeof(int));
static_assert(opsy::utility::allocator<10, true>{}.available() == 8 * sizeof(int));
static_assert(opsy::utility::allocator<10, true>{}.empty());
static_assert(opsy::utility::allocator<10, true>{}.run_check());

[[gnu::used]] opsy::utility::allocator<32> g_alloc;

[[gnu::used]] void use_allocator()
{
	auto* p = g_alloc.allocate(8);
	if (p)
		g_alloc.deallocate(p);
	(void) g_alloc.size();
	(void) g_alloc.available();
	(void) g_alloc.empty();
	(void) g_alloc.owns(p);
	(void) g_alloc.run_check();
}

// =============================== biquad ================================
// std::tan / std::acos are not constexpr in C++23 (will be in C++26), so
// the biquad ctor is exercised at runtime. Each filter type is instantiated
// to ensure the per-coefficient switch arms all compile.

[[gnu::used]] opsy::utility::biquad<float> g_biquad_lp{
	48000.0f, 1000.0f, opsy::utility::filter_type::low_pass};
[[gnu::used]] opsy::utility::biquad<float> g_biquad_hp{
	48000.0f, 1000.0f, opsy::utility::filter_type::high_pass};
[[gnu::used]] opsy::utility::biquad<float> g_biquad_notch{
	48000.0f, 1000.0f, opsy::utility::filter_type::notch};
[[gnu::used]] opsy::utility::biquad<float> g_biquad_bp{
	48000.0f, 1000.0f, opsy::utility::filter_type::band_pass};

[[gnu::used]] void use_biquad()
{
	g_biquad_lp.feed(1.0f);
	(void) g_biquad_lp.value();
	g_biquad_lp.reset(0.0f);
}

// ================================ slope ================================
// In-class static_assert: N >= 4. value() is constexpr, but values_ start
// uninitialized so the constexpr test feeds the buffer first.

[[gnu::used]] opsy::utility::slope<8> g_slope{1000.0f};

[[gnu::used]] void use_slope()
{
	for (auto i = 0; i < 8; ++i)
		g_slope.feed(static_cast<float>(i));
	(void) g_slope.value();
}

// =============================== vector ================================
// In-class static_asserts: N != 0, N != 1, plus per-method I < N etc.

constexpr opsy::utility::vector<3> v3{1.0f, 2.0f, 3.0f};
static_assert(v3.size() == 3);
static_assert(v3.x() == 1.0f);
static_assert(v3.y() == 2.0f);
static_assert(v3.z() == 3.0f);
static_assert(v3.norm() == 14.0f);
static_assert(v3.at<0>() == 1.0f);
static_assert(v3.at<2>() == 3.0f);

constexpr auto v3_doubled = v3 * 2.0f;
static_assert(v3_doubled.x() == 2.0f);
static_assert(v3_doubled.z() == 6.0f);

constexpr auto v3_neg = -v3;
static_assert(v3_neg.x() == -1.0f);

constexpr auto v3_sum = v3 + v3;
static_assert(v3_sum.y() == 4.0f);

constexpr auto v3_diff = v3 - v3;
static_assert(v3_diff.x() == 0.0f);

constexpr auto v4 = v3.append(4.0f);
static_assert(v4.size() == 4);
static_assert(v4.w() == 4.0f);

constexpr auto v3p = v3.prepend(0.0f);
static_assert(v3p.size() == 4);
static_assert(v3p.x() == 0.0f);

constexpr auto v2 = v3.sub<2, 1>();
static_assert(v2.size() == 2);
static_assert(v2.x() == 2.0f);
static_assert(v2.y() == 3.0f);

[[gnu::used]] opsy::utility::vector<4> g_vec;

[[gnu::used]] void use_vector()
{
	opsy::utility::vector<3> v{1.0f, 2.0f, 3.0f};
	v += v;
	v -= v;
	v *= 2.0f;
	v /= 2.0f;
	(void) v.length();
	(void) v.normalized();
	v.normalize();

	opsy::utility::vector<3> a{1.0f, 0.0f, 0.0f};
	opsy::utility::vector<3> b{0.0f, 1.0f, 0.0f};
	(void) opsy::utility::dot_product(a, b);
	(void) opsy::utility::cross_product(a, b);

	opsy::utility::vector<2> v2d{1.0f, 0.0f};
	(void) opsy::utility::rotate(v2d, 1.0f);
}

}
