/**
 ******************************************************************************
 * @file    opsy.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   The entry point to OpSy, it includes all needed files access
 *          features of OpSy, and defines the @c sleep_for and @c sleep_until
 *          used to pause tasks for at least a given time or up to a point in
 *          time.
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

#include "Config.hpp"
#include "Task.hpp"
#include "Scheduler.hpp"
#include "PriorityMutex.hpp"
#include "ConditionVariable.hpp"

namespace opsy
{

/**
 * @brief Puts the @c Task to sleep for a given @c duration
 * @warning This should only be called from @c Task and never from interrupt service routine
 * @warning Make sure you release all @c Mutex before you call @c sleep_for
 */
void inline sleep_for(duration t)
{
	asm volatile(
			"mov r0, %[count] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Sleep), [count] "r" (t.count())
			: "r0");
}

/**
 * @brief Puts the @c Task to sleep up to a @c time_point
 * @warning This should only be called from @c Task and never from interrupt service routine
 * @warning Make sure you release all @c Mutex before you call @c sleep_for
 */
void inline sleep_until(time_point tp)
{
	using namespace std::chrono_literals;
	assert(tp-Scheduler::now() < 1h); // if you sleep for more than 1h you probably are missing something (low power mode)
	sleep_for(tp - Scheduler::now());
}

}
