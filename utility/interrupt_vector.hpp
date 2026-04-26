/**
 ******************************************************************************
 * @file    interrupt_vector.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Compile-time interrupt vector table builder for ARM Cortex-M
 *
 *          Builds a constant-initialised vector table that can be placed in
 *          flash (via @c __attribute__((section(".isr_vector"), used))). Each
 *          system exception slot is a constructor argument; peripheral ISRs
 *          are added by chaining @c with_handler<IRQ, handler>() — every chain
 *          step yields a new @c interrupt_vector with the requested slot
 *          populated, so the whole table remains constant-initialisable.
 *
 *          Peripheral handlers go through @c hooks::decorate_isr so that
 *          tracing hooks (when configured) wrap the user handler.
 *
 *          Example:
 *          @code
 *          constexpr auto g_vectors = opsy::utility::interrupt_vector<IrqCount>(
 *              &_estack,
 *              Reset_Handler,
 *              SysTick_Handler,
 *              SVC_Handler,
 *              PendSV_Handler,
 *              Default_Handler,                 // NMI
 *              Default_Handler                  // HardFault
 *              )
 *              .with_handler<USART3_IRQn,     Uart::rx_isr>()
 *              .with_handler<GPDMA1_CH0_IRQn, Uart::tx_isr>();
 *          @endcode
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
#include <array>

// hooks.hpp references task_control_block / condition_variable / isr_priority
// in its method signatures and is not self-sufficient — pulling the umbrella
// header is the simplest way to make sure every type it touches is visible.
#include "../opsy.hpp"

namespace opsy::utility
{

/**
 * @brief Compile-time builder for the ARM Cortex-M interrupt vector table.
 * @tparam PeripheralIrqs Number of peripheral IRQs the chip exposes
 *         (excluding the 16 system exception slots). The chip vendor's
 *         CMSIS header usually defines a matching constant
 *         (e.g. @c IrqCount, @c MAX_IRQn).
 */
template<std::size_t PeripheralIrqs>
class interrupt_vector
{
public:

	/**
	 * @brief Builds a vector table with the system exception slots
	 *        populated and every peripheral slot left as @c nullptr.
	 * @param end_of_stack            Top-of-stack value the core loads into MSP at reset
	 * @param reset                   @c Reset_Handler
	 * @param systick                 @c SysTick_Handler (defaults to @c nullptr)
	 * @param service_call            @c SVC_Handler (defaults to @c nullptr)
	 * @param pending_service         @c PendSV_Handler (defaults to @c nullptr)
	 * @param non_maskable_interrupt  @c NMI_Handler (defaults to @c nullptr)
	 * @param hard_fault              @c HardFault_Handler (defaults to @c nullptr)
	 * @param memory_management       @c MemManage_Handler (defaults to @c nullptr)
	 * @param bus_fault               @c BusFault_Handler (defaults to @c nullptr)
	 * @param usage_fault             @c UsageFault_Handler (defaults to @c nullptr)
	 * @param debug_monitor           @c DebugMon_Handler (defaults to @c nullptr)
	 */
	constexpr interrupt_vector(
		uint32_t* end_of_stack,
		cortex_m::isr_handler_t reset,
		cortex_m::isr_handler_t systick = nullptr,
		cortex_m::isr_handler_t service_call = nullptr,
		cortex_m::isr_handler_t pending_service = nullptr,
		cortex_m::isr_handler_t non_maskable_interrupt = nullptr,
		cortex_m::isr_handler_t hard_fault = nullptr,
		cortex_m::isr_handler_t memory_management = nullptr,
		cortex_m::isr_handler_t bus_fault = nullptr,
		cortex_m::isr_handler_t usage_fault = nullptr,
		cortex_m::isr_handler_t debug_monitor = nullptr) :
			system_(
					end_of_stack, reset, systick, service_call,
					pending_service, non_maskable_interrupt, hard_fault, memory_management,
					bus_fault, usage_fault, debug_monitor)
	{
	}

	/**
	 * @brief Returns a copy of this vector with @p handler installed at
	 *        peripheral slot @p IRQ.
	 * @tparam IRQ      The peripheral IRQ number (any integral or enum type
	 *                  comparable to @c PeripheralIrqs — the vendor's
	 *                  @c IRQn_Type works as-is).
	 * @tparam handler  The user handler. Wrapped by @c hooks::decorate_isr
	 *                  so any configured tracing hook fires around it.
	 */
	template<auto IRQ, cortex_m::isr_handler_t handler>
	constexpr auto with_handler() const -> interrupt_vector
	{
		static_assert(IRQ < PeripheralIrqs, "IRQ number is outside the peripheral range");
		auto tmp = peripherals_;
		tmp[IRQ] = hooks::decorate_isr<handler>();
		return interrupt_vector(system_, tmp);
	}

private:

	struct system_block
	{
		constexpr system_block(
				uint32_t* end_of_stack,
				cortex_m::isr_handler_t reset,
				cortex_m::isr_handler_t systick,
				cortex_m::isr_handler_t service_call,
				cortex_m::isr_handler_t pending_service,
				cortex_m::isr_handler_t non_maskable_interrupt,
				cortex_m::isr_handler_t hard_fault,
				cortex_m::isr_handler_t memory_management,
				cortex_m::isr_handler_t bus_fault,
				cortex_m::isr_handler_t usage_fault,
				cortex_m::isr_handler_t debug_monitor) :
				end_of_stack_(end_of_stack),
				reset_(reset),
				non_maskable_interrupt_(non_maskable_interrupt),
				hard_fault_(hard_fault),
				memory_management_(memory_management),
				bus_fault_(bus_fault),
				usage_fault_(usage_fault),
				service_call_(service_call),
				debug_monitor_(debug_monitor),
				pending_service_(pending_service),
				systick_(systick) {}

		// Layout matches the ARMv7-M / ARMv8-M Mainline vector table — DO NOT
		// reorder these fields, the hardware reads them at fixed offsets.
		uint32_t*               end_of_stack_;
		cortex_m::isr_handler_t reset_;
		cortex_m::isr_handler_t non_maskable_interrupt_;
		cortex_m::isr_handler_t hard_fault_;
		cortex_m::isr_handler_t memory_management_;
		cortex_m::isr_handler_t bus_fault_;
		cortex_m::isr_handler_t usage_fault_;
		const std::array<cortex_m::isr_handler_t, 4> reserved_0_ = { nullptr, nullptr, nullptr, nullptr };
		cortex_m::isr_handler_t service_call_;
		cortex_m::isr_handler_t debug_monitor_;
		const std::array<cortex_m::isr_handler_t, 1> reserved_1_ = { nullptr };
		cortex_m::isr_handler_t pending_service_;
		cortex_m::isr_handler_t systick_;
	};

	constexpr interrupt_vector(const system_block system, const std::array<cortex_m::isr_handler_t, PeripheralIrqs> peripherals) :
			system_(system), peripherals_(peripherals)
	{
	}

public:

	const system_block system_;
	std::array<cortex_m::isr_handler_t, PeripheralIrqs> peripherals_{ {} };
};

} // namespace opsy::utility
