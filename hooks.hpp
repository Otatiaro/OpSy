/**
 ******************************************************************************
 * @file    hooks.hpp
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
 * 			a file opsy_hooks.hpp in one of the include directory of the project.
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

#if __has_include(<opsy_hooks.hpp>)
#include <opsy_hooks.hpp>
#else

namespace opsy
{
	/**
	 * @brief Methods called by OpSy at various places in the code
	 */
	class hooks
	{
	public:
		/**
		 * @brief Called when the scheduler is starting
		 * @param idle The idle task
		 * @param coreClock The core clock
		 * @param foreachTask A @c callback used to iterate over all @c task
		 */
		static void starting([[maybe_unused]] idle_task_control_block& idle, [[maybe_unused]] uint32_t coreClock, [[maybe_unused]] callback<void(callback<void(const task_control_block&)>)> foreachTask)
		{}

		/**
		 * @brief Called when entering PendSv for context switch
		 */
		static constexpr void enter_pend_sv()
		{}

		/**
		 * @brief Called when going to idle (no active task)
		 */
		static constexpr void enter_idle()
		{}

		/**
		 * @brief Called when entering Systick handler
		 */
		static constexpr void enter_systick()
		{}

		/**
		 * @brief Called when exiting Systick handler
		 * @param taskSwitch @c true if a @c task switch is requested, @c false otherwise
		 */
		static constexpr void exit_systick([[maybe_unused]] bool taskSwitch)
		{}

		/**
		 * @brief Called when entering Service Call handler
		 */
		static constexpr void enter_service_call()
		{}

		/**
		 * @brief Called when exiting Service Call handler
		 * @param taskSwitch @c true if a @c task switch is requested, @c false otherwise
		 */
		static constexpr void exit_service_call([[maybe_unused]] bool taskSwitch)
		{}

		/**
		 * @brief Called when a @c task is added to the list of active @c task
		 * @param task The newly added @c task
		 */
		static constexpr void task_added([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task is terminated (removed from the list of active @c task)
		 * @param task The terminated @c task
		 */
		static constexpr void task_terminated([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task is started (executed)
		 * @param task The @c task being started
		 */
		static constexpr void task_started([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task is put to sleep
		 * @param task The @c task put to sleep
		 */
		static constexpr void task_sleep([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task is stopped (stop being executed)
		 * @param task The @c task being stopped
		 */
		static constexpr void task_stopped([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task starts waiting a @c condition_variable
		 * @param task The @c task that starts waiting
		 * @param cv The @c condition_variable being waited on
		 */
		static constexpr void task_wait([[maybe_unused]] task_control_block& task, [[maybe_unused]] condition_variable& cv)
		{}

		/**
		 * @brief Called when a @c task starts waiting a @c condition_variable with a duration
		 * @param task The @c task that starts waiting
		 * @param cv The @c condition_variable being waited on
		 * @param tp The @c time_point at which a timeout would occur
		 */
		static constexpr void task_wait_timeout([[maybe_unused]] task_control_block& task, [[maybe_unused]] condition_variable& cv, [[maybe_unused]] time_point tp)
		{}

		/**
		 * @brief Called when a @c task is ready (to be executed)
		 * @param task The @c task that is ready to be executed
		 */
		static constexpr void task_ready([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task name has changed
		 * @param task The @c task that has changed name
		 */
		static constexpr void task_name_changed([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task priority has changed
		 * @param task The @c task that has changed priority
		 */
		static constexpr void task_priority_changed([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task enters a critical section (prevents task switching)
		 */
		static constexpr void enter_critical_section()
		{}

		/**
		 * @brief Called when a @c task exits a critical section (task switching allowed)
		 */
		static constexpr void exit_critical_section()
		{}

		/**
		 * @brief Called when a @c mutex is stored by the system on a @c task
		 * @param task The @c task for which the system has stored a @c mutex
		 */
		static constexpr void mutex_stored_for_task([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c mutex is restored by the system for a @c task
		 * @param task The @c task whose @c mutex has been restored by the system
		 */
		static constexpr void mutex_restored_for_task([[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task or interrupt service routine enters a full lock (complete lock, @c PRIMASK = 1)
		 */
		static constexpr void enter_full_lock()
		{}

		/**
		 * @brief Called when a @c task or interrupt service routine exits a full lock
		 */
		static constexpr void exit_full_lock()
		{}

		/**
		 * @brief Called when a @c task or interrupt service routine enters a priority lock (partial lock, @c BASEPRI = XX)
		 */
		static constexpr void enter_priority_lock(isr_priority)
		{}

		/**
		 * @brief Called when a @c task or interrupt service routine exits a priority lock
		 */
		static constexpr void exit_priority_lock()
		{}

		/**
		 * @brief Called when a @c task starts waiting a @c condition_variable
		 * @param cv The @c condition_variable being waiting on
		 * @param task The @c task that starts waiting
		 */
		static constexpr void condition_variable_start_waiting([[maybe_unused]] condition_variable& cv, [[maybe_unused]] task_control_block& task)
		{}

		/**
		 * @brief Called when a @c task starts waiting a @c condition_variable with a timeout
		 * @param cv The @c condition_variable being waiting on
		 * @param task The @c task that starts waiting
		 * @param timeout The timeout duration
		 */
		static constexpr void condition_variable_start_waiting([[maybe_unused]] condition_variable& cv, [[maybe_unused]] task_control_block& task, [[maybe_unused]] duration timeout)
		{}

		/**
		 * @brief Called when a @c condition_variable is notified once
		 * @param cv The @c condition_variable being notified
		 */
		static constexpr void condition_variable_notify_one([[maybe_unused]] condition_variable& cv)
		{}

		/**
		 * @brief Called when a @c condition_variable is notified for all waiting @c task
		 * @param cv The @c condition_variable being notified
		 */
		static constexpr void condition_variable_notify_all([[maybe_unused]] condition_variable& cv)
		{}

		/**
		 * @brief This is a placeholder to set names for different objects, it is not called by OpSy
		 * @tparam T The type of the object to name
		 * @param target The @c T object to set name for
		 * @param name The @c T name
		 */
		template<typename T>
		static constexpr void set_name([[maybe_unused]] const T& target, [[maybe_unused]] const char* name)
		{}

		/**
		 * @brief This is a placeholder that you can use to call code before @c main starts, it is not called by OpSy
		 * @param coreClock The core clock in hertz
		 */
		static constexpr void boot([[maybe_unused]] uint32_t coreClock)
		{}

		template<void(*Routine)()>
		static constexpr auto decorate_isr()
		{
			return Routine;
		}
	};
}

#endif

