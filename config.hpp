/**
 ******************************************************************************
 * @file    config.hpp
 * @author  Thomas Legrand
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
 * 			The @c mutex concrete implementation is also defined here, by default
 * 			it is set to @c isr_lock, which is the correct implementation
 * 			for the vast majority of projects. But this using allows for special
 * 			types of mutex to be used (e.g. multi-processor semaphores).
 *
 * 			Finally OpSy defines the default @c duration with a time base of 1ms
 * 			and @c time_point as a 64 bit derivative of @c duration.
 * 			1ms is often use as high level time base, it is a compromise between
 * 			too frequent calls to @c Systick and fine details of timeouts and
 * 			sleep durations.
 *
 * 			You can easily override this configuration by creating a file
 * 			named opsy_config.hpp in any of the include directory.
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

#include "isr_lock.hpp"
#include <cstdint>
#include <chrono>
#include <ratio>


namespace opsy
{

#if __has_include(<opsy_config.hpp>)
#include <opsy_config.hpp>
#else


extern "C"
{
	extern uint32_t SystemCoreClock;
}

/**
 * @brief Get the system core clock in Hz
 * @return The system core clock in Hz
 */
uint32_t inline core_clock()
{
	return SystemCoreClock;
}

#ifdef __NVIC_PRIO_BITS
/**
 * @brief The number of priority bits implemented in the system
 */
constexpr uint32_t priority_bits = __NVIC_PRIO_BITS;
#else

/**
 * @brief The number of priority bits implemented in the system
 */
constexpr uint32_t priority_bits = 4;

#endif

/**
 * @brief The base clock for timeouts and @c sleep
 */
using duration = std::chrono::duration<int64_t, std::milli>; // by default the timeout is based on milliseconds

/**
 * @brief The number of preemption bits OpSy will set in the system
 */
constexpr uint32_t preemption_bits = 2;

/**
 * @brief The preemption level OpSy will run at
 * @warning Any interrupt service routine running at priority above or equal to OpSy MUST NOT use any of OpSy features
 */
constexpr uint32_t opsy_preemption = 1;

/**
 * @brief How far priority inheritance follows a chain of blocked tasks
 *
 *        A task blocked on a mutex may be holding another, so raising one
 *        holder can require raising the one it is itself waiting for. The walk
 *        is bounded so a cycle — which is a deadlock, and asserted separately —
 *        cannot spin forever.
 */
static constexpr std::size_t max_inheritance_depth = 8;

// Note for code written against an OpSy older than the two-lock split: this
// file used to alias opsy::mutex onto the ISR-facing lock, which is now
// called isr_lock. The two are not interchangeable, so such an alias would be
// actively misleading and there is none here.
//
//   isr_lock   masks interrupts. No owner, never blocks. Use it to share
//              state with an interrupt handler, which cannot be suspended.
//   mutex      (in mutex.hpp) blocks. Has an owner, and the semantics of
//              std::mutex. Use it to share state between tasks.
//
// isr_lock is the extension point for projects that need a different
// implementation of the ISR-facing lock: define opsy_config.hpp and provide
// your own.


/**
 * @brief Trap the running task / system on an unrecoverable internal error.
 *
 *        OpSy invokes this from a handful of "this should never happen"
 *        sites (for example when @c scheduler::start cannot make a first
 *        context switch). The default uses @c __builtin_trap, which on
 *        ARM Cortex-M emits a @c BKPT instruction — a debugger halts on
 *        it, and otherwise the core hard-faults to a known stop.
 *
 *        Override by providing your own @c trap() in @c <opsy_config.hpp>
 *        if you need to flush logs, blink an LED, reset, etc. before
 *        halting; just make sure your replacement is @c [[noreturn]].
 */
[[noreturn]] inline void trap()
{
	__builtin_trap();
}

#endif

/**
 * @brief A minimal clock type satisfying the C++ TrivialClock requirements,
 *        used as a tag for @c time_point. The actual time source is
 *        @c scheduler::now(), which increments on each Systick interrupt.
 * @remark @c now() is declared here and defined in @c opsy.hpp after
 *         @c scheduler is available, to avoid a circular dependency.
 */
struct opsy_clock
{
	/**
	 * @warning @c is_steady is @c true in the sense the standard requires —
	 *          the value never decreases and is never adjusted — but this is a
	 *          software tick counter, not a hardware one, and the two differ in
	 *          a way that matters.
	 *
	 *          @c ticks_ advances only when @c SysTick_Handler runs. SysTick's
	 *          pending bit is a flag, not a counter, so masking above
	 *          @c systick_priority for longer than one tick period — a mutex
	 *          carrying an @c isr_priority , a full lock, a long ISR — loses
	 *          the intervening ticks outright rather than delaying them. The
	 *          clock then under-counts real time, permanently and without
	 *          bound.
	 *
	 *          So `auto t0 = now(); masked_work_10ms(); now() - t0;` can report
	 *          1 ms. Use it for scheduling and timeouts, which is what it
	 *          drives; for measuring elapsed real time, read a hardware counter
	 *          such as @c DWT_CYCCNT .
	 *
	 * @warning @c now() is not callable from anywhere: it requires the
	 *          scheduler to be started, and the caller to be running no higher
	 *          than @c systick_priority (both asserted in
	 *          @c scheduler::now ). @c std::chrono::steady_clock::now() has
	 *          neither precondition.
	 */
	using rep        = int64_t;
	using period     = std::milli;
	using duration   = opsy::duration;
	using time_point = std::chrono::time_point<opsy_clock>;
	static constexpr bool is_steady = true;
	static time_point now() noexcept;
};

/**
 * @brief The type used to describe a time point
 */
using time_point = opsy_clock::time_point;

/**
 * @brief The @c time_point used as a reference when the @c scheduler starts
 */
static constexpr time_point startup = time_point{ duration{ 0 } };

}

static_assert(opsy::preemption_bits<=opsy::priority_bits, "Required preemption bits is more than what is available in the system");
static_assert(opsy::opsy_preemption < (1<<opsy::preemption_bits), "OpSy preemption level mismatch with requested preemption bits");

// Pull in our assert override AFTER all OpSy and standard headers config.hpp
// transitively brings in (isr_lock.hpp etc., several of which include
// <cassert>). opsy_assert.hpp wins because it does its own #undef just before
// installing the trap-based macro, and OpSy headers replace their direct
// #include <cassert> by an include of opsy_assert.hpp so nothing re-clobbers
// it later in the include chain.
#include "opsy_assert.hpp"

