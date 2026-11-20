/**
 ******************************************************************************
 * @file    fault.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    20-November-2026
 * @brief   One handler for the four fault exceptions, placed in a vector
 *          table built at compile time
 *
 *          HardFault, MemManage, BusFault and UsageFault all land in one
 *          function the application writes, with the fault status decoded,
 *          the frame the core stacked, and @c EXC_RETURN. Returning from it
 *          returns from the exception, to the frame as the handler left it.
 *
 ******************************************************************************
 * @copyright Copyright 2026 Thomas Legrand under the MIT License
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

#include <concepts>
#include <cstdint>

#include "cortex_m.hpp"

namespace opsy
{

/**
 * @brief The registers the core stacks on exception entry, as the handler finds them
 *
 *        The frame of the code that faulted, on the stack it was running on.
 *        @c pc is the instruction the fault is reported on: the faulting one
 *        for a precise fault, the one after for an imprecise bus fault. A
 *        handler that writes @c pc returns there.
 *
 * @remark Only the basic frame. When bit 4 of @c EXC_RETURN is clear
 *         (@ref cortex_m::exc_return_fp_flag), the floating-point state
 *         follows it on the stack.
 */
struct exception_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t xpsr;
};

/**
 * @brief What an application's fault handler looks like
 *
 *        Called for HardFault, MemManage, BusFault and UsageFault alike;
 *        @p status says which, and why. Returning resumes at @p frame.pc with
 *        the frame as left: a handler that steps over the faulting instruction
 *        does so by moving @c pc, and clears the status with
 *        @ref cortex_m::clear_fault_status first. One that does not return --
 *        resets, halts, or stops the task -- never comes back here.
 *
 * @param status The fault status registers, read on entry and left as they are
 * @param frame The stacked registers of the code that faulted; writable
 * @param exc_return @c EXC_RETURN as the core set it: which stack holds the frame (@ref cortex_m::exc_return_psp_flag), whether a task or a handler faulted (@ref cortex_m::exc_return_thread_flag)
 */
using fault_handler_t = void (*)(const fault_status& status, exception_frame& frame, uint32_t exc_return);

namespace detail
{

/**
 * @brief The C++ half of the fault entry: reads the status and calls the handler
 *
 *        Reached by a branch, not a call, so its @c lr is @c EXC_RETURN and
 *        its own return is the exception return.
 */
template<fault_handler_t OnFault>
void fault_dispatch(exception_frame* frame, uint32_t exc_return)
{
	OnFault(cortex_m::read_fault_status(), *frame, exc_return);
}

/**
 * @brief The entry the vector table holds: finds the frame on whichever stack it is, and dispatches
 *
 *        @c EXC_RETURN bit 2 says which stack pointer the frame was pushed on.
 *        Nothing is pushed here: the frame is exactly where the core left it,
 *        and @c lr still holds @c EXC_RETURN when the dispatch returns through it.
 */
template<fault_handler_t OnFault>
[[gnu::naked]] void fault_entry()
{
	asm volatile(
			"tst lr, %[psp_flag] \n\t"
			"ite eq \n\t"
			"mrseq r0, msp \n\t"
			"mrsne r0, psp \n\t"
			"mov r1, lr \n\t"
			"b %[dispatch]"
			:
			: [dispatch] "g" (fault_dispatch<OnFault>),
			  [psp_flag] "I" (cortex_m::exc_return_psp_flag));
}

} // namespace detail

/**
 * @brief What @ref fault_isrs needs of a vector table built at compile time
 *
 *        Names the four fault exceptions through @c Table::exception_type, and
 *        fills a slot with @c with<exception, handler>(), returning the table.
 *        The same shape as @ref vector_table, over the other exceptions.
 */
template<typename Table, fault_handler_t OnFault>
concept fault_vector_table = requires(const Table table)
{
	{ table.template with<Table::exception_type::hard_fault, detail::fault_entry<OnFault>>() } -> std::same_as<Table>;
	{ table.template with<Table::exception_type::mem_manage, detail::fault_entry<OnFault>>() } -> std::same_as<Table>;
	{ table.template with<Table::exception_type::bus_fault, detail::fault_entry<OnFault>>() } -> std::same_as<Table>;
	{ table.template with<Table::exception_type::usage_fault, detail::fault_entry<OnFault>>() } -> std::same_as<Table>;
};

/**
 * @brief Places @p OnFault behind HardFault, MemManage, BusFault and UsageFault in a vector table built at compile time
 *
 *        Given to the table's @c with(install), next to @ref scheduler::isrs:
 *
 * @code
 * void on_fault(const opsy::fault_status& status, opsy::exception_frame& frame, uint32_t exc_return);
 *
 * constexpr auto g_vectors = STM32H563::vector_table<Default_Handler, opsy::hooked_isr>(&_estack, Reset_Handler)
 *     .with(opsy::scheduler::isrs)
 *     .with(opsy::fault_isrs<on_fault>);
 * @endcode
 *
 *        The three configurable faults reach @p OnFault through HardFault
 *        until @ref cortex_m::enable_configurable_faults gives them their own
 *        exceptions; either way the handler runs, and
 *        @ref fault_status::escalated tells which way it came.
 *
 * @tparam OnFault The application's handler, see @ref fault_handler_t
 */
template<fault_handler_t OnFault>
constexpr auto fault_isrs = []<typename Table>(const Table& table) -> Table
	requires fault_vector_table<Table, OnFault>
{
	using exception = typename Table::exception_type;
	return table.template with<exception::hard_fault, detail::fault_entry<OnFault>>()
	            .template with<exception::mem_manage, detail::fault_entry<OnFault>>()
	            .template with<exception::bus_fault, detail::fault_entry<OnFault>>()
	            .template with<exception::usage_fault, detail::fault_entry<OnFault>>();
};

} // namespace opsy
