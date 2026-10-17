/**
 ******************************************************************************
 * @file    isr_lock.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Masking exclusion between a task and an interrupt handler
 *
 *			It is based on:
 *			 - critical sections for @c task only exclusion
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
#include "opsy_assert.hpp"
#include <optional>

#include "isr_priority.hpp"
#include "scheduler.hpp"

namespace opsy
{

/**
 * @brief A mutual exclusive lock.
 * It is used to protect shared data from being simultaneously accessed by multiple @c task or interrupt service routines
 * @remark It is a replacement for @c std::mutex
 */
class isr_lock
{
	friend class scheduler;

public:

	/**
	 * @brief Creates a new @c isr_lock that holds off interrupt service routines up to @p priority, or only @c task if no @p priority is specified.
	 * @param priority The @c isr_priority that this @c isr_lock should lock, or nothing to make a @c task only exclusion (critical section)
	 * @remark use @c isr_priority with value 0 for a full lock (@c PRIMASK = 1)
	 */
	constexpr explicit isr_lock(std::optional<isr_priority> priority = std::nullopt) :
			priority_(priority)
	{
	}

	isr_lock(const isr_lock&) = delete;
	void operator=(const isr_lock&) = delete;

	/**
	 * @brief Constructs an @c isr_lock by moving data from another
	 * @param from The @c isr_lock to move data from
	 */
	constexpr isr_lock(isr_lock&& from) :
			locked_(from.locked_), previous_lock_(from.previous_lock_), critical_section_(std::move(from.critical_section_)), priority_(from.priority_)
	{
		// As in operator=(isr_lock&&): the lock moved out with the
		// critical_section handle, so the source no longer holds it. Leaving
		// locked_ set there would let its unlock() -- or its destructor --
		// release a section this object believes it owns.
		from.locked_ = false;
	}

	/**
	 * @brief Assigns an @c isr_lock by moving data from another
	 * @param from The @c isr_lock to move data from
	 * @return A reference to @c this
	 * @warning The current @c isr_lock must NOT be locked before being assigned
	 */
	isr_lock& operator=(isr_lock&& from)
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
	 * @brief Gets the @c isr_priority this @c isr_lock holds off, or @c std::nullopt if it is only a @c task exclusion
	 * @return The @c isr_priority this @c isr_lock locks, or @c std::nullopt if it is only a @c task exclusion
	 */
	constexpr std::optional<isr_priority> priority() const
	{
		return priority_;
	}

	/**
	 * @brief Takes a lock on this @c isr_lock
	 */
	/**
	 * @warning NOT recursive, and unlike @c std::mutex the failure is silent.
	 *          A second @c lock() from the task that already holds it receives
	 *          an invalid @c critical_section handle and assigns it over the
	 *          valid one, clearing that handle while the scheduler's
	 *          @c critical_section_ flag stays set. Every later context switch
	 *          is then refused and the system freezes on the current task, with
	 *          no trap and no fault. A typical @c std::mutex deadlocks instead,
	 *          which at least points at the culprit. There is no
	 *          @c recursive_mutex here.
	 *
	 * @warning Releases must be strictly LIFO across mutexes. Exclusion is a
	 *          single global critical section rather than a per-object lock, so
	 *          with @c a.lock(); @c b.lock(); the handle @c b holds is already
	 *          invalid, and @c a.unlock() releases the section while @c b still
	 *          believes it holds one — preemption resumes and @c b protects
	 *          nothing. @c std::mutex allows any release order.
	 *
	 * @warning Do not sleep or wait while holding one: @ref opsy::sleep_for and
	 *          the sleep service call assert that no critical section is held.
	 *          Use @c condition_variable::wait(mutex&) , which releases it
	 *          atomically with the wait.
	 */
	void lock();

	/**
	 * @brief Releases the lock on this @c isr_lock
	 */
	void unlock();

private:

	/**
	 * @brief Re-acquire the lock from @c PendSV when the owning task is resumed
	 * @param section The @c critical_section ownership transferred from the scheduler
	 * @return The preemption priority requested by the mutex (used by @c PendSV to set @c BASEPRI)
	 */
	uint32_t relock_from_pend_sv(critical_section section);

	/**
	 * @brief Release the hardware portion of the lock from a service call, leaving @c locked_ untouched
	 * @remark Used during @c condition_variable::wait so the scheduler can atomically
	 *         release the mutex and put the task to sleep.
	 */
	void release_from_service_call();

	bool locked_ = false;
	isr_priority previous_lock_ = isr_priority(0);
	critical_section critical_section_;
	std::optional<isr_priority> priority_;
};

/**
 * @brief Takes a lock on this @c mutex
 *
 * Behavior depends on @c priority_:
 *   - @c std::nullopt : task-only exclusion via a @c critical_section.
 *   - value 0         : full lock (@c PRIMASK = 1, all maskable interrupts off).
 *   - value > 0       : interrupt masking via @c BASEPRI up to that priority,
 *                       plus a @c critical_section if called from a task.
 */
// All accesses below use the unchecked dereference (operator* / operator->) on
// std::optional<isr_priority>, NOT optional::value(). value() throws
// bad_optional_access on an empty optional, which under -fno-exceptions is
// lowered to a call to abort() — and through it the whole newlib raise +
// _impure_ptr + __sf chain (~700 B of flash + 312 B of BSS for the static
// FILE pool that we never use). The compiler can't elide that branch even
// when value() is inside an if (priority_.has_value()) guard, because it
// doesn't correlate has_value() with the subsequent value() across function
// calls. Operator* / operator-> have a precondition (the optional is engaged)
// and emit nothing for the empty case, so the abort branch goes away.
inline void isr_lock::lock()
{
	if (priority_.has_value())
	{
		if (priority_->value() == 0)
		{
			assert(!cortex_m::is_primask());

			hooks::enter_full_lock();
			cortex_m::disable_interrupts();
		}
		else
		{
			if (cortex_m::ipsr() == 0) // ask for critical section only when in task
				critical_section_ = scheduler::try_critical_section();
			else
				// current_priority() returns nullopt only when ipsr() == 0; we are in the else
				// branch where ipsr() != 0, so the optional is necessarily engaged here too.
				assert(cortex_m::current_priority()->masked_value<preemption_bits>() >= priority_->masked_value<preemption_bits>()); // in interrupt, check that current priority level is lower than what is needed to lock, because if an interrupt with higher priority participate in the lock, synchronization cannot be guaranteed

			hooks::enter_priority_lock(*priority_);
			previous_lock_ = cortex_m::set_basepri(isr_priority(priority_->masked_value<preemption_bits>()));
			assert(previous_lock_.masked_value<preemption_bits>() <= priority_->masked_value<preemption_bits>()); // a new mutex lock cannot LOWER the priority mutex (basepri)
		}
	}
	else
	{
		assert(cortex_m::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		critical_section_ = scheduler::try_critical_section();
	}

	locked_ = true;
}

/**
 * @brief Releases the lock on this @c mutex
 * @remark Mirror of @c lock: undoes whatever scheme was selected at lock time.
 *         No-op if the mutex is not currently locked.
 */
inline void isr_lock::unlock()
{
	if (!locked_)
		return;

	locked_ = false;

	if (priority_.has_value())
	{
		// See comment on lock(): * / -> avoids the abort path that
		// optional::value() would emit under -fno-exceptions.
		if (priority_->value() == 0)
		{
			assert(cortex_m::is_primask());
			cortex_m::enable_interrupts();
			hooks::exit_full_lock();
		}
		else
		{
#ifndef NDEBUG
			auto was = cortex_m::set_basepri(previous_lock_);
			assert(was.masked_value<preemption_bits>() == priority_->masked_value<preemption_bits>());
#else
			cortex_m::set_basepri(previous_lock_);
#endif
			hooks::exit_priority_lock();
			auto tmp = std::move(critical_section_);
		}
	}
	else
	{
		assert(cortex_m::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		auto tmp = std::move(critical_section_);
	}
}

/**
 * @brief Re-acquires this mutex from @c PendSV when the owning task is restarted
 * @param section The @c critical_section ownership transferred from the scheduler
 * @return The preemption priority requested by the mutex
 * @remark Called from @c scheduler::pend_sv_handler when restoring a task that
 *         was put to sleep while holding a mutex (see @c condition_variable::wait
 *         with mutex). The scheduler hands the previously-held @c critical_section
 *         back to the mutex.
 */
inline uint32_t isr_lock::relock_from_pend_sv(critical_section section)
{
	critical_section_ = std::move(section); // it is a task, so critical section is mandatory

	if (priority_.has_value())
	{
		// See comment on lock(): * / -> avoids the abort path that
		// optional::value() would emit under -fno-exceptions.
		assert(priority_->value() != 0); // 0 is full mutex, can't be preempted by system
		previous_lock_ = isr_priority(0);
	}

	locked_ = true;
	return priority_.value_or(isr_priority(0)).masked_value<preemption_bits>();
}

/**
 * @brief Releases the hardware portion of the lock from a service call
 * @remark Used during @c condition_variable::wait to atomically release the
 *         mutex and put the task to sleep. @c locked_ is left untouched on
 *         purpose: the task still owns the logical lock and the scheduler
 *         will restore it via @c relock_from_pend_sv when the task wakes up.
 */
inline void isr_lock::release_from_service_call()
{
	assert(locked_);

	if (priority_.has_value())
	{
		// See comment on lock(): * / -> avoids the abort path that
		// optional::value() would emit under -fno-exceptions.
		if (priority_->value() == 0)
		{
			assert(cortex_m::is_primask());
			cortex_m::enable_interrupts();
		}
		else
		{
#ifndef NDEBUG
			auto was = cortex_m::set_basepri(previous_lock_);
			assert(was.masked_value<preemption_bits>() == priority_->masked_value<preemption_bits>());
#else
			cortex_m::set_basepri(previous_lock_);
#endif
		}
	}
}

}
