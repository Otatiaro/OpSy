/**
 ******************************************************************************
 * @file    PriorityMutex.hpp
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

#include "IsrPriority.hpp"
#include "CriticalSection.hpp"

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
			m_priority(priority)
	{
	}

	PriorityMutex(const PriorityMutex&) = delete;
	void operator=(const PriorityMutex&) = delete;

	/**
	 * @brief Constructs a @c Mutex by moving data from another @c Mutex
	 * @param from The @c Mutex to move data from
	 */
	constexpr PriorityMutex(PriorityMutex&& from) :
			m_locked(from.m_locked), m_previousLock(from.m_previousLock), m_criticalSection(std::move(from.m_criticalSection)), m_priority(from.m_priority)
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
		assert(!m_locked); // trying to override a locked mutex !
		m_locked = from.m_locked;
		m_previousLock = from.m_previousLock;
		m_criticalSection = std::move(from.m_criticalSection);
		m_priority = from.m_priority;
		from.m_locked = false;
		return *this;
	}

	/**
	 * @brief Gets the @c IsrPriority this @c Mutex locks, or @c std::nullopt if it is only a @c Task exclusion
	 * @return The @c IsrPriority this @c Mutex locks, or @c std::nullopt if it is only a @c Task exclusion
	 */
	constexpr std::optional<IsrPriority> priority() const
	{
		return m_priority;
	}

	/**
	 * @brief Takes a lock on this @c Mutex
	 */
	void lock();

	/**
	 * @brief Releases the lock on this @c Mutex
	 */
	void unlock();

private:

	uint32_t reLockFromPendSv(CriticalSection section);
	void releaseFromServiceCall();

	bool m_locked = false;
	IsrPriority m_previousLock = IsrPriority(0);
	CriticalSection m_criticalSection;
	std::optional<IsrPriority> m_priority;
};

}
