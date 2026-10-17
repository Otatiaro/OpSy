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

#include <cstddef>
#include <optional>

#include "config.hpp"
#include "embedded_list.hpp"
#include "task.hpp"
#include "cortex_m.hpp"
#include "mutex.hpp"
#include "isr_lock.hpp"
#include "scheduler.hpp"

#include <variant>

namespace opsy
{

/**
 * @brief Result of a timed wait operation on a @c condition_variable
 * @remark Mirrors @c cv_status from @c <condition_variable>, but defined
 *         in @c namespace @c opsy because @c <condition_variable> is unavailable
 *         on bare-metal Cortex-M (no OS threading support in the C++ runtime)
 *         and adding new types to @c namespace @c std is undefined behaviour
 *         per [namespace.std].
 */
enum class cv_status
{
	no_timeout = 0, ///< The condition variable was notified before the timeout
	timeout    = 1  ///< The timeout elapsed before the condition variable was notified
};

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
			notify_lock_(priority)
	{

	}

	/**
	 * @brief Notify the first @c task in the waiting list if there is any, releasing it from its wait state to the ready state
	 */
	void notify_one();

	/**
	 * @brief Notify all @c task in the waiting list, releasing them from their wait state to the ready state
	 */
	void notify_all();

	/**
	 * @brief Wait on a @c condition_variable with no time limit nor @c mutex synchronization
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	/**
	 * @warning No mutex, so no way to test a predicate and go to sleep
	 *          atomically — which is the whole reason
	 *          @c std::condition_variable::wait requires a lock. The
	 *          notification that lands between the test and the wait is lost,
	 *          and the task blocks for good:
	 *          @code
	 *          if (!ready)   // notify_one() here...
	 *              cv.wait(); // ...and this never returns
	 *          @endcode
	 *          Use this overload only to wait on a bare event with no shared
	 *          state behind it. Anything guarding a predicate wants
	 *          @ref wait(mutex&) .
	 *
	 * @remark OpSy has no spurious wake-ups: the only paths that resume a
	 *         waiter are a notification and a timeout, each setting the
	 *         @c cv_status it reports. The predicate loop the standard forces
	 *         on you is not needed here — but code meant to be portable should
	 *         keep it anyway.
	 */
	void wait();

	/**
	 * @brief Wait on a @c condition_variable with no time limit with @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	void wait(isr_lock& mtx);

	/**
	 * @brief Waits, atomically releasing an @c opsy::mutex and taking it back on wake
	 * @param mtx The mutex held by the calling task
	 *
	 * @remark The counterpart of @c std::condition_variable::wait(unique_lock&)
	 *         and the overload to prefer: releasing the mutex and going to
	 *         sleep happens as one step, so a notification cannot be missed in
	 *         between — which is exactly what @ref wait() with no lock cannot
	 *         promise.
	 *
	 * @remark On wake the mutex may be held by someone else, in which case the
	 *         task blocks on it and only returns once it owns it again.
	 */
	void wait(mutex& mtx);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and no @c mutex synchronization
	 * @param timeout The time limit of the wait. If the @c condition_variable has not been notified for @p timeout then the task will be released with @c cv_status::timeout result
	 * @return @c cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	[[nodiscard]] cv_status wait_for(duration timeout);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout The time limit of the wait. If the @c condition_variable has not been notified for @p timeout then the task will be released with @c cv_status::timeout result
	 * @return @c cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	[[nodiscard]] cv_status wait_for(isr_lock& mtx, duration timeout);

	/**
	 * @brief Waits with a timeout, releasing an @c opsy::mutex and taking it back
	 * @param mtx The mutex held by the calling task
	 * @param timeout The time limit
	 * @return @c cv_status::no_timeout if notified in time, @c cv_status::timeout otherwise
	 */
	[[nodiscard]] cv_status wait_for(mutex& mtx, duration timeout);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param timeout_time The time limit of the wait. If the @c condition_variable has not been notified before @p timeout_time then the task will be released with @c cv_status::timeout result
	 * @return @c cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	[[nodiscard]] cv_status wait_until(time_point timeout_time);

	/**
	 * @brief Wait on a @c condition_variable with a timeout and @c mutex synchronization
	 * @param mutex The @c mutex, already locked by the task, that will be atomically released by OpSy, then re-acquired when the task is released
	 * @param timeout_time The time limit of the wait. If the @c condition_variable has not been notified before @p timeout_time then the task will be released with @c cv_status::timeout result
	 * @return @c cv_status::no_timeout if the @c condition_variable has been notified before @p timeout, @c cv_status::timeout otherwise
	 * @warning Can only be called from a @c task, should never be called from an interrupt service routine
	 */
	[[nodiscard]] cv_status wait_until(isr_lock& mtx, time_point timeout_time);

	/**
	 * @brief Waits until a deadline, releasing an @c opsy::mutex and taking it back
	 * @param mtx The mutex held by the calling task
	 * @param timeout_time The absolute deadline
	 * @return @c cv_status::no_timeout if notified in time, @c cv_status::timeout otherwise
	 */
	[[nodiscard]] cv_status wait_until(mutex& mtx, time_point timeout_time);

private:

	/**
	 * @brief Issues the wait service call, whatever lock was recorded
	 * @param timeout The time limit, negative meaning none
	 * @return What the scheduler wrote back as the wake reason
	 *
	 * @remark One place doing the service call, six public overloads deciding
	 *         what to release beforehand. Which lock — if any — is recorded on
	 *         the calling task before this runs, so the handler reads a typed
	 *         variant instead of a pointer with no way to tell its kind.
	 */
	cv_status do_wait(duration timeout);

	isr_lock notify_lock_;   // guards waiting_list_ against a notifying ISR
	embedded_list<task_control_block, task_lists::waiting> waiting_list_;

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

/**
 * @brief Wakes one waiting task, if any
 * @remark Synchronization is performed by locking @c notify_lock_ around the
 *         pop+wakeup pair so notifications cannot race with @c wait.
 */
inline void condition_variable::notify_one()
{
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= cortex_m::current_priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>()); // do not call notify when priority is higher than the mutex priority !
	assert(notify_lock_.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // the lock may not mask OpSy's own level: nothing could be notified from there, and a wait under it could not take its service call

	{
		std::lock_guard<isr_lock> guard(notify_lock_);

		hooks::condition_variable_notify_one(*this);

		if (waiting_list_.empty())
			return;
		else
		{
			task_control_block& task = waiting_list_.front();
			waiting_list_.pop_front();
			scheduler::wake_up(task, *this);
		}
	}
}

/**
 * @brief Wakes every waiting task
 * @remark Same synchronization model as @c notify_one.
 */
inline void condition_variable::notify_all()
{
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= cortex_m::current_priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>()); // do not call notify when priority is higher than the mutex priority !
	assert(notify_lock_.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // the lock may not mask OpSy's own level: nothing could be notified from there, and a wait under it could not take its service call

	{
		std::lock_guard<isr_lock> guard(notify_lock_);

		hooks::condition_variable_notify_all(*this);

		while (!waiting_list_.empty())
		{
			task_control_block& task = waiting_list_.front();
			waiting_list_.pop_front();
			scheduler::wake_up(task, *this);
		}
	}
}

/**
 * @brief Issues the wait service call
 */
/**
 * @brief Records on the running task the lock this wait must release
 */
static inline void record_lock(std::variant<std::monostate, mutex*, isr_lock*> lock)
{
	auto* current = scheduler::current_task();
	assert(current != nullptr);
	current->record_released_lock(lock);
}

inline cv_status condition_variable::do_wait(duration timeout)
{
	const auto count = timeout.count();
	uint32_t result;

	// "memory" clobber: see scheduler::trigger_hard_switch.
	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, %[count_lo] \n\t"
			"mov r2, %[count_hi] \n\t"
			"svc %[immediate] \n\t"
			"mov %[result], r0"
			: [result] "=r" (result)
			: [immediate] "I" (scheduler::service_call_number::wait),
			  [this_ptr] "r" (this),
			  [count_lo] "r" (static_cast<uint32_t>(count)),
			  [count_hi] "r" (static_cast<uint32_t>(count >> 32))
			: "r0", "r1", "r2", "memory");

	assert(result == 0 || result == 1);
	return static_cast<cv_status>(result);
}

inline void condition_variable::wait()
{
	assert(cortex_m::ipsr() == 0); // cannot call in interrupt

	record_lock(std::monostate{});
	(void) do_wait(duration{-1}); // negative count means "no timeout"
}

inline void condition_variable::wait(isr_lock& mtx)
{
	assert(cortex_m::ipsr() == 0); // cannot call in interrupt
	assert(notify_lock_.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // the lock may not mask OpSy's own level: nothing could be notified from there, and a wait under it could not take its service call
	assert(mtx.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // held across the service call, so it may not mask it: the SVC would escalate to HardFault

	record_lock(&mtx);
	(void) do_wait(duration{-1});
}

inline void condition_variable::wait(mutex& mtx)
{
	assert(cortex_m::ipsr() == 0);
	assert(mtx.owner_ == scheduler::current_task()); // must be held by the caller

	record_lock(&mtx);
	(void) do_wait(duration{-1});
}

inline cv_status condition_variable::wait_for(duration timeout)
{
	assert(cortex_m::ipsr() == 0); // cannot call in interrupt

	// A negative count is how the service call encodes "no timeout at all"
	// (wait() passes -1), so it must never reach the SVC as a duration. It
	// gets here from wait_until() with a deadline already in the past, where
	// the standard answer -- and std::condition_variable's -- is to report the
	// timeout straight away rather than block forever.
	if (timeout.count() < 0)
		return cv_status::timeout;

	record_lock(std::monostate{});
	return do_wait(timeout);
}

inline cv_status condition_variable::wait_for(isr_lock& mtx, duration timeout)
{
	assert(cortex_m::ipsr() == 0); // cannot call in interrupt
	assert(notify_lock_.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // the lock may not mask OpSy's own level: nothing could be notified from there, and a wait under it could not take its service call
	assert(mtx.priority().value_or(scheduler::os_callable_priority).masked_value<preemption_bits>() >= scheduler::os_callable_priority.masked_value<preemption_bits>()); // held across the service call, so it may not mask it: the SVC would escalate to HardFault

	if (timeout.count() < 0)
		return cv_status::timeout;

	record_lock(&mtx);
	return do_wait(timeout);
}

inline cv_status condition_variable::wait_for(mutex& mtx, duration timeout)
{
	assert(cortex_m::ipsr() == 0);
	assert(mtx.owner_ == scheduler::current_task());

	if (timeout.count() < 0)
		return cv_status::timeout;

	record_lock(&mtx);
	return do_wait(timeout);
}

/**
 * @brief Waits on this condition variable until an absolute time point
 * @param timeout_time The absolute time at which the wait expires
 * @return @c cv_status::no_timeout if notified in time, @c cv_status::timeout otherwise
 * @remark A @p timeout_time already in the past returns @c cv_status::timeout
 *         immediately, without blocking.
 */
inline cv_status condition_variable::wait_until(time_point timeout_time)
{
	return wait_for(timeout_time - scheduler::now());
}

inline cv_status condition_variable::wait_until(isr_lock& mtx, time_point timeout_time)
{
	return wait_for(mtx, timeout_time - scheduler::now());
}

inline cv_status condition_variable::wait_until(mutex& mtx, time_point timeout_time)
{
	return wait_for(mtx, timeout_time - scheduler::now());
}

}
