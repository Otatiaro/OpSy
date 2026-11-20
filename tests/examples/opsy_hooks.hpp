/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   Example hooks: the longest run of each OpSy handler, in cycles.
 *
 *          hooks.hpp includes this file in place of its own class when it is
 *          on the include path. It then has to define every hook OpSy calls,
 *          which is most of the file; the measuring is the few lines that
 *          read the DWT cycle counter. The README shows those.
 *
 *          Kept in opsy::latency, as the longest seen so far:
 *          - the service call handler, enter_service_call to exit_service_call;
 *          - the SysTick handler, enter_systick to exit_systick;
 *          - the C++ half of PendSV, enter_pend_sv to the choice of the next
 *            task (task_started) or of idle (enter_idle);
 *          - every interrupt handler the vector table passes through
 *            decorate_isr, as a whole.
 *
 *          Each of OpSy's handlers keeps its own entry time: SysTick and the
 *          service call preempt PendSV. A decorated handler may be preempted
 *          by another, so it keeps its entry time on its own stack.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>

#include <callback.hpp>
#include <config.hpp>
#include <cortex_m.hpp>
#include <isr_priority.hpp>

namespace opsy
{

// Declared, not included: the headers that define these include hooks.hpp,
// and they appear in signatures only.
class task_control_block;
class idle_task_control_block;
class condition_variable;
class mutex;

/** @brief The longest run of each handler since the scheduler started, in core cycles */
struct latency
{
	static inline uint32_t service_call = 0; ///< @c SVC_Handler, as a whole
	static inline uint32_t systick      = 0; ///< @c SysTick_Handler, as a whole
	static inline uint32_t pend_sv      = 0; ///< @c PendSV_Handler, up to the choice of the next task
	static inline uint32_t isr          = 0; ///< the longest decorated interrupt handler

	/** @brief Raises @p longest to the cycles elapsed since @p entered when they are more */
	static void record(uint32_t& longest, uint32_t entered)
	{
		const uint32_t elapsed = cortex_m::cycle_count() - entered;
		if (elapsed > longest)
			longest = elapsed;
	}
};

class hooks
{
public:
	/** @brief Starts the cycle counter: after a reset it does not run unless a debugger started it */
	static void starting(idle_task_control_block&, uint32_t, callback<void(callback<void(const task_control_block&)>)>)
	{
		(void) cortex_m::enable_cycle_count();
	}

	static void enter_service_call() { service_call_entered_ = cortex_m::cycle_count(); }
	static void exit_service_call(bool) { latency::record(latency::service_call, service_call_entered_); }
	static void enter_systick() { systick_entered_ = cortex_m::cycle_count(); }
	static void exit_systick(bool) { latency::record(latency::systick, systick_entered_); }
	static void enter_pend_sv() { pend_sv_entered_ = cortex_m::cycle_count(); }
	static void task_started(task_control_block&) { latency::record(latency::pend_sv, pend_sv_entered_); }
	static void enter_idle() { latency::record(latency::pend_sv, pend_sv_entered_); }

	/** @brief The handler the vector table holds in place of @p Routine: the same, timed */
	template<void (*Routine)()>
	static constexpr auto decorate_isr()
	{
		return +[]
		{
			const uint32_t entered = cortex_m::cycle_count();
			Routine();
			latency::record(latency::isr, entered);
		};
	}

	// The hooks this example does not use. Every one OpSy calls has to exist.
	static constexpr void task_added(task_control_block&) {}
	static constexpr void task_terminated(task_control_block&) {}
	static constexpr void task_sleep(task_control_block&) {}
	static constexpr void task_stopped(task_control_block&) {}
	static constexpr void task_wait(task_control_block&, condition_variable&) {}
	static constexpr void task_wait_timeout(task_control_block&, condition_variable&, time_point) {}
	static constexpr void task_ready(task_control_block&) {}
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
	static constexpr void condition_variable_notify_one(condition_variable&) {}
	static constexpr void condition_variable_notify_all(condition_variable&) {}

private:
	static inline uint32_t service_call_entered_ = 0;
	static inline uint32_t systick_entered_      = 0;
	static inline uint32_t pend_sv_entered_      = 0;
};

} // namespace opsy
