/**
 ******************************************************************************
 * @file    slope.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   An FIR slope (numerical derivative) filter
 *
 * 			Estimates the slope of a sampled signal using a least-squares
 * 			linear fit over the last @c Depth samples. Compared to a naive
 * 			differential, this is far less sensitive to noise. The
 * 			coefficients are computed at compile time from @c Depth, so
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
 * @tparam Depth The depth of the filter (number of previous values to calculate on)
 * @tparam ValueType The type of the values, by default @c float
 * @tparam CoefficientType The type of the coefficient, by default the same as @c ValueType
 * @warning Introduces a mean delay of Depth/2 samples
 */
template<std::size_t Depth, typename ValueType = float, typename CoefficientType = float>
class slope
{
	static_assert(Depth >= 4, "For Depth below 4, this algorithm is less efficient than a normal differential");

public:

	/**
	 * Constructs a slope filter with the specified sampling frequency
	 * @param sampling_frequency The input sampling frequency
	 */
	explicit constexpr slope(CoefficientType sampling_frequency) :
			sampling_frequency_(sampling_frequency)
	{
	}

	/**
	 * Feeds a new value to the slope filter
	 * @param value The latest value to append to the filter
	 */
	constexpr void feed(const ValueType& value)
	{
		index_++; // increment current index

		if (index_ == Depth) // if end of array reached
			index_ = 0; // reset to zero

		values_[index_] = value; // copy value into the array
	}

	/**
	 * Gets the current value of the filtered slope
	 * @return The current slope (in unit / s if @c sampling_frequency is in Hertz)
	 */
	constexpr ValueType value() const
	{
		ValueType accumulator = ValueType();
		for (auto i = 0U; i < Depth / 2; ++i)
			accumulator += (backward(i) - forward(i)) * coefficients[i];
		return accumulator * sampling_frequency_;
	}

private:

	static constexpr CoefficientType twelve = static_cast<CoefficientType>(12);
	static constexpr CoefficientType six = static_cast<CoefficientType>(6);
	static constexpr CoefficientType denominator = (Depth * (Depth * Depth - 1));
	static constexpr std::array<CoefficientType, Depth / 2> coefficients = slope::coefs(std::make_index_sequence<Depth / 2>());

	std::array<ValueType, Depth> values_;
	CoefficientType sampling_frequency_;
	std::size_t index_ = 0;

	/**
	 * @brief Get a value in the array relative to the current index
	 * @param offset The offset from the current index
	 * @return The n-th value in the array (backward relative to current index)
	 */
	const ValueType& backward(std::size_t offset) const
	{
		return values_[offset > index_ ? index_ + Depth - offset : index_ - offset];
	}

	/**
	 * @brief Get a value in the array relative to the current index
	 * @param offset The offset from the current index
	 * @return The n-th value in the array (forward relative to current index)
	 */
	const ValueType& forward(std::size_t offset) const
	{
		const auto tmp = index_ + offset + 1;
		return values_[tmp < Depth ? tmp : tmp - Depth];
	}

	/**
	 * Calculate the i-th coefficient for the slope filter
	 * @param index The index of the coefficient
	 * @return The value of the i-th coefficient
	 */
	static constexpr CoefficientType coef(std::size_t index)
	{
		return (six * (Depth - 1) - twelve * static_cast<CoefficientType>(index)) / denominator;
	}

	/**
	 * Creates the list of coefficients for the slope filter
	 * @param An index sequence
	 * @return The array containing the slope filter coefficients
	 */
	template<std::size_t ... Indices>
	static constexpr auto coefs(std::index_sequence<Indices...>) -> std::array<CoefficientType, sizeof...(Indices)>
	{
		return
		{
			{	coef(Indices)...}};

	}

};

template<std::size_t Depth, typename ValueType, typename CoefficientType>
constexpr std::array<CoefficientType, Depth / 2> slope<Depth, ValueType, CoefficientType>::coefficients;

}
