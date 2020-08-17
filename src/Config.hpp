/**
 ******************************************************************************
 * @file    Config.hpp
 * @author  EZNOV SAS
 * @version V0.1
 * @date    01-March-2019
 * @brief   Default configuration file for OpSy
 *
 * 			This file contains the default configuration for OpSy.
 *
 * 			By default, OpSy will consider there is 4 bits of NVIC priority
 * 			implemented in the system, or take @c __NVIC_PRIO_BITS if the
 * 			symbol is defined (many processor description header have this)
 * 			OpSy will also take the C symbol @c SystemCoreClock to retrieve
 * 			the system clock (as defined in default STM32CubeMx projects)
 *
 * 			Then OpSy will set the Cortex priority grouping for 2 bits
 * 			of preemption (hence 2 bits of sub priority) and set the system
 * 			level at preemption 1, i.e. the second highest preemption priority
 * 			level. This is to allow a maximum number of preemption level below
 * 			system, and still leave room for one preemption level above system,
 * 			for interrupt service routines that cannot tolerate being delayed
 * 			by the system. But keep in mind interrupt service routing with
 * 			preemption level above the system MUST NOT use OpSy at all,
 * 			otherwise atomicity of system calls can not be guaranteed.
 *
 * 			The @c Mutex concrete implementation is also defined here, by default
 * 			it is set to @c PriorityMutex, which is the correct implementation
 * 			for the vast majority of projects. But this using allows for special
 * 			types of Mutex to be used (e.g. multi-processor semaphores).
 *
 * 			Finally OpSy defines the default @c duration with a time base of 1ms
 * 			and @c time_point as a 64 bit derivative of @c duration.
 * 			1ms is often use as high level time base, it is a compromise between
 * 			too frequent calls to @c Systick and fine details of timeouts and
 * 			sleep durations.
 *
 * 			You can easily override this configuration by creating a file
 * 			named OpsyConfig.hpp in any of the include directory.
 *
 ******************************************************************************
 * @copyright Copyright 2019 EZNOV SAS under the MIT License
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
 * @see https://www.opsy.tech
 * @see https://github.com/eznovsas/OpSy
 ******************************************************************************
 */

#pragma once

#include <PriorityMutex.hpp>
#include <cstdint>
#include <chrono>
#include <ratio>


namespace opsy
{

#if __has_include(<OpsyConfig.hpp>)
#include <OpsyConfig.hpp>
#else


extern "C"
{
	extern uint32_t SystemCoreClock;
}

/**
 * @brief Get the system core clock in Hz
 * @return The system core clock in Hz
 */
uint32_t inline getCoreClock()
{
	return SystemCoreClock;
}

#ifdef __NVIC_PRIO_BITS
/**
 * @brief The number of priority bits implemented in the system
 */
constexpr uint32_t kPriorityBits = __NVIC_PRIO_BITS;
#else

/**
 * @brief The number of priority bits implemented in the system
 */
constexpr uint32_t kPriorityBits = 4;

#endif

/**
 * @brief The base clock for timeouts and @c sleep
 */
using duration = std::chrono::duration<int32_t, std::milli>; // by default the timeout is based on milliseconds

/**
 * @brief The number of preemption bits OpSy will set in the system
 */
constexpr uint32_t kPreemptionBits = 2;

/**
 * @brief The preemption level OpSy will run at
 * @warning Any interrupt service routine running at priority above or equal to OpSy MUST NOT use any of OpSy features
 */
constexpr uint32_t kOpsyPreemption = 1;

/**
 * @brief Defines the concrete implementation of @c Mutex used in this project.
 */
using Mutex = PriorityMutex;

#endif

/**
 * @brief The type used to describe a time point
 */
using time_point = std::chrono::time_point<int64_t, duration>;

/**
 * @brief The @c time_point used as a reference when the @c Scheduler starts
 */
static constexpr time_point Startup = time_point{ duration{ 0 } };

}

static_assert(opsy::kPreemptionBits<=opsy::kPriorityBits, "Required preemption bits is more than what is available in the system");
static_assert(opsy::kOpsyPreemption < (1<<opsy::kPreemptionBits), "OpSy preemption level mismatch with requested preemption bits");

