/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   The bench image's hooks: timestamps along a wake-up, and the
 *          SysTick entry a sleeping task is woken from.
 *
 *          On the include path of the bench image only, where hooks.hpp
 *          takes it in place of its own class. Every hook OpSy calls has to
 *          exist; the ones that record are the few below, the rest are
 *          empty. What they record is in bench.cpp: bench::mark() keeps the
 *          cycle counter at a named point of the current scenario.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>

#include <callback.hpp>
#include <config.hpp>
#include <isr_priority.hpp>

namespace bench
{

/** @brief The points along an ISR-to-task wake-up that the hooks can see */
enum class point : uint8_t
{
	notify,   ///< condition_variable::notify_one, lock taken
	ready,    ///< the woken task inserted in the ready list
	isr_exit, ///< the decorated interrupt handler about to return
	pend_sv,  ///< first line of pend_sv_handler
	started,  ///< pend_sv_handler chose the task, about to restore it
	count
};

void mark(point);
void systick_entered();

} // namespace bench

namespace opsy
{

class task_control_block;
class idle_task_control_block;
class condition_variable;
class mutex;

class hooks
{
public:
	static constexpr void starting(idle_task_control_block&, uint32_t, callback<void(callback<void(const task_control_block&)>)>) {}

	static void condition_variable_notify_one(condition_variable&) { bench::mark(bench::point::notify); }
	static void task_ready(task_control_block&) { bench::mark(bench::point::ready); }
	static void enter_pend_sv() { bench::mark(bench::point::pend_sv); }
	static void task_started(task_control_block&) { bench::mark(bench::point::started); }
	static void enter_systick() { bench::systick_entered(); }

	/** @brief The handler the vector table holds in place of @p Routine: the same, its exit marked */
	template<void (*Routine)()>
	static constexpr auto decorate_isr()
	{
		return +[]
		{
			Routine();
			bench::mark(bench::point::isr_exit);
		};
	}

	// The hooks this file does not use. Every one OpSy calls has to exist.
	static constexpr void enter_service_call() {}
	static constexpr void exit_service_call(bool) {}
	static constexpr void exit_systick(bool) {}
	static constexpr void enter_idle() {}
	static constexpr void task_added(task_control_block&) {}
	static constexpr void task_terminated(task_control_block&) {}
	static constexpr void task_sleep(task_control_block&) {}
	static constexpr void task_stopped(task_control_block&) {}
	static constexpr void task_wait(task_control_block&, condition_variable&) {}
	static constexpr void task_wait_timeout(task_control_block&, condition_variable&, time_point) {}
	static constexpr void task_name_changed(task_control_block&) {}
	static constexpr void task_priority_changed(task_control_block&) {}
	static constexpr void enter_critical_section() {}
	static constexpr void exit_critical_section() {}
	static constexpr void mutex_taken(mutex&, task_control_block&) {}
	static constexpr void mutex_released(mutex&, task_control_block&) {}
	static constexpr void mutex_blocked(mutex&, task_control_block&) {}
	static constexpr void mutex_stored_for_task(task_control_block&) {}
	static constexpr void mutex_restored_for_task(task_control_block&) {}
	static constexpr void enter_full_lock() {}
	static constexpr void exit_full_lock() {}
	static constexpr void enter_priority_lock(isr_priority) {}
	static constexpr void exit_priority_lock() {}
	static constexpr void condition_variable_start_waiting(condition_variable&, task_control_block&) {}
	static constexpr void condition_variable_start_waiting(condition_variable&, task_control_block&, duration) {}
	static constexpr void condition_variable_notify_all(condition_variable&) {}
};

} // namespace opsy
