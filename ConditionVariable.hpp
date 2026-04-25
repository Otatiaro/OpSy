/**
 ******************************************************************************
 * @file    ConditionVariable.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Replacement for @c std::condition_variable
 *
 * 		 Compared to @c std::condition_variable:
 * 		  - uses only the @c Mutex class
 * 		  - has @c wait, @c wait_for and @c wait_until without @c Mutex
 * 		  - uses @c duration and @c time_point for time expressions
 * 		  - uses a @c Mutex for @c notify and @c notify_all synchronization
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

#include "Config.hpp"
#include "EmbeddedList.hpp"
#include "Task.hpp"
#include "CortexM.hpp"
//#include "Mutex.hpp"

namespace std
{

/**
 * The result of a call to @c wait with timeout
 * @remark It is defined as 0 or 1 so it is equivalent to @c bool with better naming
 */
enum class cv_status
{
	no_timeout = 0, ///< No Timeout
	timeout = 1 ///< Timeout
};
}

namespace opsy
{

/**
 * @brief A condition variable.
 * It is used to synchronize a @c Task with either one or multiple @c Task and/or interrupt service routines
 * @warning Only a @c Task is allowed to @c wait, @c wait_for or @c wait_until. An interrupt service routine is NOT allowed to call wait in any case
 * @remark It is a replacement for @c std::condition_variable
 */
class ConditionVariable
{
	friend class Scheduler;

public:

	/**
	 * @brief Creates a @c ConditionVariable with the @p priority synchronization on @c notify and @c notify_all
	 * @param priority The @c Mutex priority used for @c notify and @c notify_all
	 */
	constexpr explicit ConditionVariable(std::optional<IsrPriority> priority = std::nullopt) :
			m_mutex(priority)
	{

	}

	/**
	 * @brief Notify the first @c Task in the waiting list if there is any, releasing it from its wait state to the ready state
	 */
	void notify_one();

	/**
	 * @brief Notify all @c Task in the waiting list, releasing them from their wait state to the ready state
	 */
	void notify_all();

	/**
	 * @brief Wait on a @c ConditionVariable with no time limit nor @c Mutex synchronization
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	void wait();

	/**
	 * @brief Wait on a @c ConditionVariable with no time limit with @c Mutex synchronization
	 * @param mutex The @c Mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	void wait(Mutex& mutex);

	/**
	 * @brief Wait on a @c ConditionVariable with a timeout and no @c Mutex synchronization
	 * @param timeout The time limit of the wait. If the @c ConditionVariable has not been notified for @p timeout then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c ConditionVariable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	std::cv_status wait_for(duration timeout);

	/**
	 * @brief Wait on a @c ConditionVariable with a timeout and @c Mutex synchronization
	 * @param mutex The @c Mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout The time limit of the wait. If the @c ConditionVariable has not been notified for @p timeout then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c ConditionVariable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	std::cv_status wait_for(Mutex& mutex, duration timeout);

	/**
	 * @brief Wait on a @c ConditionVariable with a timeout and @c Mutex synchronization
	 * @param timeout_time The time limit of the wait. If the @c ConditionVariable has not been notified before @p timeout_time then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c ConditionVariable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	std::cv_status wait_until(time_point timeout_time);

	/**
	 * @brief Wait on a @c ConditionVariable with a timeout and @c Mutex synchronization
	 * @param mutex The @c Mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout_time The time limit of the wait. If the @c ConditionVariable has not been notified before @p timeout_time then the task will be released with @c std::cv_status::timeout result
	 * @return @c std::cv_status::no_timeout if the @c ConditionVariable has been notified before @p timeout, @c std::cv_status::timeout otherwise
	 * @warning Can only be called from a @c Task, should never be called from an interrupt service routine
	 */
	std::cv_status wait_until(Mutex& mutex, time_point timeout_time);

private:

	Mutex m_mutex;
	EmbeddedList<TaskControlBlock, TaskLists::Waiting> m_waitingList;

	void addWaiting(TaskControlBlock& task);
	void removeWaiting(TaskControlBlock& task);
};
}
