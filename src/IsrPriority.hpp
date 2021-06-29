/**
 ******************************************************************************
 * @file    IsrPriority.hpp
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

namespace opsy
{

/**
 * @brief An interrupt service (or @c Mutex) priority
 */
class IsrPriority
{
public:

	/**
	 * @brief The maximum number of preemption priority bits
	 */
	static constexpr uint8_t kMaxPreemptionBits = 8;

	/**
	 * @brief Constructs the @c IsrPriority from preemption priority and sub-priority based on number of preemption bits
	 * @param preempt The preemption priority
	 * @param sub The sub-priority
	 * @return The @c IsrPriority which has @p preempt preemption priority and @p sub sub-priority
	 */
	template<std::size_t PreemptBits>
	static constexpr IsrPriority FromPreemptSub(uint8_t preempt, uint8_t sub)
	{
		return IsrPriority(static_cast<uint8_t>(((preempt & ((1u << PreemptBits) - 1u)) << (kMaxPreemptionBits - PreemptBits)) | (sub & (((1u << (kMaxPreemptionBits - PreemptBits)) - 1u)))));
	}

	/**
	 * @brief Constructs an @c IsrPriority with a raw value.
	 * @param value The raw value of the priority (with no distinction between preemption and sub-priority)
	 * @remark The default value is @c 0xFF which is the lowest priority available in the Cortex-M
	 */
	constexpr explicit IsrPriority(uint8_t value = 0xFF) :
			m_value(value)
	{

	}

	/**
	 * @brief Constructs an @c IsrPriority based on another one.
	 * @param other The other @c IsrPriority
	 */
	constexpr IsrPriority(const IsrPriority& other) :
			m_value(other.m_value)
	{
	}

	/**
	 * @brief Copies the data from another @c IsrPriority.
	 * @param other The other @c IsrPriority
	 * @return A reference to this instance
	 */
	constexpr IsrPriority& operator=(const IsrPriority& other)
	{
		m_value = other.m_value;
		return *this;
	}

	/**
	 * @brief Preemption level of the @c IsrPriority
	 * @return The preemption level
	 * @remark Like all @c IsrPriority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PreemptBits>
	constexpr uint8_t preempt() const
	{
		return m_value >> (kMaxPreemptionBits - PreemptBits);
	}

	/**
	 * @brief Sub-priority of the @c IsrPriority
	 * @return The sub-priority
	 * @remark Like all @c IsrPriority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PreemptBits>
	constexpr uint8_t sub() const
	{
		return m_value & (1 << ((kMaxPreemptionBits - PreemptBits)) - 1);
	}

	/**
	 * @brief Raw value of the @c IsrPriority
	 * @return The raw value
	 * @remark Like all @c IsrPriority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	constexpr uint8_t value() const
	{
		return m_value;
	}

	/**
	 * @brief Masked raw value of the @c IsrPriority. It contains only the bits actually implemented in the Cortex-M.
	 * @return The masked raw value.
	 * @remark Like all @c IsrPriority a lower value means a higher priority. @c 0 is the system highest priority, when @c 0xFF is the lowest priority.
	 */
	template<std::size_t PriorityBits>
	constexpr uint8_t maskedValue() const
	{
		constexpr uint8_t mask = static_cast<uint8_t>(((1 << PriorityBits) - 1) << (kMaxPreemptionBits - PriorityBits));
		return value() & mask;
	}

private:
	uint8_t m_value;
};

}
