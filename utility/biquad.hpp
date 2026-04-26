/**
 ******************************************************************************
 * @file    biquad.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   A biquad (second-order IIR) filter
 *
 * 			Implements a direct-form II transposed biquad with low-pass,
 * 			high-pass, notch and band-pass topologies. Coefficients are
 * 			computed at construction from the sampling frequency, the cut
 * 			frequency and the quality factor (defaulting to Butterworth).
 * 			Both the value type and the coefficient type are template
 * 			parameters so the filter can run on @c float, @c double or any
 * 			fixed-point arithmetic that supports the required operations.
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

#include <math.h>
#include <array>

namespace opsy::utility
{

/**
 * Defines the type of a filter
 */
enum class filter_type
{
	low_pass, /// Filter high frequencies, allow low frequencies
	high_pass, /// Filter low frequencies, allow high frequencies
	notch, /// Filter around a specified frequency
	band_pass, /// Filter everything except a specified frequency
};

/**
 * A biquad filter class
 * @tparam ValueType The type of the values to filter
 * @tparam CoefficientType The type of the coefficients (usually float or double)
 */
template<typename ValueType = float, typename CoefficientType = float>
class biquad
{
public:

	/**
	 * The Butterworth Q value
	 */
	static constexpr CoefficientType butterworth_q = static_cast<CoefficientType>(1.0 / std::sqrt(2.0));

	/**
	 * Constructs a @c biquad with the specified characteristics
	 * @param sampling_frequency The sampling frequency
	 * @param cut_frequency The cut frequency of the filter
	 * @param type The type of the filter
	 * @param q The Q value ("quality factor") of the filter
	 * @param initial_value The initial value of the filter
	 */
	constexpr biquad(
			const CoefficientType& sampling_frequency,
			const CoefficientType& cut_frequency,
			filter_type type,
			CoefficientType q = butterworth_q,
			const ValueType& initial_value = ValueType()) :
					b_({
							make<coeff::b0>(sampling_frequency, cut_frequency, type, q),
							make<coeff::b1>(sampling_frequency, cut_frequency, type, q),
							make<coeff::b2>(sampling_frequency, cut_frequency, type, q)}),
					a_({
							make<coeff::a1>(sampling_frequency, cut_frequency, type, q),
							make<coeff::a2>(sampling_frequency, cut_frequency, type, q)}),
					delay_({
							init(initial_value),
							init(initial_value),
							init(initial_value)})
	{

	}

	/**
	 * Feeds a new value to the filter
	 * @param value The new value of the signal (should be called at sampling frequency)
	 */
	constexpr void feed(const ValueType& value)
	{
		auto w = value - delay_[0] * a_[0] - delay_[1] * a_[1];
		delay_[2] = delay_[1];
		delay_[1] = delay_[0];
		delay_[0] = w;
	}

	/**
	 * Computes the current value of the filter
	 * @return The current value of the filter
	 */
	constexpr ValueType value() const
	{
		return delay_[0] * b_[0] + delay_[1] * b_[1] + delay_[2] * b_[2];
	}

	/**
	 * Resets the filter to a known value if possible
	 * @param value The expected new output value of the filter.
	 */
	constexpr void reset(const ValueType& value)
	{
		delay_[0] = delay_[1] = delay_[2] = init(value);
	}

private:

	static constexpr CoefficientType zero = static_cast<CoefficientType>(0);
	static constexpr CoefficientType one = static_cast<CoefficientType>(1);

	std::array<CoefficientType, 3> b_;
	std::array<CoefficientType, 2> a_;
	std::array<ValueType, 3> delay_;

	enum class coeff
	{
		b0, b1, b2, a1, a2
	};

	/**
	 * Calculates the specified coefficient for the filter
	 * @param sampling The sampling frequency
	 * @param cut The cut frequency
	 * @param type The filter type
	 * @param q The quality factor
	 * @return The value of the coefficient
	 */
	template<coeff Which>
	constexpr CoefficientType make(const CoefficientType& sampling, const CoefficientType& cut, filter_type type, CoefficientType q)
	{
		constexpr CoefficientType pi = static_cast<CoefficientType>(std::acos(-1.0));
		constexpr CoefficientType two = static_cast<CoefficientType>(2);

		auto k = std::tan(pi * cut / sampling);
		[[maybe_unused]] auto norm = one / (one + k / q + k * k);

		switch (Which)
		{
		case coeff::b0:
		{
			switch (type)
			{
			default:
			case filter_type::low_pass:
				return k * k * norm;
			case filter_type::high_pass:
				return norm;
			case filter_type::band_pass:
				return k / q * norm;
			case filter_type::notch:
				return (one + k * k) * norm;
			}
		}
		case coeff::b1:
		{
			switch (type)
			{
			default:
			case filter_type::low_pass:
				return two * b_[0];
			case filter_type::high_pass:
				return -two * b_[0];
			case filter_type::band_pass:
				return zero;
			case filter_type::notch:
				return two * (k * k - one) * norm;
			}
		}
		case coeff::b2:
		{
			switch (type)
			{
			default:
			case filter_type::low_pass:
			case filter_type::high_pass:
			case filter_type::notch:
				return b_[0];

			case filter_type::band_pass:
				return -b_[0];
			}
		}
		case coeff::a1:
		{
			switch (type)
			{
			default:
			case filter_type::low_pass:
			case filter_type::high_pass:
			case filter_type::band_pass:
				return two * (k * k - one) * norm;
			case filter_type::notch:
				return b_[1];
			}
		}
		case coeff::a2:
			return (one - k / q + k * k) * norm;

		default:
			return zero;
		}
	}

	/**
	 * Computes the value to put in delay_ to get the specified filter output
	 * @param value The expected filter output
	 * @return The value to put in delay_ to get the expected filter output
	 */
	constexpr ValueType init(const ValueType& value)
	{
		auto denom = one + a_[0] + a_[1];
		return denom == zero ? value : value / denom;
	}
};

}
