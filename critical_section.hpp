/**
 ******************************************************************************
 * @file    critical_section.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   An object representing a lock on a critical section
 *
 * 			When a @c task has a lock on a critical section, this prevents
 * 			the @c scheduler from changing the current task.
 * 			A valid @c critical_section can only be created by @c scheduler
 *
 * 			Obviously, do not call blocking features with an active
 * 			critical section (other than @c condition_variable wait)
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

#include "opsy_assert.hpp"

namespace opsy
{
/**
 * @brief A critical section handle
 */
class critical_section
{
	friend class scheduler;

public:

	/**
	 * @brief Constructs an invalid @c critical_section handle (only @c scheduler can create valid ones)
	 */
	constexpr critical_section() :
			valid_(false)
	{

	}

	critical_section& operator=(const critical_section&) = delete;
	critical_section(const critical_section&) = delete;

	/**
	 * @brief Assigns a @c critical_section by moving data from another @c critical_section
	 * @param other The @c critical_section to move data from
	 * @warning This @c critical_section should not be active before it is assigned to
	 */
	constexpr critical_section& operator=(critical_section&& other)
	{
		assert(!valid_);
		valid_ = other.valid_;
		other.valid_ = false;
		return *this;
	}

	/**
	 * @brief Constructs a @c critical_section by moving data from another @c critical_section
	 * @param other The @c critical_section to move data from
	 */
	constexpr critical_section(critical_section&& other) :
			valid_(other.valid_)
	{
		other.valid_ = false;
	}

	/**
	 * @brief Deletes the @c critical_section, releasing the lock if it is active
	 * @remark The body is defined inline at the bottom of @c scheduler.hpp because
	 *         it calls @c scheduler::critical_section_end, and @c scheduler.hpp
	 *         already includes (transitively) this header. Defining the body
	 *         after the @c scheduler class declaration breaks the cycle without
	 *         requiring a translation unit.
	 */
	~critical_section();

private:

	constexpr explicit critical_section(bool valid) :
			valid_(valid)
	{

	}

	constexpr void disable()
	{
		assert(valid_);
		valid_ = false;
	}

	bool valid_;

};
}
