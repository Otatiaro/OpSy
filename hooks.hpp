/**
 ******************************************************************************
 * @file    hooks.hpp
 * @author  Thomas Legrand
 * @version V0.2
 * @date    01-March-2019
 * @brief   The points OpSy reports, and how a program listens to them
 *
 * 			OpSy reports what it does -- a task made ready, a handler entered,
 * 			a mutex handed over -- through the static functions of @c opsy::hook.
 * 			Each of them forwards to the function of the same name in
 * 			@c opsy::hooks when the program defines one, and is empty otherwise:
 * 			a hook nobody listens to costs nothing from -Og up, its arguments
 * 			included.
 *
 * 			A program listens by providing a file @c opsy_hooks.hpp on its
 * 			include path, defining @c opsy::hooks with the hooks it wants and
 * 			no other. @c hook below is the list of what it may define, with the
 * 			signature of each; a hook may also take the shorter form noted on
 * 			it. A name that matches nothing in the list is never called -- the
 * 			compiler has no way to tell a hook from a helper -- so a hook that
 * 			stays silent is checked against this list first.
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

#include <cstdint>

#include "callback.hpp"
#include "config.hpp"
#include "cortex_m.hpp"
#include "isr_priority.hpp"

namespace opsy
{

// Declared, not included: these appear in signatures only, and their headers
// include this one back. Every type the signatures below name has to be
// listed -- an omission stops this header compiling on its own, and with it
// anything standalone that includes it, while most callers go on building
// because they included <opsy.hpp> first. An <opsy_hooks.hpp> sees these
// declarations too, and needs none of its own.
class task_control_block;
class idle_task_control_block;
class condition_variable;
class mutex;
class isr_lock;
struct exception_frame;
enum class task_priority : uint8_t;

/**
 * @brief What made a task ready: the one fact about a wake-up that cannot be
 *        read back from the task afterwards
 */
enum class wake_reason : uint8_t
{
	notified,      ///< a @c condition_variable it waited on was notified
	timed_out,     ///< its wait on a @c condition_variable reached its deadline
	sleep_elapsed, ///< its sleep is over
	mutex_granted, ///< the @c mutex it was blocked on was handed to it
};

}

#if __has_include(<opsy_hooks.hpp>)
#include <opsy_hooks.hpp> // IWYU pragma: export
#else

namespace opsy
{
	/**
	 * @brief The hooks of a program that provides no @c <opsy_hooks.hpp>: none
	 */
	class hooks
	{};
}

#endif

namespace opsy
{
	/**
	 * @brief Every point OpSy reports, each forwarded to @c opsy::hooks when
	 *        the program defines a function of that name there
	 *
	 *        What OpSy calls. A program never calls these, and never defines
	 *        them: it defines the ones it listens to in @c opsy::hooks, with
	 *        the signature given here or the shorter one a hook notes, and
	 *        @c hook finds them. The hooks run where OpSy calls them: inside
	 *        its handlers and under its locks, at every level from a task to
	 *        SysTick. One must not block, and must not call into OpSy.
	 */
	struct hook
	{
		/**
		 * @brief The scheduler is starting
		 * @param idle The idle task
		 * @param core_clock_value The core clock, in hertz
		 * @param foreach_task Calls its argument once per task
		 * @remark Runs before the scheduler counts as started, with interrupts
		 *         enabled and SysTick already ticking: @c scheduler::now and
		 *         @c scheduler::all_tasks assert here. @p foreach_task is the
		 *         way to see the tasks.
		 */
		template<class Hooks = hooks>
		static void starting(idle_task_control_block& idle, uint32_t core_clock_value, callback<void(callback<void(const task_control_block&)>)> foreach_task)
		{
			if constexpr(requires { Hooks::starting(idle, core_clock_value, foreach_task); })
				Hooks::starting(idle, core_clock_value, foreach_task);
		}

		/**
		 * @brief The idle task has nothing to run: wait for an interrupt
		 * @remark The default is a @c WFI, or a @c NOP in a build without
		 *         @c NDEBUG so that a debugger steps through it. A tracer that
		 *         reads memory through the debug port while the core sleeps
		 *         gets zeros on some parts, and defines this as a @c NOP .
		 */
		template<class Hooks = hooks>
		static void idle_wait()
		{
			if constexpr(requires { Hooks::idle_wait(); })
			{
				Hooks::idle_wait();
			}
			else
			{
#ifndef NDEBUG
				cortex_m::nop();
#else
				cortex_m::wfi();
#endif
			}
		}

		/** @brief PendSV is entered, for a context switch */
		template<class Hooks = hooks>
		static constexpr void enter_pend_sv()
		{
			if constexpr(requires { Hooks::enter_pend_sv(); })
				Hooks::enter_pend_sv();
		}

		/** @brief The switch goes to the idle task: no task is ready */
		template<class Hooks = hooks>
		static constexpr void enter_idle()
		{
			if constexpr(requires { Hooks::enter_idle(); })
				Hooks::enter_idle();
		}

		/** @brief The SysTick handler is entered */
		template<class Hooks = hooks>
		static constexpr void enter_systick()
		{
			if constexpr(requires { Hooks::enter_systick(); })
				Hooks::enter_systick();
		}

		/**
		 * @brief The SysTick handler is about to return
		 * @param task_switch @c true if it asked for a context switch
		 */
		template<class Hooks = hooks>
		static constexpr void exit_systick(bool task_switch)
		{
			if constexpr(requires { Hooks::exit_systick(task_switch); })
				Hooks::exit_systick(task_switch);
		}

		/** @brief The service call handler is entered */
		template<class Hooks = hooks>
		static constexpr void enter_service_call()
		{
			if constexpr(requires { Hooks::enter_service_call(); })
				Hooks::enter_service_call();
		}

		/**
		 * @brief The service call handler is about to return
		 * @param task_switch @c true if it asked for a context switch
		 */
		template<class Hooks = hooks>
		static constexpr void exit_service_call(bool task_switch)
		{
			if constexpr(requires { Hooks::exit_service_call(task_switch); })
				Hooks::exit_service_call(task_switch);
		}

		/**
		 * @brief A task joins the list of active tasks
		 * @param task The task
		 */
		template<class Hooks = hooks>
		static constexpr void task_added(task_control_block& task)
		{
			if constexpr(requires { Hooks::task_added(task); })
				Hooks::task_added(task);
		}

		/**
		 * @brief A task leaves the list of active tasks
		 * @param task The task
		 */
		template<class Hooks = hooks>
		static constexpr void task_terminated(task_control_block& task)
		{
			if constexpr(requires { Hooks::task_terminated(task); })
				Hooks::task_terminated(task);
		}

		/**
		 * @brief A task is chosen to run: its context is about to be restored
		 * @param task The task
		 */
		template<class Hooks = hooks>
		static constexpr void task_started(task_control_block& task)
		{
			if constexpr(requires { Hooks::task_started(task); })
				Hooks::task_started(task);
		}

		/**
		 * @brief A task goes to sleep
		 * @param task The task
		 * @param until When it is due to wake
		 * @remark Also accepted: @c task_sleep(task) .
		 * @return What the hook returns, which is nothing; it tells a test
		 *         which form was taken.
		 */
		template<class Hooks = hooks>
		static constexpr auto task_sleep(task_control_block& task, time_point until)
		{
			if constexpr(requires { Hooks::task_sleep(task, until); })
				return Hooks::task_sleep(task, until);
			else if constexpr(requires { Hooks::task_sleep(task); })
				return Hooks::task_sleep(task);
		}

		/**
		 * @brief A task stops running: its context is about to be saved
		 * @param task The task
		 */
		template<class Hooks = hooks>
		static constexpr void task_stopped(task_control_block& task)
		{
			if constexpr(requires { Hooks::task_stopped(task); })
				Hooks::task_stopped(task);
		}

		/**
		 * @brief A task starts waiting on a @c condition_variable
		 * @param task The task
		 * @param condition The condition variable
		 */
		template<class Hooks = hooks>
		static constexpr void task_wait(task_control_block& task, condition_variable& condition)
		{
			if constexpr(requires { Hooks::task_wait(task, condition); })
				Hooks::task_wait(task, condition);
		}

		/**
		 * @brief A task starts waiting on a @c condition_variable with a deadline
		 * @param task The task
		 * @param condition The condition variable
		 * @param until When the wait times out
		 */
		template<class Hooks = hooks>
		static constexpr void task_wait_timeout(task_control_block& task, condition_variable& condition, time_point until)
		{
			if constexpr(requires { Hooks::task_wait_timeout(task, condition, until); })
				Hooks::task_wait_timeout(task, condition, until);
		}

		/**
		 * @brief A task becomes ready to run
		 * @param task The task
		 * @param reason What woke it
		 * @remark Also accepted: @c task_ready(task) . A task that is started
		 *         is reported by @ref task_added, and one whose priority
		 *         changes while ready by @ref task_priority_changed: neither
		 *         is a wake-up.
		 * @return What the hook returns, which is nothing; it tells a test
		 *         which form was taken.
		 */
		template<class Hooks = hooks>
		static constexpr auto task_ready(task_control_block& task, wake_reason reason)
		{
			if constexpr(requires { Hooks::task_ready(task, reason); })
				return Hooks::task_ready(task, reason);
			else if constexpr(requires { Hooks::task_ready(task); })
				return Hooks::task_ready(task);
		}

		/**
		 * @brief A task's effective priority changed: the one it is scheduled
		 *        by, raised by a waiter's donation, lowered when the donation
		 *        ends, or set by @c task::priority
		 * @param task The task
		 * @param priority Its effective priority from now on, which
		 *        @c task::priority does not report: that one is the requested
		 *        priority
		 * @remark Also accepted: @c task_priority_changed(task) . Called only
		 *         when the effective priority moves: a requested priority that
		 *         a donation still outranks changes nothing the scheduler sees.
		 * @return What the hook returns, which is nothing; it tells a test
		 *         which form was taken.
		 */
		template<class Hooks = hooks>
		static constexpr auto task_priority_changed(task_control_block& task, task_priority priority)
		{
			if constexpr(requires { Hooks::task_priority_changed(task, priority); })
				return Hooks::task_priority_changed(task, priority);
			else if constexpr(requires { Hooks::task_priority_changed(task); })
				return Hooks::task_priority_changed(task);
		}

		/** @brief A task enters the scheduler's critical section: no task switch until it leaves */
		template<class Hooks = hooks>
		static constexpr void enter_critical_section()
		{
			if constexpr(requires { Hooks::enter_critical_section(); })
				Hooks::enter_critical_section();
		}

		/** @brief A task leaves the scheduler's critical section */
		template<class Hooks = hooks>
		static constexpr void exit_critical_section()
		{
			if constexpr(requires { Hooks::exit_critical_section(); })
				Hooks::exit_critical_section();
		}

		/**
		 * @brief A task takes a @c mutex
		 * @param taken The mutex now held
		 * @param owner The task that holds it
		 */
		template<class Hooks = hooks>
		static constexpr void mutex_taken(mutex& taken, task_control_block& owner)
		{
			if constexpr(requires { Hooks::mutex_taken(taken, owner); })
				Hooks::mutex_taken(taken, owner);
		}

		/**
		 * @brief A task releases a @c mutex
		 * @param released The mutex just freed
		 * @param owner The task that held it
		 */
		template<class Hooks = hooks>
		static constexpr void mutex_released(mutex& released, task_control_block& owner)
		{
			if constexpr(requires { Hooks::mutex_released(released, owner); })
				Hooks::mutex_released(released, owner);
		}

		/**
		 * @brief A task blocks on a @c mutex another task holds
		 * @param blocking The mutex it waits for
		 * @param waiter The task now suspended
		 */
		template<class Hooks = hooks>
		static constexpr void mutex_blocked(mutex& blocking, task_control_block& waiter)
		{
			if constexpr(requires { Hooks::mutex_blocked(blocking, waiter); })
				Hooks::mutex_blocked(blocking, waiter);
		}

		/**
		 * @brief A wait releases the lock a task was holding
		 * @param task The task whose lock was released on its behalf
		 *
		 * @warning Pairs with @ref mutex_restored_for_task only for an
		 *          @c isr_lock . An @c opsy::mutex is re-acquired on wake
		 *          rather than during the switch, so its counterpart is
		 *          @ref mutex_taken from the wake path. A tracer pairing
		 *          stored/restored one-to-one will drift by one per mutex
		 *          wait.
		 */
		template<class Hooks = hooks>
		static constexpr void mutex_stored_for_task(task_control_block& task)
		{
			if constexpr(requires { Hooks::mutex_stored_for_task(task); })
				Hooks::mutex_stored_for_task(task);
		}

		/**
		 * @brief The @c isr_lock a task waited with is taken back for it
		 * @param task The task about to run with it
		 */
		template<class Hooks = hooks>
		static constexpr void mutex_restored_for_task(task_control_block& task)
		{
			if constexpr(requires { Hooks::mutex_restored_for_task(task); })
				Hooks::mutex_restored_for_task(task);
		}

		/** @brief A task or a handler masks every interrupt (@c PRIMASK = 1) */
		template<class Hooks = hooks>
		static constexpr void enter_full_lock()
		{
			if constexpr(requires { Hooks::enter_full_lock(); })
				Hooks::enter_full_lock();
		}

		/** @brief A task or a handler unmasks the interrupts a full lock held */
		template<class Hooks = hooks>
		static constexpr void exit_full_lock()
		{
			if constexpr(requires { Hooks::exit_full_lock(); })
				Hooks::exit_full_lock();
		}

		/**
		 * @brief A task or a handler masks the interrupts at and below a priority (@c BASEPRI )
		 * @param priority The priority masked from
		 */
		template<class Hooks = hooks>
		static constexpr void enter_priority_lock(isr_priority priority)
		{
			if constexpr(requires { Hooks::enter_priority_lock(priority); })
				Hooks::enter_priority_lock(priority);
		}

		/** @brief A task or a handler lifts a priority lock */
		template<class Hooks = hooks>
		static constexpr void exit_priority_lock()
		{
			if constexpr(requires { Hooks::exit_priority_lock(); })
				Hooks::exit_priority_lock();
		}

		/**
		 * @brief A @c condition_variable gains a waiter
		 * @param condition The condition variable
		 * @param task The task that starts waiting
		 */
		template<class Hooks = hooks>
		static constexpr void condition_variable_start_waiting(condition_variable& condition, task_control_block& task)
		{
			if constexpr(requires { Hooks::condition_variable_start_waiting(condition, task); })
				Hooks::condition_variable_start_waiting(condition, task);
		}

		/**
		 * @brief A @c condition_variable gains a waiter with a timeout
		 * @param condition The condition variable
		 * @param task The task that starts waiting
		 * @param timeout How long it waits at most
		 */
		template<class Hooks = hooks>
		static constexpr void condition_variable_start_waiting(condition_variable& condition, task_control_block& task, duration timeout)
		{
			if constexpr(requires { Hooks::condition_variable_start_waiting(condition, task, timeout); })
				Hooks::condition_variable_start_waiting(condition, task, timeout);
		}

		/**
		 * @brief A @c condition_variable is notified once
		 * @param condition The condition variable
		 */
		template<class Hooks = hooks>
		static constexpr void condition_variable_notify_one(condition_variable& condition)
		{
			if constexpr(requires { Hooks::condition_variable_notify_one(condition); })
				Hooks::condition_variable_notify_one(condition);
		}

		/**
		 * @brief A @c condition_variable is notified for all its waiters
		 * @param condition The condition variable
		 */
		template<class Hooks = hooks>
		static constexpr void condition_variable_notify_all(condition_variable& condition)
		{
			if constexpr(requires { Hooks::condition_variable_notify_all(condition); })
				Hooks::condition_variable_notify_all(condition);
		}

		/**
		 * @brief A fault exception is entered, before the application's handler
		 * @param status The fault status registers, read on entry
		 * @param frame The stacked registers of the code that faulted
		 * @param exc_return @c EXC_RETURN as the core set it
		 * @remark Called by @ref fault_isrs only, ahead of the handler given to
		 *         it: that handler may reset or halt, and never come back.
		 *         Runs in the fault handler, at its priority, on whatever
		 *         state the fault left.
		 */
		template<class Hooks = hooks>
		static constexpr void fault(const fault_status& status, const exception_frame& frame, uint32_t exc_return)
		{
			if constexpr(requires { Hooks::fault(status, frame, exc_return); })
				Hooks::fault(status, frame, exc_return);
		}

		/**
		 * @brief The handler a vector table holds for a device interrupt, in place of @p Handler
		 * @tparam Handler The handler the program wrote
		 * @return What @c Hooks::decorate_isr<Handler>() returns -- the same
		 *         handler wrapped in whatever the hooks want to see around it,
		 *         as a plain function pointer -- or @p Handler itself
		 */
		template<void (*Handler)(), class Hooks = hooks>
		static constexpr auto decorate_isr()
		{
			if constexpr(requires { Hooks::template decorate_isr<Handler>(); })
				return Hooks::template decorate_isr<Handler>();
			else
				return Handler;
		}
	};

	/**
	 * @brief Passes the handler of each device interrupt through @c hook::decorate_isr
	 *
	 *        The decoration a generated vector table takes as a template
	 *        argument -- SvdGenerator's @c vector_table<Unhandled, Decoration>,
	 *        or any table that asks for a type with a member
	 *        @c template<isr_handler_t> @c static @c constexpr @c isr_handler_t @c wrap.
	 *        It is what keeps a tracer wired to @c decorate_isr seeing every
	 *        interrupt handler the table holds.
	 *
	 * @code
	 * constexpr auto g_vectors = STM32H563::vector_table<Default_Handler, opsy::hooked_isr>(&_estack, Reset_Handler)
	 *     .with(opsy::scheduler::isrs)
	 *     .with<USART3_IRQn, uart::rx_isr>();
	 * @endcode
	 */
	struct hooked_isr
	{
		template<void (*Handler)()>
		static constexpr void (*wrap)() = hook::decorate_isr<Handler>();
	};
}
