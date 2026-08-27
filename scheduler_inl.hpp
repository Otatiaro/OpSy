/**
 ******************************************************************************
 * @file    scheduler_inl.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Inline definitions for header-only OpSy primitives
 *
 *          @c critical_section, @c isr_lock and @c condition_variable have
 *          no static state of their own, so they live fully in headers. The
 *          catch is that several of their member functions call into
 *          @c scheduler (and conversely @c scheduler holds them by value or
 *          as friends), which creates a header cycle:
 *
 *              critical_section.hpp  <-+
 *              isr_lock.hpp    <-|-- scheduler.hpp
 *              condition_variable.hpp<-+   (transitively includes them all)
 *
 *          Defining the bodies inside their own headers is therefore
 *          impossible: the @c scheduler class declaration must be visible
 *          first. We break the cycle by declaring the member functions in
 *          their respective headers and defining them HERE, in a file that
 *          @c scheduler.hpp includes at the very end (after the @c scheduler
 *          class declaration is in scope).
 *
 *          Constraints / invariants this file relies on:
 *           - This file MUST only ever be included from the bottom of
 *             @c scheduler.hpp (it expects the full @c scheduler declaration
 *             and all transitive includes to be visible).
 *           - All definitions are @c inline so the One Definition Rule holds
 *             across translation units.
 *           - @c friend class declarations on @c scheduler grant
 *             @c critical_section and @c condition_variable access to private
 *             @c scheduler members (e.g. @c service_call_number,
 *             @c critical_section_end, @c wake_up).
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

// --- critical_section --------------------------------------------------------

/**
 * @brief Releases the critical section lock if this handle is the valid owner
 * @remark See cycle-breaking note at the top of this file. Calls into
 *         @c scheduler::critical_section_end.
 */
inline critical_section::~critical_section()
{
	if (valid_)
		scheduler::critical_section_end();
}

// --- isr_lock ----------------------------------------------------------

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

// --- condition_variable ------------------------------------------------------

/**
 * @brief Wakes one waiting task, if any
 * @remark Synchronization is performed by locking @c notify_lock_ around the
 *         pop+wakeup pair so notifications cannot race with @c wait.
 */
inline void condition_variable::notify_one()
{
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= cortex_m::current_priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>()); // do not call notify when priority is higher than the mutex priority !
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= scheduler::service_call_priority.masked_value<preemption_bits>()); // mutex priority can't be higher than service call

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
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= scheduler::service_call_priority.masked_value<preemption_bits>()); // mutex priority can't be higher than service call

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
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= scheduler::service_call_priority.masked_value<preemption_bits>()); // mutex priority can't be higher than service call

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
	assert(notify_lock_.priority().value_or(scheduler::service_call_priority).masked_value<preemption_bits>() >= scheduler::service_call_priority.masked_value<preemption_bits>()); // mutex priority can't be higher than service call

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

// --- mutex --------------------------------------------------------------------

/**
 * @brief Whether this task is sitting in the scheduler's ready list
 */
inline bool task_control_block::is_ready() const
{
	// next_task_ matters as much as current_task_: a task do_switch() has
	// elected is popped out of ready_ and sits in no list at all until PendSV
	// runs. Treating it as ready makes ready_.erase() a silent no-op and the
	// following insert link it a second time.
	return is_started()
		&& this != scheduler::current_task()
		&& this != scheduler::next_task()
		&& blocked_on_ == nullptr
		&& waiting_ == nullptr
		&& !wait_until_.has_value();
}

/**
 * @brief Acquires the mutex, blocking until it is free
 *
 * @remark No service call. Masking to @c service_call_priority is enough and
 *         is what every other writer of the scheduler's mutex state already
 *         does — see the note on service calls in @c docs/architecture.md .
 *         The scheduler's critical section alone would *not* be: it masks
 *         nothing, so SysTick could run @c resume_waiter and hand the same
 *         mutex to a timing-out waiter in the middle of the check.
 */
inline void mutex::lock()
{
	assert(cortex_m::ipsr() == 0);            // blocking is meaningless in an ISR
	assert(scheduler::is_os_callable());

	const auto previous = cortex_m::set_basepri(scheduler::service_call_priority);

	auto* self = scheduler::current_task();
	assert(self != nullptr);
	assert(owner_ != self);                   // not recursive, like std::mutex

	if (owner_ == nullptr)
	{
		scheduler::take_mutex(*this, *self);
		cortex_m::set_basepri(previous);
		return;
	}

	// Held: block on it. do_switch() only leaves the caller out of ready_ if
	// current_task_ is already cleared, which is how a task suspends itself
	// without a handler.
	scheduler::block_on(*this, *self);
	scheduler::clear_current_task();
	scheduler::do_switch();

	cortex_m::set_basepri(previous);          // PendSV runs here, and this task
	                                          // resumes below owning the mutex
	assert(owner_ == scheduler::current_task());
}

/**
 * @brief Acquires the mutex if it is free, without blocking
 */
inline bool mutex::try_lock()
{
	assert(cortex_m::ipsr() == 0);
	assert(scheduler::is_os_callable());

	const auto previous = cortex_m::set_basepri(scheduler::service_call_priority);

	auto* self = scheduler::current_task();
	assert(self != nullptr);
	assert(owner_ != self);                   // not recursive

	const bool taken = owner_ == nullptr;
	if (taken)
		scheduler::take_mutex(*this, *self);

	cortex_m::set_basepri(previous);
	return taken;
}

/**
 * @brief Releases the mutex and hands it to the highest-priority waiter
 */
inline void mutex::unlock()
{
	assert(cortex_m::ipsr() == 0);
	assert(scheduler::is_os_callable());

	const auto previous = cortex_m::set_basepri(scheduler::service_call_priority);
	assert(owner_ == scheduler::current_task()); // std::mutex calls this UB

	scheduler::release_mutex(*this, *scheduler::current_task());
	scheduler::do_switch();                   // a woken waiter may outrank us

	cortex_m::set_basepri(previous);
}

// --- task_control_block -------------------------------------------------------

/**
 * @brief Configures storage + priority and starts the task with the given @p entry callback
 * @param stack_base Pointer to the base of the task's stack (provided by @c task<StackSize>)
 * @param stack_size Size of the stack, in @c stack_item increments
 * @param entry The @c callback the task will execute
 * @param name Optional name for the task
 * @return @c true if the task was successfully started, @c false if it was already active
 * @remark @c stack_base_, @c stack_size_ and @c priority_ are written here on
 *         first launch (see the BSS-placement note on the @c task_control_block
 *         default constructor). Sets up the initial stack so the task starts in
 *         @c task_starter, with @c scheduler::terminate_task wired as the link
 *         register so the task terminates cleanly when its entry callback
 *         returns. frame->lr points at the global label
 *         @c opsy_terminate_task_resume, exposed by the inline asm inside
 *         @c scheduler::terminate_task right before its @c SVC — no fragile
 *         "+ N" offset past the leading @c nop (which is kept solely so GDB
 *         renders the link return one instruction before the SVC).
 */
inline bool task_control_block::start_impl(stack_item* stack_base, std::size_t stack_size,
                                           callback<void(void)>&& entry, const char* name)
{
	assert(scheduler::is_os_callable()); // add_task mutates all_tasks_ and ready_

	if(active_.exchange(true)) // we put true in the boolean value, and were expecting false, so we return if exchange return true
		return false;

	stack_base_ = stack_base;
	stack_size_ = stack_size;
	base_priority_ = task_priority::lowest;
	priority_      = task_priority::lowest;   // no inheritance on a fresh task
	stop_requested_.store(false, std::memory_order_relaxed); // a reused slot starts clean

	entry_ = std::move(entry);
	name_ = name;

#ifndef NDEBUG
	std::fill(stack_base_, stack_base_ + stack_size_, dummy_pattern);
#endif

	stack_pointer_ = &stack_base_[stack_size_ - 1]; // this pointer is reserved to stop stack trace unwinding
	stack_base_[stack_size_ - 1] = 0; // keep this pointer to zero to stop stack trace

	// Round the top of the frame down to an 8-byte boundary before carving it
	// out. stack_base_ is 8-aligned (see stack_storage), but &stack_base_[N - 1]
	// is deliberately one word below the top, so the frame — 8 words, alignment
	// preserving — would otherwise start the task with SP at 4-mod-8. The word
	// reserved just above to stop stack-trace unwinding is left untouched.
	stack_pointer_ = reinterpret_cast<stack_item*>(
		reinterpret_cast<std::uintptr_t>(stack_pointer_) & ~std::uintptr_t{7});

	stack_pointer_ -= sizeof(stack_frame) / sizeof(stack_item);
	static_assert(sizeof(stack_frame) % 8 == 0, "the exception frame must preserve 8-byte stack alignment");
	assert(reinterpret_cast<std::uintptr_t>(stack_pointer_) % 8 == 0);
	const auto frame = reinterpret_cast<stack_frame*>(stack_pointer_);

	frame->psr = 1 << 24;
	frame->pc = reinterpret_cast<code_pointer>(task_starter);
	frame->lr = reinterpret_cast<code_pointer>(&opsy_terminate_task_resume); // address of the SVC instruction inside terminate_task — see the comment on terminate_task in scheduler.cpp for why this avoids a "+ 2" offset
	frame->r0 = reinterpret_cast<uint32_t>(this);

	stack_pointer_ -= sizeof(context) / sizeof(stack_item);
	const auto ctx = reinterpret_cast<context*>(stack_pointer_);
	ctx->control = cortex_m::control_thread_psp_privileged;
	ctx->lr = cortex_m::exc_return_thread_psp_basic;

	scheduler::add_task(*this);
	return true;
}

/**
 * @brief Terminates the task immediately
 * @return @c true if the task was running and has been signalled to terminate, @c false if it was already inactive
 * @remark Issues an @c SVC carrying @c service_call_number::terminate; the actual
 *         teardown happens in @c scheduler::service_call_handler.
 */
inline bool task_control_block::kill()
{
	assert(scheduler::is_os_callable()); // an ISR above OpSy must not enter the scheduler

	if(!is_started()) // can only terminate an active task
		return false;

	// "memory" clobber: see scheduler::trigger_hard_switch.
	asm volatile(
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (scheduler::service_call_number::terminate), [task] "r" (this)
			: "r0", "memory");
	return true;
}

/**
 * @brief Updates the task priority
 * @param new_priority The new priority value
 * @remark No-op if the priority is unchanged. Otherwise delegates to
 *         @c scheduler::update_priority which may re-order ready/waiting lists
 *         and request a context switch.
 */
inline void task_control_block::priority(task_priority new_priority)
{
	// Setting a priority on a task that has not been started is lost: start
	// puts every task at task_priority::lowest, so the value written here
	// would be overwritten by the launch and never take effect. Nothing else
	// would report it -- the task simply runs at the wrong priority.
	assert(is_started());

	// Changing a priority re-sorts ready_, re-sorts the waiting list of a
	// condition variable, or walks the chain of mutex holders paying priority
	// inheritance -- all of it under a BASEPRI that does not mask an interrupt
	// more urgent than OpSy. Called from such a handler, it would edit those
	// lists while the scheduler is halfway through editing them itself.
	assert(scheduler::is_os_callable());

	// Compared against the requested priority, not the effective one: while
	// this task is boosted by a waiter, priority_ is the inherited value, and
	// comparing to it would silently drop a genuine change.
	if(new_priority == base_priority_)
		return;

	scheduler::update_priority(*this, new_priority);
}

inline void task_control_block::set_name(const char* name)
{
	// Naming a task that has not been started is lost: start takes the name
	// as an argument and writes it, so anything set beforehand is overwritten
	// by the launch. Pass the name to start instead.
	assert(is_started());

	// name_ is a plain pointer, not an atomic, and the scheduler's hooks read
	// it to label what they trace. Writing it from an interrupt handler more
	// urgent than OpSy would be a data race with any such read -- undefined
	// behaviour, whatever a given core happens to do with an aligned word.
	assert(scheduler::is_os_callable());

	name_ = name;
}

inline void task_control_block::request_stop()
{
	// Requesting a stop on a task that has not been started is lost: start
	// clears the flag so that a reused task slot begins with none pending.
	// The task would then run to completion having never seen the request.
	assert(is_started());

	// No context assert here, deliberately: the flag is a relaxed atomic and
	// nothing else is touched, so this is safe to call from any interrupt
	// handler, including one more urgent than OpSy. Asking a task to stop is
	// exactly the kind of thing a handler has reason to do.
	stop_requested_.store(true, std::memory_order_relaxed);
}

/**
 * @brief Trampoline executed at the start of every task
 * @param thisPtr Pointer to the @c task_control_block being started
 * @remark Calls the user-provided entry callback, then terminates the task
 *         via @c scheduler::terminate_task. The link register set up in
 *         @c start would also lead here, this trampoline is for the normal
 *         (non-fault) entry path.
 */
inline void task_control_block::task_starter(task_control_block* thisPtr)
{
	thisPtr->entry_();
	scheduler::terminate_task(thisPtr);
}

}
