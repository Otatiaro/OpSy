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
#include <concepts>
#include <cstddef>

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
