/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   The bench image's hooks: timestamps along a wake-up, and the
 *          SysTick entry a sleeping task is woken from.
 *
 *          On the include path of the bench image only, where hooks.hpp
 *          takes it and calls the hooks it defines. What they record is in
 *          bench.cpp: bench::mark() keeps the cycle counter at a named point
 *          of the current scenario.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>

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
	count,
};

void mark(point reached);
void systick_entered();

} // namespace bench

namespace opsy
{

class hooks
{
public:
	static void condition_variable_notify_one(condition_variable& /*condition*/) { bench::mark(bench::point::notify); }
	static void task_ready(task_control_block& /*task*/) { bench::mark(bench::point::ready); }
	static void enter_pend_sv() { bench::mark(bench::point::pend_sv); }
	static void task_started(task_control_block& /*task*/) { bench::mark(bench::point::started); }
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
};

} // namespace opsy
