/**
 ******************************************************************************
 * @file    isr_priority.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Interrupt Service Routine (ISR) priority
 *
 * 			The underlying type for Cortex-M is an 8 bit unsigned integer
 * 			This class is used to easy masking and preemption / sub priority
 * 			handling
 *
 ******************************************************************************
 * @copyright Copyright 2019 Thomas Legrand under the MIT License
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
#include <cstdint>

namespace opsy
{

/**
 * @brief An interrupt service (or @c mutex) priority
 */
class isr_priority
{
public:

	/**
	 * @brief The maximum number of preemption priority bits
	 */
	static constexpr uint8_t max_preemption_bits = 8;

	/**
	 * @brief The raw value of the lowest priority of the Cortex-M
	 */
	static constexpr uint8_t lowest = 0xFF;

	/**
	 * @brief Constructs the @c isr_priority from preemption priority and sub-priority based on number of preemption bits
	 * @param preempt The preemption priority
	 * @param sub The sub-priority
	 * @return The @c isr_priority which has @p preempt preemption priority and @p sub sub-priority
	 */
	template<std::size_t PreemptBits>
	static constexpr isr_priority from_preempt_sub(uint8_t preempt, uint8_t sub)
	{
		return isr_priority(static_cast<uint8_t>(((preempt & ((1U << PreemptBits) - 1U)) << (max_preemption_bits - PreemptBits)) | (sub & ((1U << (max_preemption_bits - PreemptBits)) - 1U))));
	}

	/**
	 * @brief Constructs an @c isr_priority with a raw value.
	 * @param value The raw value of the priority (with no distinction between preemption and sub-priority)
	 * @remark The default value is @c 0xFF which is the lowest priority available in the Cortex-M
	 */
	constexpr explicit isr_priority(uint8_t value = lowest) :
			value_(value)
	{

	}

	/**
	 * @brief Preemption level of the @c isr_priority
	 * @return The preemption level
	 * @remark Like all @c isr_priority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PreemptBits>
	[[nodiscard]] constexpr uint8_t preempt() const
	{
		return value_ >> (max_preemption_bits - PreemptBits);
	}

	/**
	 * @brief Sub-priority of the @c isr_priority
	 * @return The sub-priority
	 * @remark Like all @c isr_priority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PreemptBits>
	[[nodiscard]] constexpr uint8_t sub() const
	{
		return value_ & ((1U << (max_preemption_bits - PreemptBits)) - 1U);
	}

	/**
	 * @brief Raw value of the @c isr_priority
	 * @return The raw value
	 * @remark Like all @c isr_priority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	[[nodiscard]] constexpr uint8_t value() const
	{
		return value_;
	}

	/**
	 * @brief Masked raw value of the @c isr_priority. It contains only the bits actually implemented in the Cortex-M.
	 * @return The masked raw value.
	 * @remark Like all @c isr_priority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PriorityBits>
	[[nodiscard]] constexpr uint8_t masked_value() const
	{
		constexpr auto mask = static_cast<uint8_t>(((1U << PriorityBits) - 1U) << (max_preemption_bits - PriorityBits));
		return value() & mask;
	}

private:
	uint8_t value_;
};

}
