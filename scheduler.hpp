/**
 ******************************************************************************
 * @file    scheduler.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   The OpSy scheduler.
 *
 * 			The scheduler is responsible for task switching, timeouts, and
 * 			globally makes the link between all OpSy elements.
 *
 * 			@c task will not run until the @c scheduler is started.
 * 			When the @c scheduler starts, it does NOT return from @c start.
 * 			Instead it starts running the most important @c task or goes to
 * 			idle if there is no @c task ready to run.
 *
 * 			It also offers the @c now method, which returns the @c time_point
 * 			since the @c scheduler started.
 *
 * 			You can iterate over all the @c task currently handled by the
 * 			@c scheduler with @c all_tasks
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

/**
 * @brief Token guarding @c scheduler_inl.hpp inclusion
 * @remark @c scheduler_inl.hpp checks this macro to refuse direct inclusion
 *         from anywhere other than the bottom of @c scheduler.hpp.
 */
#define OPSY_SCHEDULER_HPP_INCLUDED_

#include <cstdint>
#include <ratio>
#include "opsy_assert.hpp"
#include <atomic>

#include "config.hpp"
#include "task.hpp"
#include "mutex.hpp"
#include "condition_variable.hpp"
#include "hooks.hpp"

extern "C" void SysTick_Handler();
extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();

// Defined inline by the asm body of opsy::scheduler::terminate_task in
// scheduler.cpp. Points exactly at the SVC instruction that finalizes a
// task, so frame->lr can target it directly via &opsy_terminate_task_resume
// without any "+ N bytes past the nop" arithmetic. We never call this
// function — only take its address — so its signature here is purely a
// linker-visible declaration; the [[noreturn]] attribute is documentary.
extern "C" [[noreturn]] void opsy_terminate_task_resume();

namespace opsy
{

/**
 * @brief The @c scheduler is the OpSy manager, it handles @c task switch, timeouts, etc.
 * @remark You can start @c task before the @c scheduler is started, but they will not be executed until the @c scheduler is started
 */
class scheduler
{
	friend class mutex;

	friend void ::SysTick_Handler();
	friend void ::PendSV_Handler();
	friend void ::SVC_Handler();
	friend void sleep_for(duration t);
	friend class task_control_block;
	friend class critical_section;
	friend class condition_variable;

public:

	/**
	 * @brief The Service Call @c isr_priority, it is set to system preemption and most important sub-priority
	 */
	static constexpr auto service_call_priority = isr_priority::from_preempt_sub<preemption_bits>(opsy_preemption, 0);

	/**
	 * @brief The Systick @c isr_priority, it is set to system preemption and least important sub-priority
	 * @remark Any interrupt service routine with priority above this one, or @c mutex locks that locks higher priority, will NOT be able to use any of OpSy features
	 */
	static constexpr auto systick_priority = isr_priority::from_preempt_sub<preemption_bits>(opsy_preemption, cortex_m::min_sub());

	/**
	 * @brief The PendSV @c isr_priority, is it set to the minimum preemption and sub-priority possible
	 * @remark But as soon as it starts, it will lock anything up to the Service Call
	 */
	static constexpr auto pend_sv_priority = isr_priority::from_preempt_sub<preemption_bits>(cortex_m::min_preempt(), cortex_m::min_sub());

	/**
	 * @brief Starts the @c scheduler
	 * @tparam IdleStackSize Stack size of the @c idle_task in @c stack_item increments
	 * @param idle The @c idle_task to use when the system goes idle. Default is @c default_idle<IdleStackSize>
	 * @param entry The function the idle task should run. Default is @c default_idle_loop (a @c WFI / @c NOP spin)
	 * @remark Templated wrapper that calls @c idle.prepare(entry) and dispatches to @c start_impl.
	 *         Going through @c prepare lets @c idle_task<N> stay zero-initialized (and therefore in @c .bss)
	 *         until the scheduler actually launches.
	 *         Never returns: takes control of the CPU and runs the highest-priority @c task or the idle loop.
	 *         Trying to start the scheduler twice trips an assert.
	 */
	template<std::size_t IdleStackSize = 64>
	[[noreturn]] static void start(idle_task<IdleStackSize>& idle = default_idle<IdleStackSize>,
	                               code_pointer entry = default_idle_loop)
	{
		idle.prepare(entry);
		start_impl(idle);
	}

private:

	/**
	 * @brief The non-template body of @c start
	 * @remark Defined in @c scheduler.cpp; the template wrapper above is the
	 *         single public entry point so users keep calling
	 *         @c scheduler::start as before.
	 */
	[[noreturn]] static void start_impl(idle_task_control_block& idle);

public:

	/**
	 * @brief Gets a read only reference to the @c embedded_list of @c task currently active
	 * @return A read only reference to the @c embedded_list of @c task currently active
	 */
	static const embedded_list<task_control_block, task_lists::handle>& all_tasks()
	{
		assert(is_started_.load(std::memory_order_relaxed));
		assert(is_os_callable()); // the list can be mid-update under an ISR above OpSy
		return all_tasks_;
	}

	/**
	 * @brief Gets the current @c time_point
	 * @return The current @c time_point
	 */
	static inline time_point now()
	{
		assert(is_started_.load(std::memory_order_relaxed)
			&& cortex_m::current_priority().value_or(cortex_m::lowest_priority).value() >= systick_priority.value());

		// ticks_ is 64 bits, which is two ldr on Cortex-M, and SysTick — the only
		// writer — can land between them: when the low word wraps (every ~49.7
		// days at 1 ms) the two halves come from different values and the result
		// is torn. std::atomic<time_point> is not an option: atomic<uint64_t> is
		// not lock-free on any target in the matrix (checked on m3/m4/m7/m33), so
		// it would call into libatomic from an ISR.
		//
		// Masking SysTick for the duration of the read is the whole fix, and it
		// can only ever raise the mask: the assert above already requires the
		// caller to be running no higher than systick_priority. set_basepri
		// carries a "memory" clobber, which also stops the compiler from caching
		// ticks_ in a register across the read — the reason a spin such as
		// while (now() < deadline) {} could never terminate at -O2.
		const auto previous = cortex_m::set_basepri(systick_priority);
		const auto value = ticks_;
		cortex_m::set_basepri(previous);
		return value;
	}

	/**
	 * @brief The task currently running, or @c nullptr between tasks
	 * @remark Exposed for @c mutex , which needs to know who is acquiring.
	 */
	[[nodiscard]] static inline task_control_block* current_task()
	{
		return current_task_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief Whether the caller is running at a level where OpSy may be entered
	 *
	 *        OpSy deliberately leaves the priority levels above its own free
	 *        for latency-critical interrupt handlers — they preempt the
	 *        scheduler itself, which is the point. The price is that such a
	 *        handler can interrupt OpSy at an arbitrary instruction, with its
	 *        lists half-stitched and its globals mid-update, so it must not
	 *        call into OpSy at all.
	 *
	 *        The service calls are protected by the hardware: an @c SVC issued
	 *        from a handler that outranks @c service_call_priority escalates
	 *        straight to HardFault. Everything that does not go through an
	 *        @c SVC — this function, @ref now , the accessors — has no such
	 *        protection, and asserts on this instead.
	 *
	 * @return @c true from a task, or from a handler at or below
	 *         @c service_call_priority
	 *
	 * @remark Thread mode reports no priority at all, which is the lowest
	 *         there is, so a task always passes.
	 */
	[[nodiscard]] static inline bool is_os_callable()
	{
		return cortex_m::current_priority()
			.value_or(cortex_m::lowest_priority)
			.masked_value<preemption_bits>()
			>= service_call_priority.masked_value<preemption_bits>();
	}

	/**
	 * @brief Try to get a valid @c critical_section from the @c scheduler
	 * @return A @c critical_section with state @c true if possible, @c false otherwise (already in critical section)
	 * @remark Use this only for @c task to @c task synchronization, prefer @c mutex for a more generic synchronization (uses @c isr_priority to sychronize with interrupt service routines)
	 */
	[[nodiscard]] static inline opsy::critical_section try_critical_section()
	{
		assert(is_os_callable()); // an ISR above OpSy must not touch the scheduler

		// A single read-modify-write, not a load followed by a store: SysTick can
		// land between the two and switch to a task that takes the section for
		// itself, after which both tasks believe they hold it and the first
		// release clears the flag for both.
		if (critical_section_.exchange(true, std::memory_order_relaxed)) // was already in critical section, iterative is OK but the new object is invalid, meaning the critical section is ended only when the first (the only valid) object is released
			return opsy::critical_section(false);

		hooks::enter_critical_section();
		return opsy::critical_section(true);
	}

private:

	enum class service_call_number
		: uint8_t
		{
			terminate, sleep, context_switch, wait, mutex_lock, mutex_unlock,
	};

	// All flags / pointers below are read or written from both task and ISR
	// context (PendSV, SysTick, SVC). We keep them as std::atomic<...> with
	// memory_order_relaxed, matching the rest of OpSy (see task::active_):
	// single-core Cortex-M makes the underlying word load/store atomic for
	// free, the only thing relaxed atomics buy us — and the only thing we
	// need — is preventing the compiler from caching them in registers across
	// SVC / PendSV / function calls. Stronger orders would just add DMBs that
	// are unnecessary on a uniprocessor.
	static std::atomic<bool> is_started_;
	static time_point ticks_;
	static embedded_list<task_control_block, task_lists::handle> all_tasks_;
	static embedded_list<task_control_block, task_lists::timeout> timeouts_;
	static embedded_list<task_control_block, task_lists::waiting> ready_;

	// Every mutex currently held, by anyone. Mutexes have no global
	// enumeration of their own the way tasks have all_tasks_, and both
	// releasing a dead task's mutexes and recomputing an inherited priority
	// need to walk what a task holds. Membership here is exactly
	// `owner_ != nullptr`.
	static embedded_list<mutex, mutex> locked_mutexes_;
	static std::atomic<bool> idling_;
	static std::atomic<bool> may_need_switch_;
	static std::atomic<bool> critical_section_;

	static std::atomic<idle_task_control_block*> idle_;
	static std::atomic<task_control_block*> previous_task_;
	static std::atomic<task_control_block*> current_task_;
	static std::atomic<task_control_block*> next_task_;

	static void add_task(task_control_block& task)
	{
		hooks::task_added(task);

		{
			// Called straight from task context by task_control_block::start_impl,
			// so SysTick can land in the middle of these two list mutations and run
			// its own ready_.insert_when on a half-stitched list -- losing a task or
			// closing the links into a cycle. The mask is released before
			// trigger_soft_switch, which asserts BASEPRI is clear (and is therefore
			// why the caller cannot simply hold the lock across the whole thing).
			const auto previous = cortex_m::set_basepri(service_call_priority);
			all_tasks_.push_front(task);
			ready_.insert_when(task_control_block::priority_is_lower, task);
			cortex_m::set_basepri(previous);
		}

		if(is_started_.load(std::memory_order_relaxed))
			trigger_soft_switch();
	}

	static void terminate_task(task_control_block* task);

	static void trigger_soft_switch()
	{
		auto previous = cortex_m::set_basepri(service_call_priority);
		may_need_switch_.store(false, std::memory_order_relaxed);
		assert(previous.value() == 0); // there is no reason to call this being in a mutex
		do_switch(); // do the actual switch
		cortex_m::set_basepri(previous); // and restore the basepri to its previous value
	}

	static __attribute__((always_inline)) void trigger_hard_switch()
	{
		// "memory" clobber: the SVC enters the scheduler which may modify the
		// task's frame on the PSP and any global it touches (current_task_,
		// next_task_, ready_, ...). Without this clobber the compiler would be
		// free to keep stale globals/locals in registers across the call —
		// invisible at -Og but a real bug at -O2/-O3/LTO.
		asm volatile("svc %[immediate]" : : [immediate] "I" (service_call_number::context_switch) : "memory");
	}

	static constexpr bool wakeup_after(const task_control_block& left, const task_control_block& right)
	{
		assert(left.wait_until_.has_value() && right.wait_until_.has_value());
		return left.wait_until_.value_or(startup) < right.wait_until_.value_or(startup);
	}

	static bool do_switch();

	static void __attribute__((always_inline)) systick_handler()
	{
		hooks::enter_systick();
		ticks_+=duration(1); // the only writer, and nothing above system preemption level runs while it does; readers below that level go through now(), which masks

		bool dirty = false;


		// Tasks on the timeouts_ list always have wait_until_ engaged (it is set
		// before insertion and cleared after pop). Use * not .value() to avoid
		// the bad_optional_access -> abort path under -fno-exceptions.
		while(!timeouts_.empty() && *timeouts_.front().wait_until_ <= ticks_)
		{
			auto& task = timeouts_.front();
			timeouts_.pop_front();
			task.wait_until_ = std::nullopt;

			if(task.waiting_ != nullptr)
			{
				task.waiting_->remove_waiting(task);
				task.waiting_ = nullptr; // the task no longer waits on it -- see wake_up, which does the same
				task.set_return_value(static_cast<uint32_t>(cv_status::timeout)); // notify timeout to thread (write value to its R0 frame)
			}

			// Same path as a notification: a task that waited holding a mutex
			// must own it again before it can run, and may have to block on
			// it instead of becoming runnable.
			resume_waiter(task);
			dirty = true;
		}

		if(dirty)
			hooks::exit_systick(do_switch());
		else
			hooks::exit_systick(false);
	}

	static uint64_t pend_sv_handler(uint32_t* psp);
	static void service_call_handler(stack_frame* frame, service_call_number parameter, bool is_thread, uint32_t exc_return);
	/**
	 * @brief Records @p task as the owner of @p m
	 * @remark The two sides of taking a mutex, in one place so they cannot
	 *         drift: the owner pointer, and membership of locked_mutexes_.
	 *         Must be called with the critical section held.
	 */
	static void take_mutex(mutex& m, task_control_block& task);

	/**
	 * @brief Releases @p m , handing it straight to its highest-priority waiter
	 * @remark Shared by the unlock service call and by a condition variable
	 *         wait, which has to release the mutex it was given so another
	 *         task can take it while this one sleeps. The waiter is made the
	 *         owner here rather than left to race for it, so a task woken from
	 *         a mutex owns it the moment it runs.
	 */
	static void release_mutex(mutex& m, task_control_block& owner);

	/**
	 * @brief Puts a task woken from a condition variable back where it belongs,
	 *        re-acquiring the mutex it waited with, or blocking on it
	 */
	static void resume_waiter(task_control_block& task);

	static void wake_up(task_control_block& task, condition_variable& initiator);

	/**
	 * @brief Highest-priority task currently blocked on @p m , or @c nullptr
	 * @remark Derived by walking all_tasks_ rather than kept in a per-mutex
	 *         list, so the relation lives in exactly one place
	 *         (@c task::blocked_on_ ). all_tasks_ is a handful of entries on
	 *         any real system, and this only runs on unlock with contention.
	 */
	static task_control_block* highest_waiter(const mutex& m);

	/**
	 * @brief Recomputes @p task 's effective priority from its base one and
	 *        every task waiting on a mutex it holds
	 * @return @c true if the effective priority changed
	 * @remark This is the other half of priority inheritance: raising happens
	 *         when a waiter arrives, this restores the right level when a
	 *         mutex is released — which may still be an inherited one, if the
	 *         task holds another contended mutex.
	 */
	static bool recompute_priority(task_control_block& task);

	/**
	 * @brief Raises @p owner to at least @p priority, following the chain of
	 *        blocked_on_ so a transitive holder is raised too
	 */
	static void inherit_priority(task_control_block& owner, task_priority priority);

	/** @brief Releases every mutex held by @p task, waking the waiters */
	static void release_all_mutexes(task_control_block& task);
	static void update_priority(task_control_block& task, task_priority new_priority);

	static void update_name(task_control_block& task)
	{
		hooks::task_name_changed(task);
	}

	static void critical_section_end()
	{
		assert(critical_section_.load(std::memory_order_relaxed) == true); // should be in critical section
		critical_section_.store(false, std::memory_order_relaxed);
		hooks::exit_critical_section();
		if (may_need_switch_.load(std::memory_order_relaxed))
			trigger_soft_switch();
	}
};

}

// Inline definitions for critical_section / isr_lock / condition_variable.
// Pulled here, AFTER the full @c scheduler declaration is in scope, to break
// the include cycle between scheduler.hpp and these three primitives' headers.
// See scheduler_inl.hpp for details.
#include "scheduler_inl.hpp"

