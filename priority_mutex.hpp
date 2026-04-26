/**
 ******************************************************************************
 * @file    priority_mutex.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Replacement for @c std::mutex
 *
 *			It is based on:
 *			 - critical sections for @c Task only exclusion
 *			 - @c BASEPRI register (interrupt masking) for interrupt service
 *			 routine exclusion
 *
 *			It is a @c BasicLockable type, which means it only has @c lock and
 *			@c unlock.
 *			These cannot fail. Moreover, they are guaranteed to be lock free.
 *
 *			It is not copy constructible not copy assignable, only move
 *			constructible and assignable.
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

#include <atomic>
#include <mutex>
#include <cassert>
#include <optional>

#include "isr_priority.hpp"
#include "critical_section.hpp"

namespace opsy
{

/**
 * @brief A mutual exclusive lock.
 * It is used to protect shared data from being simultaneously accessed by multiple @c Task or interrupt service routines
 * @remark It is a replacement for @c std::mutex
 */
class PriorityMutex
{
	friend class Scheduler;

public:

	/**
	 * @brief Creates a new @c Mutex that locks interrupt service routine up to @p priority, or only @c Task if no @p priority is specified.
	 * @param priority The @c IsrPriority that this @c Mutex should lock, or nothing to make a @c Task only exclusion (critical section)
	 * @remark use @c IsrPriority with value 0 for a full lock (@c PRIMASK = 1)
	 */
	constexpr explicit PriorityMutex(std::optional<IsrPriority> priority = std::nullopt) :
			priority_(priority)
	{
	}

	PriorityMutex(const PriorityMutex&) = delete;
	void operator=(const PriorityMutex&) = delete;

	/**
	 * @brief Constructs a @c Mutex by moving data from another @c Mutex
	 * @param from The @c Mutex to move data from
	 */
	constexpr PriorityMutex(PriorityMutex&& from) :
			locked_(from.locked_), previous_lock_(from.previous_lock_), critical_section_(std::move(from.critical_section_)), priority_(from.priority_)
	{
	}

	/**
	 * @brief Assigns a @c Mutex by moving data from another @c Mutex
	 * @param from The @c Mutex to move data from
	 * @return A reference to @c this
	 * @warning The current @c Mutex must NOT be locked before being assigned
	 */
	PriorityMutex& operator=(PriorityMutex&& from)
	{
		assert(!locked_); // trying to override a locked mutex !
		locked_ = from.locked_;
		previous_lock_ = from.previous_lock_;
		critical_section_ = std::move(from.critical_section_);
		priority_ = from.priority_;
		from.locked_ = false;
		return *this;
	}

	/**
	 * @brief Gets the @c IsrPriority this @c Mutex locks, or @c std::nullopt if it is only a @c Task exclusion
	 * @return The @c IsrPriority this @c Mutex locks, or @c std::nullopt if it is only a @c Task exclusion
	 */
	constexpr std::optional<IsrPriority> priority() const
	{
		return priority_;
	}

	/**
	 * @brief Takes a lock on this @c Mutex
	 * @remark Defined inline at the bottom of @c scheduler.hpp (calls into
	 *         @c Scheduler, @c Hooks and @c CortexM, see the cycle-breaking
	 *         note there).
	 */
	void lock();

	/**
	 * @brief Releases the lock on this @c Mutex
	 * @remark Defined inline at the bottom of @c scheduler.hpp (see @c lock).
	 */
	void unlock();

private:

	/**
	 * @brief Re-acquire the lock from @c PendSV when the owning task is resumed
	 * @param section The @c CriticalSection ownership transferred from the scheduler
	 * @return The preemption priority requested by the mutex (used by @c PendSV to set @c BASEPRI)
	 * @remark Defined inline at the bottom of @c scheduler.hpp.
	 */
	uint32_t relock_from_pend_sv(CriticalSection section);

	/**
	 * @brief Release the hardware portion of the lock from a service call, leaving @c locked_ untouched
	 * @remark Used during @c ConditionVariable::wait so the scheduler can atomically
	 *         release the mutex and put the task to sleep. Defined inline at the
	 *         bottom of @c scheduler.hpp.
	 */
	void release_from_service_call();

	bool locked_ = false;
	IsrPriority previous_lock_ = IsrPriority(0);
	CriticalSection critical_section_;
	std::optional<IsrPriority> priority_;
};

}
