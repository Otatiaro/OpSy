/**
 ******************************************************************************
 * @file    Hooks.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Default (empty) hooks for OpSy
 *
 * 			This class is a default (empty) implementation of the hooks
 * 			used by OpSy. All methods are @c static @c constexpr @c void and
 * 			empty, so that starting at Og all calls to these are removed by
 * 			the compiler.
 *
 * 			You can redirect these calls to a custom implementation by creating
 * 			a file OpsyHooks.hpp in one of the include directory of the project.
 *
 * 			You can then add custom checks and code, e.g. for integration of a
 * 			monitoring tool like SEGGER SystemView.
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

#if __has_include(<OpsyHooks.hpp>)
#include <OpsyHooks.hpp>
#else

namespace opsy
{
	/**
	 * @brief Methods called by OpSy at various places in the code
	 */
	class Hooks
	{
	public:
		/**
		 * @brief Called when the scheduler is starting
		 * @param idle The idle task
		 * @param coreClock The core clock
		 * @param foreachTask A @c Callback used to iterate over all @c Task
		 */
		static void starting([[maybe_unused]] IdleTaskControlBlock& idle, [[maybe_unused]] uint32_t coreClock, [[maybe_unused]] Callback<void(Callback<void(const TaskControlBlock&)>)> foreachTask)
		{}

		/**
		 * @brief Called when entering PendSv for context switch
		 */
		static constexpr void enterPendSv()
		{}

		/**
		 * @brief Called when going to idle (no active task)
		 */
		static constexpr void enterIdle()
		{}

		/**
		 * @brief Called when entering Systick handler
		 */
		static constexpr void enterSystick()
		{}

		/**
		 * @brief Called when exiting Systick handler
		 * @param taskSwitch @c true if a @c Task switch is requested, @c false otherwise
		 */
		static constexpr void exitSystick([[maybe_unused]] bool taskSwitch)
		{}

		/**
		 * @brief Called when entering Service Call handler
		 */
		static constexpr void enterServiceCall()
		{}

		/**
		 * @brief Called when exiting Service Call handler
		 * @param taskSwitch @c true if a @c Task switch is requested, @c false otherwise
		 */
		static constexpr void exitServiceCall([[maybe_unused]] bool taskSwitch)
		{}

		/**
		 * @brief Called when a @c Task is added to the list of active @c Task
		 * @param task The newly added @c Task
		 */
		static constexpr void taskAdded([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task is terminated (removed from the list of active @c Task)
		 * @param task The terminated @c Task
		 */
		static constexpr void taskTerminated([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task is started (executed)
		 * @param task The @c Task being started
		 */
		static constexpr void taskStarted([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task is put to sleep
		 * @param task The @c Task put to sleep
		 */
		static constexpr void taskSleep([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task is stopped (stop being executed)
		 * @param task The @c Task being stopped
		 */
		static constexpr void taskStopped([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task starts waiting a @c ConditionVariable
		 * @param task The @c Task that starts waiting
		 * @param cv The @c ConditionVariable being waited on
		 */
		static constexpr void taskWait([[maybe_unused]] TaskControlBlock& task, [[maybe_unused]] ConditionVariable& cv)
		{}

		/**
		 * @brief Called when a @c Task starts waiting a @c ConditionVariable with a duration
		 * @param task The @c Task that starts waiting
		 * @param cv The @c ConditionVariable being waited on
		 * @param tp The @c time_point at which a timeout would occur
		 */
		static constexpr void taskWaitTimeout([[maybe_unused]] TaskControlBlock& task, [[maybe_unused]] ConditionVariable& cv, [[maybe_unused]] time_point tp)
		{}

		/**
		 * @brief Called when a @c Task is ready (to be executed)
		 * @param task The @c Task that is ready to be executed
		 */
		static constexpr void taskReady([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task name has changed
		 * @param task The @c Task that has changed name
		 */
		static constexpr void taskNameChanged([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task priority has changed
		 * @param task The @c Task that has changed priority
		 */
		static constexpr void taskPriorityChanged([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task enters a critical section (prevents task switching)
		 */
		static constexpr void enterCriticalSection()
		{}

		/**
		 * @brief Called when a @c Task exits a critical section (task switching allowed)
		 */
		static constexpr void exitCriticalSection()
		{}

		/**
		 * @brief Called when a @c Mutex is stored by the system on a @c Task
		 * @param task The @c Task for which the system has stored a @c Mutex
		 */
		static constexpr void mutexStoredForTask([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Mutex is restored by the system for a @c Task
		 * @param task The @c Task whose @c Mutex has been restored by the system
		 */
		static constexpr void mutexRestoredForTask([[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task or interrupt service routine enters a full lock (complete lock, @c PRIMASK = 1)
		 */
		static constexpr void enterFullLock()
		{}

		/**
		 * @brief Called when a @c Task or interrupt service routine exits a full lock
		 */
		static constexpr void exitFullLock()
		{}

		/**
		 * @brief Called when a @c Task or interrupt service routine enters a priority lock (partial lock, @c BASEPRI = XX)
		 */
		static constexpr void enterPriorityLock(IsrPriority)
		{}

		/**
		 * @brief Called when a @c Task or interrupt service routine exits a priority lock
		 */
		static constexpr void exitPriorityLock()
		{}

		/**
		 * @brief Called when a @c Task starts waiting a @c ConditionVariable
		 * @param cv The @c ConditionVariable being waiting on
		 * @param task The @c Task that starts waiting
		 */
		static constexpr void conditionVariableStartWaiting([[maybe_unused]] ConditionVariable& cv, [[maybe_unused]] TaskControlBlock& task)
		{}

		/**
		 * @brief Called when a @c Task starts waiting a @c ConditionVariable with a timeout
		 * @param cv The @c ConditionVariable being waiting on
		 * @param task The @c Task that starts waiting
		 * @param timeout The timeout duration
		 */
		static constexpr void conditionVariableStartWaiting([[maybe_unused]] ConditionVariable& cv, [[maybe_unused]] TaskControlBlock& task, [[maybe_unused]] duration timeout)
		{}

		/**
		 * @brief Called when a @c ConditionVariable is notified once
		 * @param cv The @c ConditionVariable being notified
		 */
		static constexpr void conditionVariableNotifyOne([[maybe_unused]] ConditionVariable& cv)
		{}

		/**
		 * @brief Called when a @c ConditionVariable is notified for all waiting @c Task
		 * @param cv The @c ConditionVariable being notified
		 */
		static constexpr void conditionVariableNotifyAll([[maybe_unused]] ConditionVariable& cv)
		{}

		/**
		 * @brief This is a placeholder to set names for different objects, it is not called by OpSy
		 * @tparam T The type of the object to name
		 * @param target The @c T object to set name for
		 * @param name The @c T name
		 */
		template<typename T>
		static constexpr void setName([[maybe_unused]] const T& target, [[maybe_unused]] const char* name)
		{}

		/**
		 * @brief This is a placeholder that you can use to call code before @c main starts, it is not called by OpSy
		 * @param coreClock The core clock in hertz
		 */
		static constexpr void boot([[maybe_unused]] uint32_t coreClock)
		{}

		template<void(*Routine)()>
		static constexpr auto decorateIsr()
		{
			return Routine;
		}
	};
}

#endif

