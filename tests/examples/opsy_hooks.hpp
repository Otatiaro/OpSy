/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   Example hooks: the longest run of each OpSy handler, in cycles.
 *
 *          hooks.hpp includes this file when it is on the include path, and
 *          calls the hooks it defines; the ones it does not define are not
 *          called. The measuring is the few lines that read the DWT cycle
 *          counter. The README shows them.
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

#include <atomic>
#include <cstdint>

#include <callback.hpp>
#include <config.hpp>
#include <cortex_m.hpp>

namespace opsy
{

/** @brief The longest run of each handler since the scheduler started, in core cycles */
struct latency
{
	static inline std::atomic<uint32_t> service_call = 0; ///< @c SVC_Handler, as a whole
	static inline std::atomic<uint32_t> systick      = 0; ///< @c SysTick_Handler, as a whole
	static inline std::atomic<uint32_t> pend_sv      = 0; ///< @c PendSV_Handler, up to the choice of the next task
	static inline std::atomic<uint32_t> isr          = 0; ///< the longest decorated interrupt handler

	/**
	 * @brief Raises @p longest to the cycles elapsed since @p entered when they are more
	 * @remark One atomic step: decorated handlers of different priorities all
	 *         record into @c isr, and a handler preempted between its compare
	 *         and its store would otherwise overwrite a larger maximum.
	 */
	static void record(std::atomic<uint32_t>& longest, uint32_t entered)
	{
		longest.fetch_max(cortex_m::cycle_count() - entered, std::memory_order_relaxed);
	}
};

class hooks
{
public:
	/** @brief Starts the cycle counter: after a reset it does not run unless a debugger started it */
	static void starting(idle_task_control_block& /*idle*/, uint32_t /*core_clock*/, callback<void(callback<void(const task_control_block&)>)> /*for_each_task*/)
	{
		(void) cortex_m::enable_cycle_count();
	}

	static void enter_service_call() { service_call_entered_ = cortex_m::cycle_count(); }
	static void exit_service_call(bool /*task_switch*/) { latency::record(latency::service_call, service_call_entered_); }
	static void enter_systick() { systick_entered_ = cortex_m::cycle_count(); }
	static void exit_systick(bool /*task_switch*/) { latency::record(latency::systick, systick_entered_); }
	static void enter_pend_sv() { pend_sv_entered_ = cortex_m::cycle_count(); }
	static void task_started(task_control_block& /*task*/) { latency::record(latency::pend_sv, pend_sv_entered_); }
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

private:
	static inline uint32_t service_call_entered_ = 0;
	static inline uint32_t systick_entered_      = 0;
	static inline uint32_t pend_sv_entered_      = 0;
};

} // namespace opsy
