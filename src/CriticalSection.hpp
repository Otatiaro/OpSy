/**
 ******************************************************************************
 * @file    CriticalSection.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   An object representing a lock on a critical section
 *
 * 			When a @c Task has a lock on a critical section, this prevents
 * 			the @c Scheduler from changing the current task.
 * 			A valid @c CriticalSection can only be created by @c Scheduler
 *
 * 			Obviously, do not call blocking features with an active
 * 			critical section (other than @c ConditionVariable wait)
 *
 * 			This object is not copy constructible or copy assignable, only
 * 			move constructible and move assignable.
 *
 * 			This ensures the lock will be released when the object is discarded
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

#include <cassert>

namespace opsy
{
/**
 * @brief A critical section handle
 */
class CriticalSection
{
	friend class Scheduler;

public:

	/**
	 * @brief Constructs an invalid @c CriticalSection handle (only @c Scheduler can create valid ones)
	 */
	constexpr CriticalSection() :
			m_valid(false)
	{

	}

	CriticalSection& operator=(const CriticalSection&) = delete;
	CriticalSection(const CriticalSection&) = delete;

	/**
	 * @brief Assigns a @c CriticalSection by moving data from another @c CriticalSection
	 * @param other The @c CriticalSection to move data from
	 * @warning This @c CriticalSection should not be active before it is assigned to
	 */
	constexpr CriticalSection& operator=(CriticalSection&& other)
	{
		assert(!m_valid);
		m_valid = other.m_valid;
		other.m_valid = false;
		return *this;
	}

	/**
	 * @brief Constructs a @c CriticalSection by moving data from another @c CriticalSection
	 * @param other The @c CriticalSection to move data from
	 */
	constexpr CriticalSection(CriticalSection&& other) :
			m_valid(other.m_valid)
	{
		other.m_valid = false;
	}

	/**
	 * @brief Deletes the @c CriticalSection, releasing the lock if it is active
	 */
	~CriticalSection();

private:

	constexpr explicit CriticalSection(bool valid) :
			m_valid(valid)
	{

	}

	constexpr void disable()
	{
		assert(m_valid);
		m_valid = false;
	}

	bool m_valid;

};
}
