/**
 ******************************************************************************
 * @file    matrix.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   A fixed-size mathematical matrix
 *
 *          Compile-time-sized full matrix targeted at the small dimensions
 *          common in embedded math (typically up to 4x4): rotations,
 *          covariance blocks of small Kalman filters, etc.
 *
 *          Storage is row-major: @c values_[i][j] is the element at row @c i
 *          column @c j . Element access through @c m(i, @c j) follows the
 *          mathematical @c m_{ij} convention.
 *
 *          This first iteration covers the basic algebra: construction,
 *          element / row / column access, equality, addition, subtraction,
 *          unary minus, scalar multiplication and division, transposition,
 *          matrix-matrix multiplication, matrix-vector multiplication, and
 *          the identity factory. Determinant, inverse, decompositions etc.
 *          will come in follow-up iterations.
 *
 ******************************************************************************
 * @copyright Copyright 2026 Thomas Legrand under the MIT License
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

#include "../opsy_assert.hpp"
#include "vector.hpp"

namespace opsy::utility
{

/**
 * @brief Fixed-size full matrix, row-major storage.
 *
 * @tparam Rows Number of rows (must be > 0).
 * @tparam Cols Number of columns (must be > 0).
 * @tparam T    Element type (default @c float ).
 */
template<std::size_t Rows, std::size_t Cols, typename T = float>
class matrix
{
	static_assert(Rows != 0, "matrix must have at least one row");
	static_assert(Cols != 0, "matrix must have at least one column");

public:

	using value_type = T;
	static constexpr std::size_t row_count    = Rows;
	static constexpr std::size_t column_count = Cols;

	/** Zero matrix. */
	constexpr matrix() : values_{} {}

	/**
	 * @brief Build from a flat row-major list of @c Rows * @c Cols values.
	 *
	 *        @code
	 *        matrix<2, 3, float> m{ 1, 2, 3,
	 *                               4, 5, 6 };
	 *        @endcode
	 */
	template<typename... Args>
		requires (sizeof...(Args) == Rows * Cols)
		      && (std::convertible_to<Args, T> && ...)
	constexpr matrix(Args... args) : values_{}
	{
		const T flat[]{ static_cast<T>(args)... };
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] = flat[i * Cols + j];
	}

	/** Wrap an existing nested array. */
	explicit constexpr matrix(const std::array<std::array<T, Cols>, Rows>& v) : values_{v} {}

	// ── Element access ───────────────────────────────────────────────────

	constexpr const T& operator()(std::size_t row, std::size_t col) const { return values_[row][col]; }
	constexpr       T& operator()(std::size_t row, std::size_t col)       { return values_[row][col]; }

	template<std::size_t Row, std::size_t Col>
	constexpr const T& at() const
	{
		static_assert(Row < Rows, "row index out of bounds");
		static_assert(Col < Cols, "column index out of bounds");
		return values_[Row][Col];
	}

	template<std::size_t Row, std::size_t Col>
	constexpr T& at()
	{
		static_assert(Row < Rows, "row index out of bounds");
		static_assert(Col < Cols, "column index out of bounds");
		return values_[Row][Col];
	}

	/** Returns the @p i -th row as a @c vector<Cols, T> (by value). */
	constexpr vector<Cols, T> row(std::size_t i) const
	{
		return vector<Cols, T>(values_[i]);
	}

	/** Returns the @p j -th column as a @c vector<Rows, T> (by value). */
	constexpr vector<Rows, T> col(std::size_t j) const
	{
		vector<Rows, T> result;
		for (std::size_t i = 0; i < Rows; ++i)
			result[i] = values_[i][j];
		return result;
	}

	// ── Equality ─────────────────────────────────────────────────────────

	constexpr bool operator==(const matrix&) const = default;

	// ── Element-wise arithmetic ──────────────────────────────────────────

	constexpr matrix operator+(const matrix& other) const
	{
		matrix result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result.values_[i][j] = values_[i][j] + other.values_[i][j];
		return result;
	}

	constexpr matrix operator-(const matrix& other) const
	{
		matrix result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result.values_[i][j] = values_[i][j] - other.values_[i][j];
		return result;
	}

	constexpr matrix operator-() const
	{
		matrix result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result.values_[i][j] = -values_[i][j];
		return result;
	}

	constexpr matrix operator*(const T& factor) const
	{
		matrix result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result.values_[i][j] = values_[i][j] * factor;
		return result;
	}

	constexpr matrix operator/(const T& factor) const
	{
		matrix result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result.values_[i][j] = values_[i][j] / factor;
		return result;
	}

	constexpr matrix& operator+=(const matrix& other)
	{
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] += other.values_[i][j];
		return *this;
	}

	constexpr matrix& operator-=(const matrix& other)
	{
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] -= other.values_[i][j];
		return *this;
	}

	constexpr matrix& operator*=(const T& factor)
	{
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] *= factor;
		return *this;
	}

	constexpr matrix& operator/=(const T& factor)
	{
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] /= factor;
		return *this;
	}

	// ── Transpose ────────────────────────────────────────────────────────

	/**
	 * @brief Returns the transpose of this matrix.
	 *
	 *        Always works (returns a @c matrix<Cols, Rows, T> ); the in-place
	 *        form is reserved for square matrices and not provided yet.
	 */
	constexpr matrix<Cols, Rows, T> transposed() const
	{
		matrix<Cols, Rows, T> result;
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				result(j, i) = values_[i][j];
		return result;
	}

	// ── Row operations (in place) ────────────────────────────────────────
	//
	// Building blocks for Gaussian elimination and similar algorithms.
	// Names follow BLAS conventions ( @c scale_row , @c add_scaled_row )
	// so the intent reads at a glance to anyone with linear algebra
	// background.

	/**
	 * @brief Swap rows @p i and @p j in place.
	 *
	 *        No-op when @p i == @p j .
	 */
	constexpr void swap_rows(std::size_t i, std::size_t j)
	{
		if (i == j)
			return;
		std::swap(values_[i], values_[j]);
	}

	/**
	 * @brief Multiply every element of row @p i by @p factor in place.
	 */
	constexpr void scale_row(std::size_t i, const T& factor)
	{
		for (std::size_t j = 0; j < Cols; ++j)
			values_[i][j] *= factor;
	}

	/**
	 * @brief Add @p factor * row[ @p src ] to row[ @p dst ] in place.
	 *
	 *        This is the @c daxpy step on a single row pair: the third row
	 *        operation needed by Gauss-Jordan elimination.
	 */
	constexpr void add_scaled_row(std::size_t src, std::size_t dst, const T& factor)
	{
		for (std::size_t j = 0; j < Cols; ++j)
			values_[dst][j] += factor * values_[src][j];
	}

	// ── Sub-matrix extraction ────────────────────────────────────────────

	/**
	 * @brief Returns the @c SubRows x @c SubCols block starting at row
	 *        @p top and column @p left.
	 *
	 *        The block dimensions are template parameters so the result has
	 *        a fixed type known at compile time. Out-of-bounds offsets are
	 *        the caller's responsibility (no run-time check; @c top + @c
	 *        SubRows must be @c <= Rows and similarly for the columns).
	 */
	template<std::size_t SubRows, std::size_t SubCols>
	constexpr matrix<SubRows, SubCols, T> sub_matrix(std::size_t top, std::size_t left) const
	{
		static_assert(SubRows <= Rows, "sub_matrix row count exceeds source");
		static_assert(SubCols <= Cols, "sub_matrix column count exceeds source");
		matrix<SubRows, SubCols, T> result;
		for (std::size_t i = 0; i < SubRows; ++i)
			for (std::size_t j = 0; j < SubCols; ++j)
				result(i, j) = values_[top + i][left + j];
		return result;
	}

	// ── Element-wise function application ────────────────────────────────

	/**
	 * @brief Apply @p f to every element in place.
	 *
	 *        @p f must be invocable with @c T and return something
	 *        convertible to @c T. Equivalent to @c std::transform(begin,
	 *        end, begin, f) over a flat view of the storage.
	 */
	template<typename F>
		requires std::invocable<F&, T>
		      && std::convertible_to<std::invoke_result_t<F&, T>, T>
	constexpr void apply(F&& f)
	{
		for (std::size_t i = 0; i < Rows; ++i)
			for (std::size_t j = 0; j < Cols; ++j)
				values_[i][j] = static_cast<T>(f(values_[i][j]));
	}

	// ── Determinant (square matrices, sizes 1 to 4) ──────────────────────

	/**
	 * @brief Returns the determinant of this square matrix.
	 *
	 *        Hardcoded for sizes 1 through 4 (the dimensions this header
	 *        targets). Larger matrices are intentionally not supported yet
	 *        — a follow-up iteration can plug in cofactor expansion or LU
	 *        decomposition if the need arises.
	 */
	[[gnu::always_inline]] constexpr T determinant() const requires (Rows == Cols)
	{
		static_assert(Rows < 5, "determinant only implemented for 1x1 through 4x4 matrices");

		if constexpr (Rows == 1)
		{
			return values_[0][0];
		}
		else if constexpr (Rows == 2)
		{
			return values_[0][0] * values_[1][1] - values_[0][1] * values_[1][0];
		}
		else if constexpr (Rows == 3)
		{
			const T& a = values_[0][0]; const T& b = values_[0][1]; const T& c = values_[0][2];
			const T& d = values_[1][0]; const T& e = values_[1][1]; const T& f = values_[1][2];
			const T& g = values_[2][0]; const T& h = values_[2][1]; const T& i = values_[2][2];
			return a * (e*i - f*h) - b * (d*i - f*g) + c * (d*h - e*g);
		}
		else if constexpr (Rows == 4)
		{
			const T& m00 = values_[0][0]; const T& m01 = values_[0][1]; const T& m02 = values_[0][2]; const T& m03 = values_[0][3];
			const T& m10 = values_[1][0]; const T& m11 = values_[1][1]; const T& m12 = values_[1][2]; const T& m13 = values_[1][3];
			const T& m20 = values_[2][0]; const T& m21 = values_[2][1]; const T& m22 = values_[2][2]; const T& m23 = values_[2][3];
			const T& m30 = values_[3][0]; const T& m31 = values_[3][1]; const T& m32 = values_[3][2]; const T& m33 = values_[3][3];

			// 2x2 sub-determinants of rows 2,3 — shared between every cofactor of row 0.
			const T s01 = m20*m31 - m21*m30;
			const T s02 = m20*m32 - m22*m30;
			const T s03 = m20*m33 - m23*m30;
			const T s12 = m21*m32 - m22*m31;
			const T s13 = m21*m33 - m23*m31;
			const T s23 = m22*m33 - m23*m32;

			// Laplace expansion along row 0.
			return m00 * (m11*s23 - m12*s13 + m13*s12)
			     - m01 * (m10*s23 - m12*s03 + m13*s02)
			     + m02 * (m10*s13 - m11*s03 + m13*s01)
			     - m03 * (m10*s12 - m11*s02 + m12*s01);
		}
	}

	// ── Inverse (square matrices, sizes 1 to 4) ──────────────────────────

	/**
	 * @brief Returns the inverse of this square matrix.
	 *
	 *        Two implementations are dispatched on size:
	 *         - For @c Rows @c <= @c 4 , a closed-form @c adjugate(M) /
	 *           @c determinant(M) . The 4x4 path shares the six 2x2
	 *           sub-determinants of every row pair across the 16
	 *           cofactors, which keeps the operation count near 140 mul /
	 *           70 add — about half what an unshared expansion would cost.
	 *           Branchless and fully unrolled, so the compiler can
	 *           pipeline aggressively.
	 *         - For @c Rows @c >= @c 5 , Gauss-Jordan elimination with
	 *           partial pivoting on a working copy. Closed-form would
	 *           explode factorially (5! terms on 5x5) and lose to the
	 *           N^3 generic algorithm well before that.
	 *
	 * @pre   The matrix must be invertible — @c determinant() must be
	 *        non-zero. The result is undefined (NaN / Inf elements) on a
	 *        singular matrix; callers that may face singular input should
	 *        check @ref determinant first (closed-form sizes) or guard
	 *        the input some other way.
	 */
	[[gnu::always_inline]] constexpr matrix inverse() const requires (Rows == Cols)
	{
		if constexpr (Rows == 1)
		{
			return matrix{ T{1} / values_[0][0] };
		}
		else if constexpr (Rows == 2)
		{
			const T inv_det = T{1} / determinant();
			return matrix{
				 values_[1][1] * inv_det, -values_[0][1] * inv_det,
				-values_[1][0] * inv_det,  values_[0][0] * inv_det
			};
		}
		else if constexpr (Rows == 3)
		{
			const T& a = values_[0][0]; const T& b = values_[0][1]; const T& c = values_[0][2];
			const T& d = values_[1][0]; const T& e = values_[1][1]; const T& f = values_[1][2];
			const T& g = values_[2][0]; const T& h = values_[2][1]; const T& i = values_[2][2];

			// Cofactors with sign already applied. The inverse layout is
			// the cofactor matrix transposed (= adjugate) divided by det.
			const T cof00 = e*i - f*h;
			const T cof01 = f*g - d*i;
			const T cof02 = d*h - e*g;
			const T cof10 = c*h - b*i;
			const T cof11 = a*i - c*g;
			const T cof12 = b*g - a*h;
			const T cof20 = b*f - c*e;
			const T cof21 = c*d - a*f;
			const T cof22 = a*e - b*d;

			const T inv_det = T{1} / (a*cof00 + b*cof01 + c*cof02);

			return matrix{
				cof00 * inv_det, cof10 * inv_det, cof20 * inv_det,
				cof01 * inv_det, cof11 * inv_det, cof21 * inv_det,
				cof02 * inv_det, cof12 * inv_det, cof22 * inv_det
			};
		}
		else if constexpr (Rows == 4)
		{
			const T& m00 = values_[0][0]; const T& m01 = values_[0][1]; const T& m02 = values_[0][2]; const T& m03 = values_[0][3];
			const T& m10 = values_[1][0]; const T& m11 = values_[1][1]; const T& m12 = values_[1][2]; const T& m13 = values_[1][3];
			const T& m20 = values_[2][0]; const T& m21 = values_[2][1]; const T& m22 = values_[2][2]; const T& m23 = values_[2][3];
			const T& m30 = values_[3][0]; const T& m31 = values_[3][1]; const T& m32 = values_[3][2]; const T& m33 = values_[3][3];

			// 2x2 sub-determinants of rows 2,3 — shared between every cofactor of rows 0 and 1.
			const T s23_01 = m20*m31 - m21*m30;
			const T s23_02 = m20*m32 - m22*m30;
			const T s23_03 = m20*m33 - m23*m30;
			const T s23_12 = m21*m32 - m22*m31;
			const T s23_13 = m21*m33 - m23*m31;
			const T s23_23 = m22*m33 - m23*m32;

			// 2x2 sub-determinants of rows 1,3 — shared between every cofactor of row 2.
			const T s13_01 = m10*m31 - m11*m30;
			const T s13_02 = m10*m32 - m12*m30;
			const T s13_03 = m10*m33 - m13*m30;
			const T s13_12 = m11*m32 - m12*m31;
			const T s13_13 = m11*m33 - m13*m31;
			const T s13_23 = m12*m33 - m13*m32;

			// 2x2 sub-determinants of rows 1,2 — shared between every cofactor of row 3.
			const T s12_01 = m10*m21 - m11*m20;
			const T s12_02 = m10*m22 - m12*m20;
			const T s12_03 = m10*m23 - m13*m20;
			const T s12_12 = m11*m22 - m12*m21;
			const T s12_13 = m11*m23 - m13*m21;
			const T s12_23 = m12*m23 - m13*m22;

			// Adjugate = cofactor matrix transposed. We build the 16 entries
			// directly in transposed positions so the final result is just
			// "adj * inv_det".
			const T adj00 =  (m11*s23_23 - m12*s23_13 + m13*s23_12);
			const T adj10 = -(m10*s23_23 - m12*s23_03 + m13*s23_02);
			const T adj20 =  (m10*s23_13 - m11*s23_03 + m13*s23_01);
			const T adj30 = -(m10*s23_12 - m11*s23_02 + m12*s23_01);

			const T adj01 = -(m01*s23_23 - m02*s23_13 + m03*s23_12);
			const T adj11 =  (m00*s23_23 - m02*s23_03 + m03*s23_02);
			const T adj21 = -(m00*s23_13 - m01*s23_03 + m03*s23_01);
			const T adj31 =  (m00*s23_12 - m01*s23_02 + m02*s23_01);

			const T adj02 =  (m01*s13_23 - m02*s13_13 + m03*s13_12);
			const T adj12 = -(m00*s13_23 - m02*s13_03 + m03*s13_02);
			const T adj22 =  (m00*s13_13 - m01*s13_03 + m03*s13_01);
			const T adj32 = -(m00*s13_12 - m01*s13_02 + m02*s13_01);

			const T adj03 = -(m01*s12_23 - m02*s12_13 + m03*s12_12);
			const T adj13 =  (m00*s12_23 - m02*s12_03 + m03*s12_02);
			const T adj23 = -(m00*s12_13 - m01*s12_03 + m03*s12_01);
			const T adj33 =  (m00*s12_12 - m01*s12_02 + m02*s12_01);

			// det = expansion along row 0 = sum_j m[0][j] * cofactor[0][j].
			//     The cofactor[0][j] sits at adj[j][0] (because adj = cof^T).
			const T inv_det = T{1} / (m00*adj00 + m01*adj10 + m02*adj20 + m03*adj30);

			return matrix{
				adj00 * inv_det, adj01 * inv_det, adj02 * inv_det, adj03 * inv_det,
				adj10 * inv_det, adj11 * inv_det, adj12 * inv_det, adj13 * inv_det,
				adj20 * inv_det, adj21 * inv_det, adj22 * inv_det, adj23 * inv_det,
				adj30 * inv_det, adj31 * inv_det, adj32 * inv_det, adj33 * inv_det
			};
		}
		else
		{
			// Gauss-Jordan with partial pivoting. We keep two NxN working
			// matrices side by side: @c work starts as a copy of @c *this and
			// gets reduced to the identity by row operations; @c result starts
			// as the identity and accumulates the inverse of the same row
			// operations. When @c work has been reduced to I, @c result holds
			// M⁻¹.
			matrix work   = *this;
			matrix result;  // built as identity below; @c identity_matrix free function isn't visible from inside the class body yet.
			for (std::size_t k = 0; k < Rows; ++k)
				result(k, k) = T{1};

			for (std::size_t i = 0; i < Rows; ++i)
			{
				// Partial pivot: find the row in [i, Rows) with the largest
				// |work(r, i)| and swap it into position i. Pivoting on a
				// near-zero diagonal element would amplify rounding error
				// (or trigger division by zero on a singular matrix).
				std::size_t pivot_row = i;
				T pivot_abs = work(i, i) < T{0} ? -work(i, i) : work(i, i);
				for (std::size_t r = i + 1; r < Rows; ++r)
				{
					const T abs_val = work(r, i) < T{0} ? -work(r, i) : work(r, i);
					if (abs_val > pivot_abs)
					{
						pivot_abs = abs_val;
						pivot_row = r;
					}
				}
				if (pivot_row != i)
				{
					work.swap_rows(i, pivot_row);
					result.swap_rows(i, pivot_row);
				}

				// Normalise pivot row so the diagonal becomes 1.
				const T inv_pivot = T{1} / work(i, i);
				work.scale_row(i, inv_pivot);
				result.scale_row(i, inv_pivot);

				// Eliminate column i in every other row.
				for (std::size_t r = 0; r < Rows; ++r)
				{
					if (r == i)
						continue;
					const T factor = -work(r, i);
					work.add_scaled_row(i, r, factor);
					result.add_scaled_row(i, r, factor);
				}
			}

			return result;
		}
	}

	// ── Symmetric eigen decomposition ────────────────────────────────────
	//
	// Eigenvalues + eigenvectors of a real symmetric matrix via a
	// Householder tridiagonalisation followed by QL with implicit shifts
	// — the textbook two-pass algorithm shipped by EISPACK / JAMA. Both
	// entry points sort the eigenvalues in ascending order; the columns
	// of the returned @c vectors matrix line up with @c values .
	//
	// The methods abort via @c assert(M @c == @c M^T) when given a
	// non-symmetric matrix: the algorithm assumes symmetry and would
	// silently produce nonsense otherwise. The check is exact on float
	// equality, which is fine for matrices built symmetrically (e.g.
	// @c DᵀD ); if you accumulate symmetric updates with rounding, build
	// the matrix once symmetrically before calling.

	/** Return type of @ref symmetric_eigen_decomposition . */
	struct eigen_decomposition_t
	{
		vector<Rows, T>       values;   // ascending order
		matrix<Rows, Rows, T> vectors;  // column @c i is the eigenvector for @c values[i]
	};

	/**
	 * @brief Returns the eigenvalues of this real symmetric matrix in
	 *        ascending order.
	 *
	 * @pre   The matrix must be symmetric (asserted in debug).
	 */
	vector<Rows, T> eigenvalues() const requires (Rows == Cols && Rows >= 2);

	/**
	 * @brief Returns the eigenvalues and eigenvectors of this real
	 *        symmetric matrix.
	 *
	 *        Eigenvalues are sorted ascending; column @c i of @c vectors
	 *        is the unit eigenvector for @c values[i] . By construction
	 *        @c vectors is orthogonal, so the original matrix can be
	 *        reconstructed as @c vectors @c * @c diag(values) @c *
	 *        @c vectors.transposed() (within float tolerance).
	 *
	 * @pre   The matrix must be symmetric (asserted in debug).
	 */
	eigen_decomposition_t symmetric_eigen_decomposition() const requires (Rows == Cols && Rows >= 2);

private:

	std::array<std::array<T, Cols>, Rows> values_;
};

/** Scalar-on-the-left multiplication. */
template<std::size_t R, std::size_t C, typename T>
constexpr matrix<R, C, T> operator*(const T& factor, const matrix<R, C, T>& m)
{
	return m * factor;
}

/**
 * @brief Matrix-matrix multiplication: @c <R, K> * @c <K, C> -> @c <R, C> .
 *
 * @remark Marked @c [[gnu::always_inline]] so it stays inlined at @c -Og .
 *         For the small dimensions this header targets (typically up to
 *         4x4), the inner loop is well above the conservative @c -Og
 *         "small function" threshold but still trivial to inline; without
 *         the hint, hot loops would pay a real call per multiplication.
 *         At @c -O2 the attribute is a no-op (the function would be
 *         inlined anyway).
 */
template<std::size_t R, std::size_t K, std::size_t C, typename T>
[[gnu::always_inline]] constexpr matrix<R, C, T> operator*(const matrix<R, K, T>& a, const matrix<K, C, T>& b)
{
	matrix<R, C, T> result;
	for (std::size_t i = 0; i < R; ++i)
		for (std::size_t j = 0; j < C; ++j)
		{
			T sum = T();
			for (std::size_t k = 0; k < K; ++k)
				sum += a(i, k) * b(k, j);
			result(i, j) = sum;
		}
	return result;
}

/**
 * @brief Matrix-vector multiplication: @c <R, C> * @c vector<C> -> @c vector<R> .
 *
 *        Treats the right-hand side as a column vector and returns the
 *        transformed column vector @c M * v .
 */
template<std::size_t R, std::size_t C, typename T>
[[gnu::always_inline]] constexpr vector<R, T> operator*(const matrix<R, C, T>& m, const vector<C, T>& v)
{
	vector<R, T> result;
	for (std::size_t i = 0; i < R; ++i)
	{
		T sum = T();
		for (std::size_t j = 0; j < C; ++j)
			sum += m(i, j) * v[j];
		result[i] = sum;
	}
	return result;
}

// ── Symmetric eigen decomposition implementation ────────────────────────
//
// Ported from JAMA / EISPACK ( @c tred2 + @c tql2 ). Two passes:
//
//   1. @ref detail::tred2 reduces the symmetric matrix @c V to tridiagonal
//      form by chained Householder reflections. After the call, the main
//      diagonal sits in @c d , the off-diagonal in @c e (with @c e[0]
//      unused — slot kept for indexing convenience), and @c V holds the
//      orthogonal accumulator @c Q such that @c QᵀMQ is tridiagonal.
//
//   2. @ref detail::tql2 runs QL with implicit Wilkinson shifts on the
//      tridiagonal pair @c (d, @c e), composing the rotations into @c V .
//      On exit @c d holds the eigenvalues and the columns of @c V hold
//      the matching eigenvectors. A final pass sorts both ascending.
//
// The algorithm is iterative (QL converges quadratically near the
// eigenvalues) and uses @c std::sqrt / @c std::hypot, so neither helper
// can be @c constexpr in C++23 — @c std::sqrt only became @c constexpr in
// C++26. Both helpers are therefore plain functions, exercised at
// runtime by the sanity test.
namespace detail
{

template<std::size_t N, typename T>
void tred2(matrix<N, N, T>& V, vector<N, T>& d, vector<N, T>& e)
{
	for (std::size_t j = 0; j < N; ++j)
		d[j] = V(N - 1, j);

	// Householder reduction (i goes from N-1 down to 1, inclusive).
	for (std::size_t i = N - 1; i > 0; --i)
	{
		T scale = T{0};
		T h     = T{0};
		for (std::size_t k = 0; k < i; ++k)
			scale += d[k] < T{0} ? -d[k] : d[k];

		if (scale == T{0})
		{
			e[i] = d[i - 1];
			for (std::size_t j = 0; j < i; ++j)
			{
				d[j]    = V(i - 1, j);
				V(i, j) = T{0};
				V(j, i) = T{0};
			}
		}
		else
		{
			// Generate Householder vector.
			for (std::size_t k = 0; k < i; ++k)
			{
				d[k] /= scale;
				h += d[k] * d[k];
			}
			T f = d[i - 1];
			T g = std::sqrt(h);
			if (f > T{0})
				g = -g;
			e[i]     = scale * g;
			h        -= f * g;
			d[i - 1] = f - g;
			for (std::size_t j = 0; j < i; ++j)
				e[j] = T{0};

			// Apply similarity transformation to remaining columns.
			for (std::size_t j = 0; j < i; ++j)
			{
				f       = d[j];
				V(j, i) = f;
				g       = e[j] + V(j, j) * f;
				for (std::size_t k = j + 1; k < i; ++k)
				{
					g    += V(k, j) * d[k];
					e[k] += V(k, j) * f;
				}
				e[j] = g;
			}
			f = T{0};
			for (std::size_t j = 0; j < i; ++j)
			{
				e[j] /= h;
				f    += e[j] * d[j];
			}
			const T hh = f / (h + h);
			for (std::size_t j = 0; j < i; ++j)
				e[j] -= hh * d[j];
			for (std::size_t j = 0; j < i; ++j)
			{
				f = d[j];
				g = e[j];
				for (std::size_t k = j; k < i; ++k)
					V(k, j) -= f * e[k] + g * d[k];
				d[j]    = V(i - 1, j);
				V(i, j) = T{0};
			}
		}
		d[i] = h;
	}

	// Accumulate transformations into V (V <- Q).
	for (std::size_t i = 0; i + 1 < N; ++i)
	{
		V(N - 1, i) = V(i, i);
		V(i, i)     = T{1};
		const T h   = d[i + 1];
		if (h != T{0})
		{
			for (std::size_t k = 0; k <= i; ++k)
				d[k] = V(k, i + 1) / h;
			for (std::size_t j = 0; j <= i; ++j)
			{
				T g = T{0};
				for (std::size_t k = 0; k <= i; ++k)
					g += V(k, i + 1) * V(k, j);
				for (std::size_t k = 0; k <= i; ++k)
					V(k, j) -= g * d[k];
			}
		}
		for (std::size_t k = 0; k <= i; ++k)
			V(k, i + 1) = T{0};
	}
	for (std::size_t j = 0; j < N; ++j)
	{
		d[j]        = V(N - 1, j);
		V(N - 1, j) = T{0};
	}
	V(N - 1, N - 1) = T{1};
	e[0] = T{0};
}

template<std::size_t N, typename T>
void tql2(matrix<N, N, T>& V, vector<N, T>& d, vector<N, T>& e)
{
	// Shift e so e[i] holds the off-diagonal between rows i and i+1.
	for (std::size_t i = 1; i < N; ++i)
		e[i - 1] = e[i];
	e[N - 1] = T{0};

	T f    = T{0};
	T tst1 = T{0};
	const T eps = std::numeric_limits<T>::epsilon();

	for (std::size_t l = 0; l < N; ++l)
	{
		// Find a small sub-diagonal element to split off.
		const T abs_d = d[l] < T{0} ? -d[l] : d[l];
		const T abs_e = e[l] < T{0} ? -e[l] : e[l];
		tst1 = tst1 > abs_d + abs_e ? tst1 : abs_d + abs_e;
		std::size_t m = l;
		while (m < N)
		{
			const T abs_em = e[m] < T{0} ? -e[m] : e[m];
			if (abs_em <= eps * tst1)
				break;
			++m;
		}

		// If m == l, d[l] is an eigenvalue. Otherwise iterate.
		if (m > l)
		{
			do
			{
				// Implicit Wilkinson shift.
				T g       = d[l];
				T p       = (d[l + 1] - g) / (T{2} * e[l]);
				T r       = std::hypot(p, T{1});
				if (p < T{0})
					r = -r;
				d[l]      = e[l] / (p + r);
				d[l + 1]  = e[l] * (p + r);
				const T dl1 = d[l + 1];
				const T h   = g - d[l];
				for (std::size_t i = l + 2; i < N; ++i)
					d[i] -= h;
				f += h;

				// Implicit QL transformation.
				p           = d[m];
				T c         = T{1};
				T c2        = c;
				T c3        = c;
				const T el1 = e[l + 1];
				T s         = T{0};
				T s2        = T{0};
				for (std::size_t ii = m; ii > l; --ii)
				{
					const std::size_t i = ii - 1;
					c3 = c2;
					c2 = c;
					s2 = s;
					g  = c * e[i];
					const T h2 = c * p;
					r           = std::hypot(p, e[i]);
					e[i + 1]    = s * r;
					s           = e[i] / r;
					c           = p / r;
					p           = c * d[i] - s * g;
					d[i + 1]    = h2 + s * (c * g + s * d[i]);

					// Accumulate transformation into V.
					for (std::size_t k = 0; k < N; ++k)
					{
						const T tmp = V(k, i + 1);
						V(k, i + 1) = s * V(k, i) + c * tmp;
						V(k, i)     = c * V(k, i) - s * tmp;
					}
				}
				p    = -s * s2 * c3 * el1 * e[l] / dl1;
				e[l] = s * p;
				d[l] = c * p;
			}
			while ((e[l] < T{0} ? -e[l] : e[l]) > eps * tst1);
		}
		d[l] += f;
		e[l]  = T{0};
	}

	// Sort eigenvalues and corresponding columns of V ascending.
	for (std::size_t i = 0; i + 1 < N; ++i)
	{
		std::size_t k = i;
		T p           = d[i];
		for (std::size_t j = i + 1; j < N; ++j)
		{
			if (d[j] < p)
			{
				k = j;
				p = d[j];
			}
		}
		if (k != i)
		{
			d[k] = d[i];
			d[i] = p;
			for (std::size_t j = 0; j < N; ++j)
			{
				const T tmp = V(j, i);
				V(j, i)     = V(j, k);
				V(j, k)     = tmp;
			}
		}
	}
}

}  // namespace detail

template<std::size_t Rows, std::size_t Cols, typename T>
vector<Rows, T> matrix<Rows, Cols, T>::eigenvalues() const requires (Rows == Cols && Rows >= 2)
{
	assert(*this == this->transposed());

	matrix<Rows, Rows, T> V = *this;
	vector<Rows, T> d;
	vector<Rows, T> e;
	detail::tred2(V, d, e);
	detail::tql2(V, d, e);
	return d;
}

template<std::size_t Rows, std::size_t Cols, typename T>
typename matrix<Rows, Cols, T>::eigen_decomposition_t
matrix<Rows, Cols, T>::symmetric_eigen_decomposition() const requires (Rows == Cols && Rows >= 2)
{
	assert(*this == this->transposed());

	eigen_decomposition_t result;
	result.vectors = *this;
	vector<Rows, T> e;
	detail::tred2(result.vectors, result.values, e);
	detail::tql2(result.vectors, result.values, e);
	return result;
}

/**
 * @brief Returns the @c N x @c N identity matrix.
 */
template<std::size_t N, typename T = float>
constexpr matrix<N, N, T> identity_matrix()
{
	matrix<N, N, T> result;
	for (std::size_t i = 0; i < N; ++i)
		result(i, i) = T{1};
	return result;
}

}
