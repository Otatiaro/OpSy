/**
 ******************************************************************************
 * @file    condition_variable.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Replacement for @c std::condition_variable
 *
 * 		 Compared to @c std::condition_variable:
 * 		  - uses only the @c mutex class
 * 		  - has @c wait, @c wait_for and @c wait_until without @c mutex
 * 		  - uses @c duration and @c time_point for time expressions
 * 		  - uses a @c mutex for @c notify and @c notify_all synchronization
 * 		  - does not generate spurious notifications, hence there is no
 * 		    method with check predicates
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

#include <optional>

#include "config.hpp"
#include "embedded_list.hpp"
#include "task.hpp"
#include "cortex_m.hpp"
//#include "mutex.hpp"

namespace std
{

/**
 * @brief Result of a timed wait operation on a @c condition_variable
 * @remark Equivalent to @c std::cv_status from @c <condition_variable>,
 *         redefined here because @c <condition_variable> is unavailable on
 *         Cortex-M targets (no OS threading support in the C++ runtime).
 *         Defined in @c namespace @c std to remain compatible with standard
 *         call sites and @c std::lock_guard usage.
 */
enum class cv_status
{
	no_timeout = 0, ///< The condition variable was notified before the timeout
	timeout    = 1  ///< The timeout elapsed before the condition variable was notified
};

} // namespace std

namespace opsy
{

/**
 * @brief A condition variable.
 * It is used to synchronize a @c task with either one or multiple @c task and/or interrupt service routines
 * @warning Only a @c task is allowed to @c wait, @c wait_for or @c wait_until. An interrupt service routine is NOT allowed to call wait in any case
 * @remark It is a replacement for @c std::condition_variable
 */
class condition_variable
{
	friend class scheduler;

public:

	/**
	 * @brief Creates a @c condition_variable with the @p priority synchronization on @c notify and @c notify_all
	 * @param priority The @c mutex priority used for @c notify and @c notify_all
	 */
	constexpr explicit condition_variable(std::optional<isr_priority> priority = std::nullopt) :
			mutex_(priority)
	{

	}

	/**
	 * @brief Notify the first @c task in the waiting list if there is any, releasing it from its wait state to the ready state
	 * @remark Defined inline at the bottom of @c scheduler.hpp (calls
	 *         @c scheduler::wake_up; see the cycle-breaking note there).
	 */
	void notify_one();

	/**
	 * @brief Notify all @c task in the waiting list, releasing them from their wait state to the ready state
	 * @remark Defined inline at the bottom of @c scheduler.hpp (see @c notify_one).
	 */
	void notify_all();

	/**
	 * @brief Wait on a @c condition_variable with no time limit nor @c mutex synchronization
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp (issues an @c SVC
	 *         using @c scheduler::service_call_number).
	 */
	void wait();

	/**
	 * @brief Wait on a @c condition_variable with no time limit with @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp.
	 */
	void wait(mutex& mtx);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and no @c mutex synchronization
	 * @param timeout The time limit of the wait. If the @c condition_variable has not been notified for @p timeout then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp.
	 */
	std::cv_status wait_for(duration timeout);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout The time limit of the wait. If the @c condition_variable has not been notified for @p timeout then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp.
	 */
	std::cv_status wait_for(mutex& mtx, duration timeout);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param timeout_time The time limit of the wait. If the @c condition_variable has not been notified before @p timeout_time then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp (uses @c scheduler::now).
	 */
	std::cv_status wait_until(time_point timeout_time);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout_time The time limit of the wait. If the @c condition_variable has not been notified before @p timeout_time then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 * @remark Defined inline at the bottom of @c scheduler.hpp (uses @c scheduler::now).
	 */
	std::cv_status wait_until(mutex& mtx, time_point timeout_time);

private:

	mutex mutex_;
	embedded_list<task_control_block, task_lists::Waiting> waiting_list_;

	/**
	 * @brief Adds a @c task_control_block to the waiting list, ordered by priority
	 * @param task The @c task_control_block to add
	 * @remark Inline here because it has no dependency on @c scheduler.
	 */
	void add_waiting(task_control_block& task)
	{
		waiting_list_.insert_when(task_control_block::priority_is_lower, task);
	}

	/**
	 * @brief Removes a @c task_control_block from the waiting list
	 * @param task The @c task_control_block to remove
	 * @remark Inline here because it has no dependency on @c scheduler.
	 */
	void remove_waiting(task_control_block& task)
	{
		waiting_list_.erase(task);
	}
};
}
