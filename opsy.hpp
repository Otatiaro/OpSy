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

#include "config.hpp"
#include "task.hpp"
#include "scheduler.hpp"
#include "isr_lock.hpp"
#include "condition_variable.hpp"

namespace opsy
{

/**
 * @brief Puts the @c task to sleep for a given @c duration
 * @warning This should only be called from @c task and never from interrupt service routine
 * @warning Make sure you release all @c mutex before you call @c sleep_for
 */
void inline sleep_for(duration t)
{
	assert(cortex_m::ipsr() == 0);            // there is no task to put to sleep in an ISR
	assert(scheduler::is_os_callable());      // ... and an ISR above OpSy must not enter it at all

	// A duration that has already elapsed is not an error -- std::this_thread's
	// sleep_for and sleep_until both return immediately on one. It arrives here
	// from sleep_until with a deadline in the past, where the sleep service call
	// would otherwise trip its assert(delta.count() >= 0), and under NDEBUG set
	// wait_until_ to a point in the past and sleep until the next tick anyway.
	//
	// Zero is left alone: it still costs one tick, which is the established way
	// to yield.
	if (t.count() < 0)
		return;

	const auto count = t.count();
	// "memory" clobber: see trigger_hard_switch in scheduler.hpp. The SVC may
	// reschedule, modify the task frame on PSP and the scheduler globals; the
	// compiler must drop any cached state across the call.
	asm volatile(
			"mov r0, %[count_lo] \n\t"
			"mov r1, %[count_hi] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (scheduler::service_call_number::sleep),
			  [count_lo] "r" (static_cast<uint32_t>(count)),
			  [count_hi] "r" (static_cast<uint32_t>(count >> 32))
			: "r0", "r1", "memory");
}

/**
 * @brief Puts the @c task to sleep up to a @c time_point
 * @warning This should only be called from @c task and never from interrupt service routine
 * @warning Make sure you release all @c mutex before you call @c sleep_for
 */
void inline sleep_until(time_point tp)
{
	using namespace std::chrono_literals;
	const auto remaining = tp - scheduler::now();
	assert(remaining < 1h); // if you sleep for more than 1h you probably are missing something (low power mode)
	sleep_for(remaining);
}

inline opsy_clock::time_point opsy_clock::now() noexcept
{
	return scheduler::now();
}

}
