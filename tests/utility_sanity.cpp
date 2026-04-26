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
#include <utility/matrix.hpp>
#include <utility/memory.hpp>
#include <utility/quaternion.hpp>
#include <utility/slope.hpp>
#include <utility/vector.hpp>

namespace {

// ============================== allocator ==============================
// In-class static_asserts: N > 3, sizeof(element_type) == sizeof(void*).
// Constexpr surface: ctor, size(), available(), empty(), run_check().

static_assert(opsy::utility::allocator<10, true>{}.size() == 8 * sizeof(int));
// available() now reports what allocate() will actually accept: the trailing
// free chunk minus the 2 indicator slots that any new allocation must push
// in for the new free chunk that follows it.
static_assert(opsy::utility::allocator<10, true>{}.available() == 6 * sizeof(int));
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

// dot_product / cross_product are constexpr — lock that in.
static_assert(opsy::utility::dot_product(v3, v3) == 14.0f);
constexpr opsy::utility::vector<3> ex{1.0f, 0.0f, 0.0f};
constexpr opsy::utility::vector<3> ey{0.0f, 1.0f, 0.0f};
constexpr auto ez = opsy::utility::cross_product(ex, ey);
static_assert(ez.x() == 0.0f);
static_assert(ez.y() == 0.0f);
static_assert(ez.z() == 1.0f);

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

// ================================ matrix ===============================
// Default ctor is the zero matrix.
constexpr opsy::utility::matrix<2, 3> m_zero;
static_assert(m_zero(0, 0) == 0.0f);
static_assert(m_zero(1, 2) == 0.0f);

// Variadic ctor: row-major flat list.
constexpr opsy::utility::matrix<2, 3> m_a{
	1.0f, 2.0f, 3.0f,
	4.0f, 5.0f, 6.0f
};
static_assert(m_a(0, 0) == 1.0f);
static_assert(m_a(0, 2) == 3.0f);
static_assert(m_a(1, 0) == 4.0f);
static_assert(m_a(1, 2) == 6.0f);
static_assert(m_a.at<0, 1>() == 2.0f);
static_assert(m_a.at<1, 1>() == 5.0f);

// row(i) and col(j).
constexpr auto row1 = m_a.row(1);
static_assert(row1.x() == 4.0f && row1.y() == 5.0f && row1.z() == 6.0f);
constexpr auto col2 = m_a.col(2);
static_assert(col2.x() == 3.0f && col2.y() == 6.0f);

// Element-wise + and -.
constexpr auto m_sum = m_a + m_a;
static_assert(m_sum(0, 0) == 2.0f && m_sum(1, 2) == 12.0f);
constexpr auto m_zero2 = m_a - m_a;
static_assert(m_zero2 == m_zero);

// Unary minus + scalar mul/div.
static_assert((-m_a)(0, 0) == -1.0f);
static_assert((m_a * 2.0f)(1, 1) == 10.0f);
static_assert((2.0f * m_a)(1, 1) == 10.0f);
static_assert((m_a / 2.0f)(0, 2) == 1.5f);

// Transpose: <2, 3> -> <3, 2>.
constexpr auto m_at = m_a.transposed();
static_assert(m_at.row_count == 3 && m_at.column_count == 2);
static_assert(m_at(0, 0) == 1.0f && m_at(2, 0) == 3.0f);
static_assert(m_at(0, 1) == 4.0f && m_at(2, 1) == 6.0f);
// (m^T)^T == m
static_assert(m_at.transposed() == m_a);

// Identity matrix.
constexpr auto i3 = opsy::utility::identity_matrix<3>();
static_assert(i3(0, 0) == 1.0f && i3(1, 1) == 1.0f && i3(2, 2) == 1.0f);
static_assert(i3(0, 1) == 0.0f && i3(2, 0) == 0.0f);

// Matrix * matrix: identity is the neutral element.
constexpr opsy::utility::matrix<3, 3> m_b{
	1.0f, 2.0f, 3.0f,
	4.0f, 5.0f, 6.0f,
	7.0f, 8.0f, 9.0f
};
static_assert(i3 * m_b == m_b);
static_assert(m_b * i3 == m_b);

// A specific 2x2 product, hand-computed.
//   ( 1 2 )   ( 5 6 )   ( 1*5+2*7  1*6+2*8 )   ( 19 22 )
//   ( 3 4 ) * ( 7 8 ) = ( 3*5+4*7  3*6+4*8 ) = ( 43 50 )
constexpr opsy::utility::matrix<2, 2> m_l{1.0f, 2.0f, 3.0f, 4.0f};
constexpr opsy::utility::matrix<2, 2> m_r{5.0f, 6.0f, 7.0f, 8.0f};
constexpr auto m_p = m_l * m_r;
static_assert(m_p(0, 0) == 19.0f);
static_assert(m_p(0, 1) == 22.0f);
static_assert(m_p(1, 0) == 43.0f);
static_assert(m_p(1, 1) == 50.0f);

// Non-square product: <2, 3> * <3, 2> = <2, 2>.
constexpr opsy::utility::matrix<3, 2> m_c{
	7.0f,  8.0f,
	9.0f, 10.0f,
	11.0f, 12.0f
};
constexpr auto m_a_times_c = m_a * m_c;
// Row 0: (1, 2, 3) . cols of m_c
//   col0: 1*7 + 2*9 + 3*11 = 7+18+33 = 58
//   col1: 1*8 + 2*10 + 3*12 = 8+20+36 = 64
static_assert(m_a_times_c(0, 0) == 58.0f);
static_assert(m_a_times_c(0, 1) == 64.0f);
// Row 1: (4, 5, 6) . cols of m_c
//   col0: 4*7 + 5*9 + 6*11 = 28+45+66 = 139
//   col1: 4*8 + 5*10 + 6*12 = 32+50+72 = 154
static_assert(m_a_times_c(1, 0) == 139.0f);
static_assert(m_a_times_c(1, 1) == 154.0f);

// Matrix * vector.
//   m_a * (1, 1, 1) = (1+2+3, 4+5+6) = (6, 15)
constexpr opsy::utility::vector<3> v_ones{1.0f, 1.0f, 1.0f};
constexpr auto v_mv = m_a * v_ones;
static_assert(v_mv.x() == 6.0f && v_mv.y() == 15.0f);

// Identity * v == v.
constexpr opsy::utility::vector<3> v_arb{1.0f, 2.0f, 3.0f};
static_assert(i3 * v_arb == v_arb);

// Determinant — closed form for 1x1 .. 4x4.
static_assert(opsy::utility::matrix<1, 1>{ 7.0f }.determinant() == 7.0f);
static_assert(m_l.determinant() == -2.0f); // 1*4 - 2*3 = -2
//   ( 1 2 3 )
//   ( 0 1 4 )    det = 1*(1*0 - 4*6) - 2*(0*0 - 4*5) + 3*(0*6 - 1*5)
//   ( 5 6 0 )        = 1*(-24) - 2*(-20) + 3*(-5)
//                    = -24 + 40 - 15 = 1
constexpr opsy::utility::matrix<3, 3> m_d3{
	1.0f, 2.0f, 3.0f,
	0.0f, 1.0f, 4.0f,
	5.0f, 6.0f, 0.0f
};
static_assert(m_d3.determinant() == 1.0f);

// Identity always has determinant 1.
static_assert(opsy::utility::identity_matrix<3>().determinant() == 1.0f);
static_assert(opsy::utility::identity_matrix<4>().determinant() == 1.0f);

// Inverse — closed form for 1x1 .. 4x4.
//   For a 2x2 with det == 1, inverse(M) * M == opsy::utility::identity_matrix<2>().
constexpr opsy::utility::matrix<2, 2> m_unit_det{2.0f, 1.0f, 1.0f, 1.0f}; // det = 2-1 = 1
static_assert(m_unit_det.inverse() == opsy::utility::matrix<2, 2>{1.0f, -1.0f, -1.0f, 2.0f});
static_assert(m_unit_det.inverse() * m_unit_det == opsy::utility::identity_matrix<2>());
static_assert(m_unit_det * m_unit_det.inverse() == opsy::utility::identity_matrix<2>());

// 3x3: inverse of identity is identity.
static_assert(opsy::utility::identity_matrix<3>().inverse() == opsy::utility::identity_matrix<3>());

// 3x3: a known invertible matrix — verify inverse(M) * M == I.
static_assert(m_d3.inverse() * m_d3 == opsy::utility::identity_matrix<3>());

// 4x4: identity inverse == identity.
static_assert(opsy::utility::identity_matrix<4>().inverse() == opsy::utility::identity_matrix<4>());

// 4x4: a non-trivial invertible matrix.
//   Use a triangular matrix with 1s on the diagonal and known det = 1, easy to verify.
constexpr opsy::utility::matrix<4, 4> m_d4{
	1.0f, 2.0f, 3.0f, 4.0f,
	0.0f, 1.0f, 5.0f, 6.0f,
	0.0f, 0.0f, 1.0f, 7.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};
static_assert(m_d4.determinant() == 1.0f);
static_assert(m_d4.inverse() * m_d4 == opsy::utility::identity_matrix<4>());

// Inverse for sizes >= 5 dispatches to Gauss-Jordan with partial pivoting.
// Helper for tolerance-based equality (Gauss-Jordan accumulates rounding).
template<std::size_t R, std::size_t C, typename T>
constexpr bool matrix_approx_equal(
	const opsy::utility::matrix<R, C, T>& a,
	const opsy::utility::matrix<R, C, T>& b,
	T tol)
{
	for (std::size_t i = 0; i < R; ++i)
		for (std::size_t j = 0; j < C; ++j)
		{
			T d = a(i, j) - b(i, j);
			if (d < T{0}) d = -d;
			if (d > tol) return false;
		}
	return true;
}

// 5x5: identity inverse is exact.
static_assert(opsy::utility::identity_matrix<5>().inverse() == opsy::utility::identity_matrix<5>());

// 5x5: a permutation matrix forces partial pivoting (the initial diagonal
// has zeros so the pivot search must swap rows). Permutations are
// involutions, so the inverse equals the transpose, and M*M is exactly the
// identity even in float arithmetic.
constexpr opsy::utility::matrix<5, 5> m_p5{
	0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 1.0f
};
static_assert(m_p5.inverse() == m_p5.transposed());
static_assert(m_p5.inverse() * m_p5 == opsy::utility::identity_matrix<5>());

// 5x5: a diagonal matrix with arbitrary scales — pivoting picks the largest
// scale first. Inverse * original is identity within float tolerance.
constexpr opsy::utility::matrix<5, 5> m_d5{
	2.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 3.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 5.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 7.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f, 11.0f
};
static_assert(matrix_approx_equal(m_d5.inverse() * m_d5, opsy::utility::identity_matrix<5>(), 1e-6f));

// 9x9: identity round-trip. Exercises the dispatch on a size that ellipsoid
// fitting will eventually need (DᵀD is 9x9 in that algorithm).
static_assert(opsy::utility::identity_matrix<9>().inverse() == opsy::utility::identity_matrix<9>());

// 9x9: a non-trivial well-conditioned matrix — diagonal with off-diagonal
// perturbation small enough that the system stays diagonally dominant.
constexpr auto m_9 = []{
	opsy::utility::matrix<9, 9> m;
	for (std::size_t i = 0; i < 9; ++i)
	{
		m(i, i) = static_cast<float>(i + 2);
		if (i + 1 < 9)
			m(i, i + 1) = 0.25f;
	}
	return m;
}();
static_assert(matrix_approx_equal(m_9.inverse() * m_9, opsy::utility::identity_matrix<9>(), 1e-5f));
static_assert(matrix_approx_equal(m_9 * m_9.inverse(), opsy::utility::identity_matrix<9>(), 1e-5f));

// ── Row operations (constexpr — exercise mutation in a constexpr lambda).
constexpr auto m_swapped = []{
	opsy::utility::matrix<3, 3> m{
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f,
		7.0f, 8.0f, 9.0f
	};
	m.swap_rows(0, 2);
	return m;
}();
static_assert(m_swapped(0, 0) == 7.0f && m_swapped(0, 2) == 9.0f);
static_assert(m_swapped(2, 0) == 1.0f && m_swapped(2, 2) == 3.0f);
static_assert(m_swapped(1, 1) == 5.0f);   // middle row untouched

constexpr auto m_self_swap = []{
	opsy::utility::matrix<2, 2> m{1.0f, 2.0f, 3.0f, 4.0f};
	m.swap_rows(1, 1);  // no-op path
	return m;
}();
static_assert(m_self_swap == opsy::utility::matrix<2, 2>{1.0f, 2.0f, 3.0f, 4.0f});

constexpr auto m_scaled = []{
	opsy::utility::matrix<2, 3> m{
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f
	};
	m.scale_row(1, 0.5f);
	return m;
}();
static_assert(m_scaled(0, 0) == 1.0f && m_scaled(0, 2) == 3.0f);
static_assert(m_scaled(1, 0) == 2.0f && m_scaled(1, 1) == 2.5f && m_scaled(1, 2) == 3.0f);

// add_scaled_row: row[1] -= 4 * row[0] kills the leading 4 in row[1].
constexpr auto m_axpy = []{
	opsy::utility::matrix<2, 3> m{
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f
	};
	m.add_scaled_row(0, 1, -4.0f);
	return m;
}();
static_assert(m_axpy(1, 0) == 0.0f);
static_assert(m_axpy(1, 1) == -3.0f);
static_assert(m_axpy(1, 2) == -6.0f);
static_assert(m_axpy(0, 0) == 1.0f);  // row 0 unchanged

// sub_matrix: extract a 2x2 block at (1, 1) from a 3x3.
constexpr opsy::utility::matrix<3, 3> m_for_sub{
	1.0f, 2.0f, 3.0f,
	4.0f, 5.0f, 6.0f,
	7.0f, 8.0f, 9.0f
};
constexpr auto m_block = m_for_sub.sub_matrix<2, 2>(1, 1);
static_assert(m_block.row_count == 2 && m_block.column_count == 2);
static_assert(m_block(0, 0) == 5.0f && m_block(0, 1) == 6.0f);
static_assert(m_block(1, 0) == 8.0f && m_block(1, 1) == 9.0f);
// Whole-matrix sub == itself.
static_assert(m_for_sub.sub_matrix<3, 3>(0, 0) == m_for_sub);
// 1x1 view of a single element.
static_assert(m_for_sub.sub_matrix<1, 1>(2, 0)(0, 0) == 7.0f);

// apply: square every element in place.
constexpr auto m_squared = []{
	opsy::utility::matrix<2, 2> m{1.0f, 2.0f, 3.0f, 4.0f};
	m.apply([](float x) { return x * x; });
	return m;
}();
static_assert(m_squared == opsy::utility::matrix<2, 2>{1.0f, 4.0f, 9.0f, 16.0f});

[[gnu::used]] void use_matrix()
{
	using opsy::utility::matrix;
	using opsy::utility::identity_matrix;

	matrix<3, 3> m{
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f
	};
	m += opsy::utility::identity_matrix<3>();
	m -= opsy::utility::identity_matrix<3>();
	m *= 2.0f;
	m /= 2.0f;
	(void) m.row(0);
	(void) m.col(1);

	// Eigen decomposition — runtime only (uses sqrt / hypot, neither
	// constexpr in C++23).
	//
	// 2x2 with eigenvalues 1 and 3, identity-ish path.
	const matrix<2, 2> m_sym2{
		2.0f, 1.0f,
		1.0f, 2.0f
	};
	(void) m_sym2.eigenvalues();
	auto eig2 = m_sym2.symmetric_eigen_decomposition();
	(void) eig2.values;
	(void) eig2.vectors;

	// 3x3 symmetric — diagonal-dominant case to keep convergence cheap.
	const matrix<3, 3> m_sym3{
		2.0f, 1.0f, 0.0f,
		1.0f, 2.0f, 1.0f,
		0.0f, 1.0f, 2.0f
	};
	(void) m_sym3.eigenvalues();
	auto eig3 = m_sym3.symmetric_eigen_decomposition();
	(void) eig3.values;

	// Diagonal matrix — eigenvalues are the diagonal entries (sorted).
	const matrix<4, 4> m_diag{
		3.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 4.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 2.0f
	};
	auto eig4 = m_diag.symmetric_eigen_decomposition();
	(void) eig4.values;
	(void) eig4.vectors;

	// 5x5 — exercises a size where the algorithm runs more than a couple
	// QL iterations.
	const matrix<5, 5> m_sym5{
		4.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		1.0f, 4.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 4.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 4.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f, 4.0f
	};
	(void) m_sym5.eigenvalues();
}

// ============================== quaternion ==============================
// hamilton_product, conjugate, inverse and rotate(v3, q) are constexpr
// (no transcendental). from_axis_angle and slerp call sin/cos/acos so
// they only get exercised at runtime via [[gnu::used]].

constexpr auto q_id = opsy::utility::identity_quaternion<float>();
static_assert(q_id.x() == 0.0f && q_id.y() == 0.0f && q_id.z() == 0.0f && q_id.w() == 1.0f);

// hamilton(identity, q) == q  and  hamilton(q, identity) == q.
constexpr opsy::utility::quaternion<float> q_arb{0.1f, 0.2f, 0.3f, 0.9f};
constexpr auto q_left  = opsy::utility::hamilton_product(q_id, q_arb);
constexpr auto q_right = opsy::utility::hamilton_product(q_arb, q_id);
static_assert(q_left  == q_arb);
static_assert(q_right == q_arb);

// conjugate negates xyz, leaves w.
constexpr auto q_conj = opsy::utility::conjugate(q_arb);
static_assert(q_conj.x() == -0.1f);
static_assert(q_conj.y() == -0.2f);
static_assert(q_conj.z() == -0.3f);
static_assert(q_conj.w() ==  0.9f);
// involution
static_assert(opsy::utility::conjugate(q_conj) == q_arb);

// 180° rotation around z is the unit quaternion (0, 0, 1, 0).
// rotate((1, 0, 0), q_180_z) must give (-1, 0, 0).
constexpr opsy::utility::quaternion<float> q_180_z{0.0f, 0.0f, 1.0f, 0.0f};
constexpr opsy::utility::vector<3> v_x{1.0f, 0.0f, 0.0f};
constexpr auto v_rotated = opsy::utility::rotate(v_x, q_180_z);
static_assert(v_rotated.x() == -1.0f);
static_assert(v_rotated.y() ==  0.0f);
static_assert(v_rotated.z() ==  0.0f);

// rotate(v, identity) returns v unchanged.
constexpr auto v_unchanged = opsy::utility::rotate(v_x, q_id);
static_assert(v_unchanged == v_x);

// operator* equivalents — same numeric result as the named free functions.
static_assert((q_id * q_arb)   == q_arb);
static_assert((q_180_z * v_x) == v_rotated);

// Element-wise ops on the underlying vector.
static_assert((q_arb + q_arb).x() == 0.2f);
static_assert((q_arb - q_arb).w() == 0.0f);
static_assert((-q_arb).x()        == -0.1f);
static_assert((q_arb * 2.0f).w()  == 1.8f);
static_assert((2.0f * q_arb).w()  == 1.8f);
static_assert((q_arb / 2.0f).w()  == 0.45f);

[[gnu::used]] void use_quaternion()
{
	using opsy::utility::quaternion;
	using opsy::utility::from_axis_angle;
	using opsy::utility::inverse;
	using opsy::utility::slerp;

	// from_axis_angle (sin/cos — runtime only).
	const opsy::utility::vector<3> z_axis{0.0f, 0.0f, 1.0f};
	auto q1 = from_axis_angle(z_axis, 1.5707963f);   // 90° around z
	auto q2 = from_axis_angle(z_axis, 3.1415926f);   // 180° around z

	// Composition the operator way (gyroscope-style):
	// orientation = orientation * delta.
	auto orientation = q1;
	orientation = orientation * q2;
	orientation.normalize();

	// Rotation of a 3D vector via operator*.
	const opsy::utility::vector<3> v{1.0f, 2.0f, 3.0f};
	(void) (orientation * v);

	// Inverse on a non-unit quaternion exercises the divide-by-norm path.
	auto q_inv = inverse(quaternion<float>{1.0f, 2.0f, 3.0f, 4.0f});
	(void) q_inv;

	// Slerp at the endpoints, in the middle, and on a near-aligned pair
	// to hit the lerp fall-back.
	(void) slerp(q1, q2, 0.0f);
	(void) slerp(q1, q2, 0.5f);
	(void) slerp(q1, q2, 1.0f);
	(void) slerp(q1, q1 + quaternion<float>{1e-5f, 0.0f, 0.0f, 0.0f}, 0.5f);

	// Rotation-vector ctor: axis * angle. Zero vector hits the identity path.
	auto q_from_rotvec_zero = quaternion<float>{opsy::utility::vector<3>{0.0f, 0.0f, 0.0f}};
	(void) q_from_rotvec_zero;
	auto q_from_rotvec_z90  = quaternion<float>{opsy::utility::vector<3>{0.0f, 0.0f, 1.5707963f}};
	(void) q_from_rotvec_z90;

	// Shortest-arc ctor: nominal (90° from x to y), antiparallel (x to -x).
	const opsy::utility::vector<3> ux{ 1.0f,  0.0f, 0.0f};
	const opsy::utility::vector<3> uy{ 0.0f,  1.0f, 0.0f};
	const opsy::utility::vector<3> uxn{-1.0f, 0.0f, 0.0f};
	auto q_arc      = quaternion<float>{ux, uy};
	auto q_arc_anti = quaternion<float>{ux, uxn};
	(void) q_arc;
	(void) q_arc_anti;

	// Tait-Bryan extraction. Pure rotation around each axis isolates one angle.
	(void) q1.roll();
	(void) q1.pitch();
	(void) q1.yaw();

	auto q_pitch = from_axis_angle(opsy::utility::vector<3>{0.0f, 1.0f, 0.0f}, 0.5f);
	(void) q_pitch.pitch();

	// Pitch clamp at the singularity: build a quaternion that puts the
	// pitch right at the edge so asin would otherwise fall outside [-1,1].
	auto q_gimbal = quaternion<float>{0.0f, std::sin(0.7853981f), 0.0f, std::cos(0.7853981f)};  // 90° around y
	(void) q_gimbal.pitch();
}

// ================================ memory ================================
// memory<TInt, TReg> expects TReg to be a strongly-typed register-field
// wrapper of the same size as TInt with a value() accessor and the bitwise
// operators expected by the read-modify-write paths (|=, &=, ^=, <<=).
// The full set of those types is generated by the BSP for each peripheral.
// For CI purposes here we just need a minimal mock so the templates get
// instantiated and the bodies type-check.

struct mock_reg
{
	uint32_t v_;
	// Real generated register-field types expose `mask` so the
	// `T value || atomic` overload of memory.hpp can route through it.
	// We don't define operator|| on mock_reg, so that path is not
	// exercised here — keep the member to document the contract and
	// silence -Wunused-const-variable with [[maybe_unused]].
	[[maybe_unused]] static constexpr uint32_t mask = 0xFFFFFFFFu;

	constexpr mock_reg(uint32_t value = 0) : v_{value} {}
	constexpr uint32_t value() const { return v_; }
	constexpr operator uint32_t() const { return v_; }
	constexpr mock_reg operator|(mock_reg o) const { return mock_reg{ v_ | o.v_ }; }
	constexpr mock_reg operator&(mock_reg o) const { return mock_reg{ v_ & o.v_ }; }
	constexpr mock_reg operator^(mock_reg o) const { return mock_reg{ v_ ^ o.v_ }; }
	constexpr mock_reg operator~() const { return mock_reg{ ~v_ }; }
};

[[gnu::used]] opsy::utility::memory<uint32_t, mock_reg>            g_mem_rw;
[[gnu::used]] opsy::utility::read_only_memory<uint32_t, mock_reg>  g_mem_ro;
[[gnu::used]] opsy::utility::write_only_memory<uint32_t, mock_reg> g_mem_wo;
[[gnu::used]] opsy::utility::padding<4>                            g_pad;

[[gnu::used]] void use_memory()
{
	using opsy::utility::atomic;
	using opsy::utility::clear_set;

	g_mem_rw = mock_reg{1};
	(void) g_mem_rw.load();
	(void) static_cast<mock_reg>(g_mem_rw);

	g_mem_rw |= mock_reg{0x0F};
	g_mem_rw &= mock_reg{0xF0};
	g_mem_rw ^= mock_reg{0xFF};
	g_mem_rw <<= clear_set<mock_reg>{ mock_reg{0x0F}, mock_reg{0xF0} };

	g_mem_rw |= mock_reg{0x0F} | atomic;
	g_mem_rw &= mock_reg{0xF0} | atomic;
	g_mem_rw ^= mock_reg{0xFF} | atomic;
	g_mem_rw <<= clear_set<mock_reg>{ mock_reg{0x0F}, mock_reg{0xF0} } || atomic;

	(void) g_mem_ro.load();
	g_mem_wo = mock_reg{0x42};
}

}
