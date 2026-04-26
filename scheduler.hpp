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
#include "condition_variable.hpp"
#include "hooks.hpp"

extern "C" void SysTick_Handler();
extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();

namespace opsy
{

/**
 * @brief The @c scheduler is the OpSy manager, it handles @c task switch, timeouts, etc.
 * @remark You can start @c task before the @c scheduler is started, but they will not be executed until the @c scheduler is started
 */
class scheduler
{
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
		assert(is_started_);
		return all_tasks_;
	}

	/**
	 * @brief Gets the current @c time_point
	 * @return The current @c time_point
	 */
	static inline time_point now()
	{
		assert(is_started_ && cortex_m::current_priority().value_or(cortex_m::lowest_priority).value() >= systick_priority.value());
		return ticks_;
	}

	/**
	 * @brief Try to get a valid @c critical_section from the @c scheduler
	 * @return A @c critical_section with state @c true if possible, @c false otherwise (already in critical section)
	 * @remark Use this only for @c task to @c task synchronization, prefer @c mutex for a more generic synchronization (uses @c isr_priority to sychronize with interrupt service routines)
	 */
	[[nodiscard]] static inline opsy::critical_section try_critical_section()
	{
		if (critical_section_) // was already in critical section, iterative is OK but the new object is invalid, meaning the critical section is ended only when the first (the only valid) object is released
			return opsy::critical_section(false);
		else
		{
			hooks::enter_critical_section();
			critical_section_ = true;
			return opsy::critical_section(true);
		}
	}

private:

	enum class service_call_number
		: uint8_t
		{
			terminate, sleep, context_switch, wait,
	};

	static bool is_started_;
	static time_point ticks_;
	static embedded_list<task_control_block, task_lists::handle> all_tasks_;
	static embedded_list<task_control_block, task_lists::timeout> timeouts_;
	static embedded_list<task_control_block, task_lists::waiting> ready_;
	static bool idling_;
	static bool may_need_switch_;
	static volatile bool critical_section_;

	static idle_task_control_block* idle_;
	static task_control_block* previous_task_;
	static task_control_block* current_task_;
	static task_control_block* next_task_;

	static void add_task(task_control_block& task)
	{
		hooks::task_added(task);
		all_tasks_.push_front(task);
		ready_.insert_when(task_control_block::priority_is_lower, task);
		if(is_started_)
			trigger_soft_switch();
	}

	static void terminate_task(task_control_block* task);

	static void trigger_soft_switch()
	{
		auto previous = cortex_m::set_basepri(service_call_priority);
		may_need_switch_ = false;
		assert(previous.value() == 0); // there is no reason to call this being in a mutex
		do_switch(); // do the actual switch
		cortex_m::set_basepri(previous); // and restore the basepri to its previous value
	}

	static __attribute__((always_inline)) void trigger_hard_switch()
	{
		asm volatile("svc %[immediate]" : : [immediate] "I" (service_call_number::context_switch));
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
		ticks_+=duration(1); // this is correct and "atomic" because nothing that has preemptive level above system should use it

		bool dirty = false;


		while(!timeouts_.empty() && timeouts_.front().wait_until_.value() <= ticks_)
		{
			auto& task = timeouts_.front();
			timeouts_.pop_front();
			task.wait_until_ = std::nullopt;

			if(task.waiting_ != nullptr)
			{
				task.waiting_->remove_waiting(task);
				task.set_return_value(static_cast<uint32_t>(cv_status::timeout)); // notify timeout to thread (write value to its R0 frame)
			}

			ready_.insert_when(task_control_block::priority_is_lower, task);
			hooks::task_ready(task);
			dirty = true;
		}

		if(dirty)
			hooks::exit_systick(do_switch());
		else
			hooks::exit_systick(false);
	}

	static uint64_t pend_sv_handler(uint32_t* psp);
	static void service_call_handler(stack_frame* frame, service_call_number parameter, bool is_thread, uint32_t exc_return);
	static void wake_up(task_control_block& task, condition_variable& initiator);
	static void update_priority(task_control_block& task, task_priority new_priority);

	static void update_name(task_control_block& task)
	{
		hooks::task_name_changed(task);
	}

	static void critical_section_end()
	{
		assert(critical_section_ == true); // should be in critical section
		critical_section_ = false;
		hooks::exit_critical_section();
		if (may_need_switch_)
			trigger_soft_switch();
	}
};

}

// Inline definitions for critical_section / priority_mutex / condition_variable.
// Pulled here, AFTER the full @c scheduler declaration is in scope, to break
// the include cycle between scheduler.hpp and these three primitives' headers.
// See scheduler_inl.hpp for details.
#include "scheduler_inl.hpp"

