/**
 ******************************************************************************
 * @file    slope.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   An FIR slope (numerical derivative) filter
 *
 * 			Estimates the slope of a sampled signal using a least-squares
 * 			linear fit over the last @c N samples. Compared to a naive
 * 			differential, this is far less sensitive to noise. The
 * 			coefficients are computed at compile time from @c N, so
 * 			@c value() boils down to a fixed-size dot product.
 *
 * 			Reference: https://ieeexplore.ieee.org/document/4644063
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

#include <cstddef>
#include <array>
#include <utility>

namespace opsy::utility
{

/**
 * An FIR slope filter based on https://ieeexplore.ieee.org/document/4644063
 * @tparam N The depth of the filter (number of previous values to calculate on)
 * @tparam T The type of the values, by default @c float
 * @tparam Coef The type of the coefficient, by default the same as @c T
 * @warning Introduces a mean delay of N/2 samples
 */
template<std::size_t N, typename T = float, typename Coef = float>
class slope
{
	static_assert(N >= 4, "For N below 4, this algorithm is less efficient than a normal differential");

public:

	/**
	 * Constructs a slope filter with the specified sampling frequency
	 * @param sampling_frequency The input sampling frequency
	 */
	explicit constexpr slope(Coef sampling_frequency) :
			sampling_frequency_(sampling_frequency)
	{
	}

	/**
	 * Feeds a new value to the slope filter
	 * @param value The latest value to append to the filter
	 */
	constexpr void feed(const T& value)
	{
		index_++; // increment current index

		if (index_ == N) // if end of array reached
			index_ = 0; // reset to zero

		values_[index_] = value; // copy value into the array
	}

	/**
	 * Gets the current value of the filtered slope
	 * @return The current slope (in unit / s if @c sampling_frequency is in Hertz)
	 */
	constexpr T value() const
	{
		T accumulator = T();
		for (auto i = 0U; i < N / 2; ++i)
			accumulator += (backward(i) - forward(i)) * coefficients[i];
		return accumulator * sampling_frequency_;
	}

private:

	static constexpr Coef twelve = static_cast<Coef>(12);
	static constexpr Coef six = static_cast<Coef>(6);
	static constexpr Coef denominator = (N * (N * N - 1));
	static constexpr std::array<Coef, N / 2> coefficients = slope::coefs(std::make_index_sequence<N / 2>());

	std::array<T, N> values_;
	Coef sampling_frequency_;
	std::size_t index_ = 0;

	/**
	 * @brief Get a value in the array relative to the current index
	 * @param offset The offset from the current index
	 * @return The n-th value in the array (backward relative to current index)
	 */
	const T& backward(std::size_t offset) const
	{
		return values_[offset > index_ ? index_ + N - offset : index_ - offset];
	}

	/**
	 * @brief Get a value in the array relative to the current index
	 * @param offset The offset from the current index
	 * @return The n-th value in the array (forward relative to current index)
	 */
	const T& forward(std::size_t offset) const
	{
		const auto tmp = index_ + offset + 1;
		return values_[tmp < N ? tmp : tmp - N];
	}

	/**
	 * Calculate the i-th coefficient for the slope filter
	 * @param index The index of the coefficient
	 * @return The value of the i-th coefficient
	 */
	static constexpr Coef coef(std::size_t index)
	{
		return (six * (N - 1) - twelve * static_cast<Coef>(index)) / denominator;
	}

	/**
	 * Creates the list of coefficients for the slope filter
	 * @param An index sequence
	 * @return The array containing the slope filter coefficients
	 */
	template<std::size_t ... Is>
	static constexpr auto coefs(std::index_sequence<Is...>) -> std::array<Coef, sizeof...(Is)>
	{
		return
		{
			{	coef(Is)...}};

	}

};

template<std::size_t N, typename T, typename Coef>
constexpr std::array<Coef, N / 2> slope<N, T, Coef>::coefficients;

}
