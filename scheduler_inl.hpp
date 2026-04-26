/**
 ******************************************************************************
 * @file    scheduler_inl.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Inline definitions for header-only OpSy primitives
 *
 *          @c CriticalSection, @c PriorityMutex and @c ConditionVariable have
 *          no static state of their own, so they live fully in headers. The
 *          catch is that several of their member functions call into
 *          @c Scheduler (and conversely @c Scheduler holds them by value or
 *          as friends), which creates a header cycle:
 *
 *              critical_section.hpp  <-+
 *              priority_mutex.hpp    <-|-- scheduler.hpp
 *              condition_variable.hpp<-+   (transitively includes them all)
 *
 *          Defining the bodies inside their own headers is therefore
 *          impossible: the @c Scheduler class declaration must be visible
 *          first. We break the cycle by declaring the member functions in
 *          their respective headers and defining them HERE, in a file that
 *          @c scheduler.hpp includes at the very end (after the @c Scheduler
 *          class declaration is in scope).
 *
 *          Constraints / invariants this file relies on:
 *           - This file MUST only ever be included from the bottom of
 *             @c scheduler.hpp (it expects the full @c Scheduler declaration
 *             and all transitive includes to be visible).
 *           - All definitions are @c inline so the One Definition Rule holds
 *             across translation units.
 *           - @c friend class declarations on @c Scheduler grant
 *             @c CriticalSection and @c ConditionVariable access to private
 *             @c Scheduler members (e.g. @c ServiceCallNumber,
 *             @c criticalSectionEnd, @c wakeUp).
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

#ifndef OPSY_SCHEDULER_HPP_INCLUDED_
#error "scheduler_inl.hpp must only be included from the bottom of scheduler.hpp"
#endif

#include <algorithm>

namespace opsy
{

// --- CriticalSection --------------------------------------------------------

/**
 * @brief Releases the critical section lock if this handle is the valid owner
 * @remark See cycle-breaking note at the top of this file. Calls into
 *         @c Scheduler::criticalSectionEnd.
 */
inline CriticalSection::~CriticalSection()
{
	if (valid_)
		Scheduler::criticalSectionEnd();
}

// --- PriorityMutex ----------------------------------------------------------

/**
 * @brief Takes a lock on this @c Mutex
 *
 * Behavior depends on @c priority_:
 *   - @c std::nullopt : task-only exclusion via a @c CriticalSection.
 *   - value 0         : full lock (@c PRIMASK = 1, all maskable interrupts off).
 *   - value > 0       : interrupt masking via @c BASEPRI up to that priority,
 *                       plus a @c CriticalSection if called from a task.
 */
inline void PriorityMutex::lock()
{
	if (priority_.has_value())
	{
		if (priority_.value().value() == 0)
		{
			assert(!CortexM::isPrimask());

			Hooks::enterFullLock();
			CortexM::disableInterrupts();
		}
		else
		{
			if (CortexM::ipsr() == 0) // ask for critical section only when in task
				critical_section_ = Scheduler::criticalSection();
			else
				assert(CortexM::currentPriority().value().maskedValue<kPreemptionBits>() >= priority_.value().maskedValue<kPreemptionBits>()); // in interrupt, check that current priority level is lower than what is needed to lock, because if an interrupt with higher priority participate in the lock, synchronization cannot be guaranteed

			Hooks::enterPriorityLock(priority_.value());
			previous_lock_ = CortexM::setBasepri(IsrPriority(priority_.value().maskedValue<kPreemptionBits>()));
			assert(previous_lock_.maskedValue<kPreemptionBits>() <= priority_.value().maskedValue<kPreemptionBits>()); // a new mutex lock cannot LOWER the priority mutex (basepri)
		}
	}
	else
	{
		assert(CortexM::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		critical_section_ = Scheduler::criticalSection();
	}

	locked_ = true;
}

/**
 * @brief Releases the lock on this @c Mutex
 * @remark Mirror of @c lock: undoes whatever scheme was selected at lock time.
 *         No-op if the mutex is not currently locked.
 */
inline void PriorityMutex::unlock()
{
	if (!locked_)
		return;

	locked_ = false;

	if (priority_.has_value())
	{
		if (priority_.value().value() == 0)
		{
			assert(CortexM::isPrimask());
			CortexM::enableInterrupts();
			Hooks::exitFullLock();
		}
		else
		{
#ifndef NDEBUG
			auto was = CortexM::setBasepri(previous_lock_);
			assert(was.maskedValue<kPreemptionBits>() == priority_.value().maskedValue<kPreemptionBits>());
#else
			CortexM::setBasepri(previous_lock_);
#endif
			Hooks::exitPriorityLock();
			auto tmp = std::move(critical_section_);
		}
	}
	else
	{
		assert(CortexM::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		auto tmp = std::move(critical_section_);
	}
}

/**
 * @brief Re-acquires this mutex from @c PendSV when the owning task is restarted
 * @param section The @c CriticalSection ownership transferred from the scheduler
 * @return The preemption priority requested by the mutex
 * @remark Called from @c Scheduler::pendSvHandler when restoring a task that
 *         was put to sleep while holding a mutex (see @c ConditionVariable::wait
 *         with mutex). The scheduler hands the previously-held @c CriticalSection
 *         back to the mutex.
 */
inline uint32_t PriorityMutex::reLockFromPendSv(CriticalSection section)
{
	critical_section_ = std::move(section); // it is a task, so critical section is mandatory

	if (priority_.has_value())
	{
		assert(priority_.value().value() != 0); // 0 is full mutex, can't be preempted by system
		previous_lock_ = IsrPriority(0);
	}

	locked_ = true;
	return priority_.value_or(IsrPriority(0)).maskedValue<kPreemptionBits>();
}

/**
 * @brief Releases the hardware portion of the lock from a service call
 * @remark Used during @c ConditionVariable::wait to atomically release the
 *         mutex and put the task to sleep. @c locked_ is left untouched on
 *         purpose: the task still owns the logical lock and the scheduler
 *         will restore it via @c reLockFromPendSv when the task wakes up.
 */
inline void PriorityMutex::releaseFromServiceCall()
{
	assert(locked_);

	if (priority_.has_value())
	{
		if (priority_.value().value() == 0)
		{
			assert(CortexM::isPrimask());
			CortexM::enableInterrupts();
		}
		else
		{
#ifndef NDEBUG
			auto was = CortexM::setBasepri(previous_lock_);
			assert(was.maskedValue<kPreemptionBits>() == priority_.value().maskedValue<kPreemptionBits>());
#else
			CortexM::setBasepri(previous_lock_);
#endif
		}
	}
}

// --- ConditionVariable ------------------------------------------------------

/**
 * @brief Wakes one waiting task, if any
 * @remark Synchronization is performed by locking @c mutex_ around the
 *         pop+wakeup pair so notifications cannot race with @c wait.
 */
inline void ConditionVariable::notify_one()
{
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= CortexM::currentPriority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>()); // do not call notify when priority is higher than the mutex priority !
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	{
		std::lock_guard<Mutex> lock(mutex_);

		Hooks::conditionVariableNotifyOne(*this);

		if (waiting_list_.empty())
			return;
		else
		{
			TaskControlBlock& task = waiting_list_.front();
			waiting_list_.pop_front();
			Scheduler::wakeUp(task, *this);
		}
	}
}

/**
 * @brief Wakes every waiting task
 * @remark Same synchronization model as @c notify_one.
 */
inline void ConditionVariable::notify_all()
{
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= CortexM::currentPriority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>()); // do not call notify when priority is higher than the mutex priority !
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	{
		std::lock_guard<Mutex> lock(mutex_);

		Hooks::conditionVariableNotifyAll(*this);

		while (!waiting_list_.empty())
		{
			TaskControlBlock& task = waiting_list_.front();
			waiting_list_.pop_front();
			Scheduler::wakeUp(task, *this);
		}
	}
}

/**
 * @brief Waits on this condition variable with no timeout and no mutex
 * @remark The @c SVC instruction encodes the variant via the @c r1:r2 (timeout
 *         = -1 means none) and @c r3 (mutex pointer, 0 means none) registers.
 *         The actual handling lives in @c Scheduler::serviceCallHandler.
 */
inline void ConditionVariable::wait()
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, #-1 \n\t"  // R1:R2 = -1 (int64_t) signals no timeout
			"mov r2, #-1 \n\t"
			"mov r3, #0 \n\t"   // no mutex
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this)
			: "r0", "r1", "r2", "r3");
}

/**
 * @brief Waits on this condition variable with no timeout, atomically releasing @p mutex
 * @param mutex The @c Mutex held by the task, released atomically by OpSy and re-acquired on wake
 */
inline void ConditionVariable::wait(Mutex& mutex)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, #-1 \n\t"  // R1:R2 = -1 (int64_t) signals no timeout
			"mov r2, #-1 \n\t"
			"mov r3, %[mutex] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this), [mutex] "r" (&mutex)
			: "r0", "r1", "r2", "r3");
}

/**
 * @brief Waits on this condition variable with a timeout
 * @param timeout The time limit; the task wakes up with @c cv_status::timeout if it elapses
 * @return @c std::cv_status::no_timeout if notified in time, @c std::cv_status::timeout otherwise
 */
inline std::cv_status ConditionVariable::wait_for(duration timeout)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	const auto count = timeout.count();
	uint32_t result;
	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, %[count_lo] \n\t"  // R1:R2 = timeout count (int64_t)
			"mov r2, %[count_hi] \n\t"
			"mov r3, #0 \n\t"           // no mutex
			"svc %[immediate] \n\t"
			"mov %[result], r0"
			: [result] "=r" (result)
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait),
			  [this_ptr] "r" (this),
			  [count_lo] "r" (static_cast<uint32_t>(count)),
			  [count_hi] "r" (static_cast<uint32_t>(count >> 32))
			: "r0", "r1", "r2", "r3");

	assert(result == 0 || result == 1); // result can only be 0 or 1 (timeout or notimeout)
	return static_cast<std::cv_status>(result);
}

/**
 * @brief Waits on this condition variable with a timeout and a mutex
 * @param mutex The @c Mutex held by the task, released atomically by OpSy and re-acquired on wake
 * @param timeout The time limit; the task wakes up with @c cv_status::timeout if it elapses
 * @return @c std::cv_status::no_timeout if notified in time, @c std::cv_status::timeout otherwise
 */
inline std::cv_status ConditionVariable::wait_for(Mutex& mutex, duration timeout)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(mutex_.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	const auto count = timeout.count();
	uint32_t result;
	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, %[count_lo] \n\t"  // R1:R2 = timeout count (int64_t)
			"mov r2, %[count_hi] \n\t"
			"mov r3, %[mutex] \n\t"
			"svc %[immediate] \n\t"
			"mov %[result], r0"
			: [result] "=r" (result)
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait),
			  [this_ptr] "r" (this),
			  [count_lo] "r" (static_cast<uint32_t>(count)),
			  [count_hi] "r" (static_cast<uint32_t>(count >> 32)),
			  [mutex] "r" (&mutex)
			: "r0", "r1", "r2", "r3");

	assert(result == 0 || result == 1); // result can only be 0 or 1 (timeout or notimeout)
	return static_cast<std::cv_status>(result);
}

/**
 * @brief Waits on this condition variable until an absolute time point
 * @param timeout_time The absolute time at which the wait expires
 * @return @c std::cv_status::no_timeout if notified in time, @c std::cv_status::timeout otherwise
 */
inline std::cv_status ConditionVariable::wait_until(time_point timeout_time)
{
	return wait_for(timeout_time - Scheduler::now());
}

/**
 * @brief Waits on this condition variable until an absolute time point, with a mutex
 * @param mutex The @c Mutex held by the task, released atomically by OpSy and re-acquired on wake
 * @param timeout_time The absolute time at which the wait expires
 * @return @c std::cv_status::no_timeout if notified in time, @c std::cv_status::timeout otherwise
 */
inline std::cv_status ConditionVariable::wait_until(Mutex& mutex, time_point timeout_time)
{
	return wait_for(mutex, timeout_time - Scheduler::now());
}

// --- TaskControlBlock -------------------------------------------------------

/**
 * @brief Starts the task with the given @p entry callback
 * @param entry The @c Callback the task will execute
 * @param name Optional name for the task
 * @return @c true if the task was successfully started, @c false if it was already active
 * @remark Sets up the initial stack so the task starts in @c taskStarter, with
 *         @c Scheduler::terminateTask wired as the link register so the task
 *         terminates cleanly when its entry callback returns. The +2 offset
 *         on @c terminateTask skips the leading @c nop in its naked body
 *         (kept so GDB shows a sensible link return).
 */
inline bool TaskControlBlock::start(Callback<void(void)> && entry, const char* name)
{
	if(active_.exchange(true)) // we put true in the boolean value, and were expecting false, so we return if exchange return true
		return false;

	entry_ = std::move(entry);
	name_ = name;

#ifndef NDEBUG
	std::fill(stack_base_, stack_base_ + stack_size_, Dummy);
#endif

	stack_pointer_ = &stack_base_[stack_size_ - 1]; // this pointer is reserved to stop stack trace unwinding
	stack_base_[stack_size_ - 1] = 0; // keep this pointer to zero to stop stack trace

	stack_pointer_ -= sizeof(StackFrame) / sizeof(StackItem);
	const auto frame = reinterpret_cast<StackFrame*>(stack_pointer_);

	frame->psr = 1 << 24;
	frame->pc = reinterpret_cast<CodePointer>(taskStarter);
	frame->lr = reinterpret_cast<CodePointer>(reinterpret_cast<uint32_t>(Scheduler::terminateTask) + 2); // the 2 byte offset is to skip the "nop" instruction at the beginning of terminate task. This is to make GDB happy about the link return ...
	frame->r0 = reinterpret_cast<uint32_t>(this);

	stack_pointer_ -= sizeof(Context) / sizeof(StackItem);
	const auto context = reinterpret_cast<Context*>(stack_pointer_);
	context->control = 0b10;
	context->lr = 0xFFFFFFFD;

	Scheduler::addTask(*this);
	return true;
}

/**
 * @brief Stops the task immediately
 * @return @c true if the task was running and has been signalled to terminate, @c false if it was already inactive
 * @remark Issues an @c SVC carrying @c ServiceCallNumber::Terminate; the actual
 *         teardown happens in @c Scheduler::serviceCallHandler.
 */
inline bool TaskControlBlock::stop()
{
	if(!isStarted()) // can only stop an active task
		return false;

	asm volatile(
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Terminate), [task] "r" (this)
			: "r0");
	return true;
}

/**
 * @brief Updates the task priority
 * @param newPriority The new priority value
 * @remark No-op if the priority is unchanged. Otherwise delegates to
 *         @c Scheduler::updatePriority which may re-order ready/waiting lists
 *         and request a context switch.
 */
inline void TaskControlBlock::priority(Priority newPriority)
{
	if(newPriority == priority_)
		return;
	else
		Scheduler::updatePriority(*this, newPriority);
}

/**
 * @brief Trampoline executed at the start of every task
 * @param thisPtr Pointer to the @c TaskControlBlock being started
 * @remark Calls the user-provided entry callback, then terminates the task
 *         via @c Scheduler::terminateTask. The link register set up in
 *         @c start would also lead here, this trampoline is for the normal
 *         (non-fault) entry path.
 */
inline void TaskControlBlock::taskStarter(TaskControlBlock* thisPtr)
{
	thisPtr->entry_();
	Scheduler::terminateTask(thisPtr);
}

}
