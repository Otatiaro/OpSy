/**
 ******************************************************************************
 * @file    cortex_m.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Cortex-M (M3/M4/M7/M33) low level features and system peripherals access
 *			These are mainly for OpSy usage, you should not directly use these
 *			but preferably use OpSy equivalent features.
 *			Especially, do not modify system interrupts configuration
 *			nor use interrupt masking directly.
 *
 *			You can still use these to configure peripheral interrupts.
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

#include <cassert>
#include <optional>

#include "config.hpp"
#include "isr_priority.hpp"

namespace
{

template<typename Type>
class memory_register
{
public:

	explicit constexpr memory_register(uint32_t address) :
			address_(address)
	{

	}

	constexpr uint32_t address() const
	{
		return address_;
	}

	Type inline get() const
	{
		return *reinterpret_cast<Type*>(address_);
	}

	inline void set(Type value) const
	{
		*reinterpret_cast<Type*>(address_) = value;
	}

private:
	const uint32_t address_;
	memory_register& operator=(const memory_register& other) = delete;

};
}

namespace opsy
{

/**
 * The maximum value of the @c PRIGROUP register
 * @warning It is the maximum value, not the total number of values, with is this value + 1 (ending up at 8)
 */
static constexpr std::size_t prigroup_max = 7;

/**
 * @brief Contains all addresses and static methods to access Cortex-M low level features and system peripherals (NVIC, Systick, etc.)
 */
class cortex_m
{
public:

	/**
	 * @brief An Interrupt Service Routine (ISR) entry point definition, ISR are static or global methods with no return type and no arguments
	 */
	using isr_handler_t = void(*)(void);

	/**
	 * @brief Number of system interrupt requests in the Cortex-M
	 */
	static constexpr uint32_t system_irqs_count = 16;

	/**
	 * @brief Alignment of the SCB VTOR register. This alignment is MANDATORY to declare a RAM interrupt vector.
	 */
	static constexpr uint32_t vtor_alignment = 0x200;

	/**
	 * @brief System interrupt requests
	 */
	enum class system_irq
		: uint32_t
		{
			initial_sp = 0, ///< Value set to MSP at startup (it is not an isr_handler_t but a memory pointer)
		reset = 1, ///< First code to be executed at system reset
		non_maskable_interrupt = 2, ///< Non maskable interrupt
		hard_fault = 3, ///< Hard Fault
		service_call = 11, ///< Service Call, used by OpSy for precise calls and automatic interrupt masking
		pend_sv = 14, ///< PendSV, used by OpSy for task switch
		systick = 15, ///< Systick, used by OpSy as main clock source
	};

	/**
	 * @brief Cortex-M types
	 * @remark Values are the @c PARTNO field of the @c CPUID register. Only the variants
	 *         supported by OpSy (M3, M4, M7, M33) are listed.
	 */
	enum class core_type
		: uint16_t
		{
			m3 = 0xc23, ///< Cortex-M3 (ARMv7-M, no FPU)
		m4 = 0xc24, ///< Cortex-M4 (ARMv7E-M)
		m7 = 0xc27, ///< Cortex-M7 (ARMv7E-M)
		m33 = 0xd21, ///< Cortex-M33 (ARMv8-M Mainline)
	};

	/**
	 * @brief Lowest priority available in the system
	 * @remark Still an ISR priority, a task runs at no priority, so an ISR with this priority still preempts tasks
	 */
	static constexpr isr_priority lowest_priority = isr_priority(0xFF);

	/**
	 * @brief Highest priority available in the system
	 */
	static constexpr isr_priority highest_priority = isr_priority(0);

	/**
	 * @brief Indicates whether the target architecture provides hardware stack-limit registers (MSPLIM / PSPLIM).
	 *
	 *        @c true on ARMv8-M (Cortex-M23, M33, M55, M85), @c false on ARMv7-M (Cortex-M3, M4, M7).
	 *        Detected at compile time from the toolchain-predefined architecture macro so that
	 *        callers can use @c if @c constexpr and pay no runtime cost on architectures without the registers.
	 */
	static constexpr bool has_stack_limit_regs =
#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
		true;
#else
		false;
#endif

	/**
	 * @brief Bit 4 of @c EXC_RETURN. When @b cleared on exception entry, the hardware pushed
	 *        the extended (FP) stack frame in addition to the basic frame; when @b set, only
	 *        the basic frame is on the stack and the FPU was inactive.
	 */
	static constexpr uint32_t exc_return_fp_flag = 1u << 4;

	/**
	 * @brief Bit 3 of @c EXC_RETURN. @b Set = the exception will return to Thread mode;
	 *        @b cleared = it will return to Handler mode.
	 */
	static constexpr uint32_t exc_return_thread_flag = 1u << 3;

	/**
	 * @brief Bit 2 of @c EXC_RETURN. @b Set = the entry stack pointer was @c PSP (Thread mode
	 *        was running); @b cleared = it was @c MSP (Handler mode was running).
	 */
	static constexpr uint32_t exc_return_psp_flag = 1u << 2;

	/**
	 * @brief @c EXC_RETURN value used to initialise a fresh task context: return to Thread
	 *        mode, use @c PSP, basic stack frame (no FP context). Equivalent to
	 *        @c 0xFFFFFFFD on ARMv7-M / ARMv8-M Mainline.
	 */
	static constexpr uint32_t exc_return_thread_psp_basic = 0xFFFFFFFDu;

	/**
	 * @brief @c CONTROL register value used to initialise a task: privileged Thread mode
	 *        using @c PSP (@c SPSEL=1, @c nPRIV=0). Equivalent to @c 0b10.
	 */
	static constexpr uint32_t control_thread_psp_privileged = 0b10u;

	/**
	 * @brief Indicates whether the running target has a hardware Floating Point Unit.
	 *
	 *        Detected from the toolchain-predefined @c __ARM_FP macro (set by GCC when
	 *        @c -mfpu / @c -mfloat-abi=hard are used). On cores without an FPU (Cortex-M3),
	 *        @ref enable_fpu refuses to instantiate and the @c PendSV context-switch path
	 *        skips FP register save / restore so no clock cycle is spent on an absent feature.
	 */
	static constexpr bool has_fpu =
#if defined(__ARM_FP)
		true;
#else
		false;
#endif

	/**
	 * @brief Gets the current executing Cortex-M type
	 * @return The Cortex-M type, read from the @c CPUID @c PARTNO field. Returns one of the
	 *         values listed in @ref core_type if the running core is supported by OpSy.
	 */
	static core_type type()
	{
		const auto cpuid = memory_register<uint32_t>(ScbCpuidAddress).get();
		return static_cast<core_type>((cpuid >> CpuidPartNoPos) & CpuidPartNoMask);
	}

	/**
	 * @brief Gets the current number of preemption bits
	 * @return The current number of preemption bits
	 */
	static uint8_t preempt_bits()
	{
		return static_cast<uint8_t>(prigroup_max + 1u - priority_grouping());
	}

	/**
	 * @brief Sets the number of preemption bits
	 * @param value The required number of preemption bits
	 * @warning The @p value should be between @c 0 and the number of priority bits actually implemented in the Cortex
	 */
	static void preempt_bits(uint8_t value)
	{
		priority_grouping(static_cast<uint8_t>(prigroup_max + 1u - value));
	}

	/**
	 * @brief Enables the System Tick (Systick) counter with the specified reload counter
	 * @param reload The reload counter, counter is decremented each clock cycle and a Systick interrupt is generated when it reaches @c 0, then reloaded with this value
	 */
	static void enable_systick(uint32_t reload)
	{
		assert(reload != 0);
		assert(reload - 1 <= SystickReloadMask);
		memory_register<uint32_t>(SystickCtrlAddress).set(0); // stop timer in case it was already started
		memory_register<uint32_t>(SystickLoadAddress).set(reload - 1); // set the reload value
		memory_register<uint32_t>(SystickValAddress).set(0); // reset the counter
		memory_register<uint32_t>(SystickCtrlAddress).set(SystickCtrlClkSource | SystickCtrlTickInt | SystickCtrlEnable); // and start the timer
	}

	/**
	 * @brief Gets the current Systick counter value
	 * @return The current Systick value
	 * @remark This value is actually going up, and is the difference between the reload value and the current value of the timer (which decrements)
	 */
	static uint32_t systick_count()
	{
		return memory_register<uint32_t>(SystickLoadAddress).get() - memory_register<uint32_t>(SystickValAddress).get();
	}

	/**
	 * @brief Enables a peripheral interrupt
	 * @param irq The interrupt request to enable
	 * @warning Make sure the handler and priority are set before you enable an interrupt
	 */
	static void enable_interrupt(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		memory_register<uint32_t>(NvicIserAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Disables a peripheral interrupt
	 * @param irq The interrupt request to disable
	 */
	static void disable_interrupt(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		memory_register<uint32_t>(NvicIcerAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Checks if a peripheral interrupt is pending
	 * @param irq The interrupt request to check
	 * @return @c true if the interrupt is pending, @c false otherwise
	 */
	static bool is_pending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return (memory_register<uint32_t>(NvicIsprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).get() & (1u << (irq & NvicIrqRegisterMask))) != 0;
	}

	/**
	 * @brief Sets the pending status for a peripheral interrupt
	 * @param irq The interrupt request to set pending flag for
	 */
	static void set_pending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		memory_register<uint32_t>(NvicIsprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Clears the pending flag for a peripheral interrupt
	 * @param irq The interrupt request to clear pending flag for
	 */
	static void clear_pending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		memory_register<uint32_t>(NvicIcprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Triggers an instruction synchronization barrier
	 */
	static inline void instruction_barrier()
	{
		asm volatile("isb");
	}

	/**
	 * @brief  Triggers a data synchronization barrier
	 */
	static inline void data_barrier()
	{
		asm volatile("dsb");
	}

	/**
	 * @brief Checks if a peripheral interrupt is active
	 * @param irq The interrupt request to check
	 * @return @c true if the interrupt is active, @c false otherwise
	 */
	static bool is_active(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return (memory_register<uint32_t>(NvicIabrAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).get() & (1u << (irq & NvicIrqRegisterMask))) != 0;
	}

	/**
	 * @brief Sets the priority for a peripheral interrupt
	 * @param irq The interrupt request to set priority for
	 * @param priority The priority
	 */
	static void set_priority(uint32_t irq, isr_priority priority)
	{
		assert(irq <= MaxIrq);
		memory_register<uint8_t>(NvicIpAddress + irq).set(priority.value());
	}

	/**
	 * @brief Gets the current priority for a peripheral interrupt
	 * @param irq The interrupt request to get priority for
	 * @return The interrupt priority
	 */
	static isr_priority priority(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return isr_priority(memory_register<uint8_t>(NvicIpAddress + irq).get());
	}

	/**
	 * @brief Sets the priority for a system interrupt
	 * @param irq The interrupt request to set priority for
	 * @param priority The priority
	 * @warning Only @c system_irq::non_maskable_interrupt, @c system_irq::hard_fault, @c system_irq::service_call, @c system_irq::pend_sv and @c system_irq::systick are configurable
	 */
	static void set_priority(system_irq irq, isr_priority priority)
	{
		switch (irq)
		{
		case system_irq::non_maskable_interrupt:
		case system_irq::hard_fault:
		case system_irq::service_call:
		case system_irq::pend_sv:
		case system_irq::systick:
			memory_register<uint8_t>(ScbShpAddress + static_cast<uint32_t>(irq)).set(priority.value());
			break;
		case system_irq::initial_sp:
		case system_irq::reset:
		default:
			assert(false);
			break;
		}
	}

	/**
	 * @brief Gets the current priority for a system interrupt
	 * @param irq The interrupt request to get priority for
	 * @return The current priority
	 * @warning Only @c system_irq::non_maskable_interrupt, @c system_irq::hard_fault, @c system_irq::service_call, @c system_irq::pend_sv and @c system_irq::systick are configurable
	 */
	static isr_priority priority(system_irq irq)
	{
		switch (irq)
		{
		case system_irq::non_maskable_interrupt:
		case system_irq::hard_fault:
		case system_irq::service_call:
		case system_irq::pend_sv:
		case system_irq::systick:
			return isr_priority(memory_register<uint8_t>(ScbShpAddress + static_cast<uint32_t>(irq)).get());
			break;
		case system_irq::initial_sp:
		case system_irq::reset:
		default:
			assert(false);
			return isr_priority(0);
		}
	}

	/**
	 * @brief Minimum (lowest) preemption priority available
	 * @tparam PreemptBits Number of preemption bits
	 * @return The minimum (lowest) preemption priority available
	 */
	template<std::size_t PreemptBits = preemption_bits>
	static constexpr uint8_t min_sub()
	{
		return (1 << (prigroup_max + 1 - PreemptBits)) - 1;
	}

	/**
	 * @brief Minimum (lowest) sub-priority available
	 * @tparam PreemptBits Number of preemption bits
	 * @return The minimum (lowest) sub-priority available
	 */
	template<std::size_t PreemptBits = preemption_bits>
	static constexpr uint8_t min_preempt()
	{
		return (1 << PreemptBits) - 1;
	}

	/**
	 * @brief Generates a system reset
	 * @remark This call only sets the required bit, but it may take a couple more cycles before the reset is effective
	 */
	static void __attribute__((noreturn)) reset()
	{
		memory_register<uint32_t>(AircrAddress).set(AircrVectkeyValue | AircrSysreset);
	}

	/**
	 * @brief Gets the current interrupt handler vector
	 * @return The current interrupt handler vector
	 */
	static isr_handler_t* vtor()
	{
		return reinterpret_cast<isr_handler_t*>(memory_register<uint32_t>(ScbVtorAddress).get());
	}

	/**
	 * @brief Moves the interrupt handler vector to a new location
	 * @param new_vtor The new interrupt handler vector
	 * @param copy_size Number of handlers to copy from the previous to the new one
	 * @remark It is most preferable to move interrupt handler vector at system startup before any interrupt is active
	 */
	static void move_vtor(isr_handler_t* new_vtor, std::size_t copy_size = 0)
	{
		assert((new_vtor != nullptr) && (reinterpret_cast<uint32_t>(new_vtor) % vtor_alignment == 0)); // check vtor is not null and correctly aligned
		assert(copy_size <= MaxIrq + system_irqs_count);

		for (auto i = 0u; i < copy_size; ++i)
			new_vtor[i] = vtor()[i];

		memory_register<uint32_t>(ScbVtorAddress).set(reinterpret_cast<uint32_t>(new_vtor));
	}

	/**
	 * @brief Gets the current interrupt handler for a system interrupt
	 * @param irq The interrupt request to get the handler for
	 * @return The current handler of the specified system interrupt
	 */
	static isr_handler_t isr_handler(system_irq irq)
	{
		switch (irq)
		{
		case system_irq::non_maskable_interrupt:
		case system_irq::hard_fault:
		case system_irq::service_call:
		case system_irq::pend_sv:
		case system_irq::systick:
		case system_irq::reset:
		{
			return vtor()[static_cast<std::size_t>(irq)];
		}
		case system_irq::initial_sp:
		default:
			assert(false);
			return 0;
		}
	}

	/**
	 * @brief Gets the current interrupt handler for a peripheral interrupt
	 * @param irq The interrupt request to get the handler for
	 * @return The current handler of the specified peripheral interrupt
	 */
	static isr_handler_t isr_handler(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return vtor()[irq + system_irqs_count];
	}

	/**
	 * @brief Gets the value set to main stack pointer (MSP) at system reset (or startup)
	 * @return The value set to main stack pointer (MSP) at system reset (or startup)
	 */
	static uint32_t* msp_at_reset()
	{
		return reinterpret_cast<uint32_t*>(*vtor());
	}

	/**
	 * Sets the interrupt service routine for the specified system interrupt
	 * @param irq The interrupt request to set the handler for
	 * @param handler The interrupt service routine handler
	 * @remark This call first checks if the current ISR is already correctly set, and tries to set it otherwise
	 * @warning If you use this method to change the current value, make sure the interrupt handler vector is in writable memory (i.e. not FLASH)
	 */
	static void set_isr_handler(system_irq irq, isr_handler_t handler)
	{
		if (isr_handler(irq) != handler)
			vtor()[static_cast<std::size_t>(irq)] = handler;
	}

	/**
	 * @brief Sets the interrupt service routine for the specified peripheral interrupt
	 * @param irq The interrupt request to set the handler for
	 * @param handler The interrupt service routine handler
	 * @remark This call first checks if the current ISR is already correctly set, and tries to set it otherwise
	 * @warning If you use this method to change the current value, make sure the interrupt handler vector is in writable memory (i.e. not FLASH)
	 */
	static void set_isr_handler(uint32_t irq, isr_handler_t handler)
	{
		assert(irq <= MaxIrq);
		if (isr_handler(irq) != handler)
			vtor()[irq + system_irqs_count] = handler;
	}

	/**
	 * @brief Tries to get the priority of the currently executing interrupt service routine
	 * @return @c nullopt if no ISR currently executing, the @c isr_priority of the current ISR otherwise
	 */
	static std::optional<isr_priority> current_priority()
	{
		auto ipsrValue = ipsr();

		if (ipsrValue == 0)
			return std::nullopt;

		if (ipsrValue < system_irqs_count)
			return priority(static_cast<system_irq>(ipsrValue));
		else
			return priority(ipsrValue - system_irqs_count);
	}

	/**
	 * @brief Gets the value of IPSR register
	 * @return The value of IPSR (Interrupt Program Status Register)
	 */
	static uint32_t ipsr() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, ipsr" : "=r"(result));
		return result;
	}

	/**
	 * @brief Triggers a @c WFI (Wait For Interrupt) instruction
	 */
	static void wfi() __attribute__((always_inline))
	{
		asm volatile("wfi");
	}

	/**
	 * @brief Triggers a @c WFE (Wait For Event) instruction
	 */
	static void wfe() __attribute__((always_inline))
	{
		asm volatile("wfe");
	}

	/**
	 * @brief Outputs a @c NOP instruction, which does nothing
	 */
	static void nop() __attribute__((always_inline))
	{
		asm volatile("nop");
	}

	/**
	 * @brief Gets the current @c MSP (Main Stack Pointer) value
	 * @return The current @c MSP value
	 */
	static uint32_t* msp() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, msp" : "=r" (result));
		return reinterpret_cast<uint32_t*>(result);
	}

	/**
	 * @brief Sets the @c MSP (Main Stack Pointer) value
	 * @param msp The value to set @c MSP to
	 */
	static void set_msp(uint32_t* msp) __attribute__((always_inline))
	{
		asm volatile("msr msp, %0" : : "r"(msp));
	}

	/**
	 * @brief Sets the @c MSPLIM (Main Stack Pointer Limit) register.
	 * @param limit The lower bound of the main stack — UsageFault fires if MSP goes below this address.
	 * @remark No-op at compile time on architectures without stack-limit registers (ARMv7-M and earlier).
	 */
	static void set_msplim(uint32_t* limit) __attribute__((always_inline))
	{
		if constexpr (has_stack_limit_regs)
			asm volatile("msr msplim, %0" : : "r"(limit));
	}

	/**
	 * @brief Gets the current @c PSP (Process Stack Pointer) value
	 * @return The current @c PSP value
	 */
	static uint32_t* psp() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, psp" : "=r" (result));
		return reinterpret_cast<uint32_t*>(result);
	}

	/**
	 * @brief Sets the @c PSP (Process Stack Pointer) value
	 * @param psp The value to set @c PSP to
	 */
	static void set_psp(uint32_t* psp) __attribute__((always_inline))
	{
		asm volatile("msr psp, %0" : : "r"(psp));
	}

	/**
	 * @brief Sets the @c PSPLIM (Process Stack Pointer Limit) register.
	 * @param limit The lower bound of the process stack — UsageFault fires if PSP goes below this address.
	 * @remark No-op at compile time on architectures without stack-limit registers (ARMv7-M and earlier).
	 */
	static void set_psplim(uint32_t* limit) __attribute__((always_inline))
	{
		if constexpr (has_stack_limit_regs)
			asm volatile("msr psplim, %0" : : "r"(limit));
	}

	/**
	 * @brief Sets the @c CONTROL register value
	 * @param control The value to set @c CONTROL register to
	 */
	static void set_control(uint32_t control) __attribute__((always_inline))
	{
		asm volatile("msr control, %0" : : "r" (control) : "memory");
	}

	/**
	 * @brief Gets the current @c CONTROL register value
	 * @return The current @c CONTROL register value
	 */
	static uint32_t control() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, control" : "=r" (result));
		return result;
	}

	/**
	 * @brief Triggers the pending state for system interrupt @c PendSV
	 */
	static inline void trigger_pend_sv()
	{
		memory_register<uint32_t>(IcsrAddress).set(IcsrPendSvSet);
	}

	/**
	 * @brief Clears the pending state for system interrupt @c PendSV
	 */
	static inline void clear_pend_sv()
	{
		memory_register<uint32_t>(IcsrAddress).set(IcsrPendSvClr);
	}

	/**
	 * @brief Sets the @c BASEPRI register value, and gets back its previous value
	 * @param priority The new @c isr_priority to set @c BASEPRI register to
	 * @return The previous value of @c BASEPRI register
	 */
	static inline isr_priority set_basepri(isr_priority priority = isr_priority(0)) __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile(
				"mrs %[output], basepri \n\t" // get previous value of basepri
				"msr basepri, %[input] \n\t"// set the new value
				"isb"// make sure it is into effect before returning
				: [output] "=&r" (result)
				: [input] "r" (priority.value())
				: );
		return isr_priority(static_cast<uint8_t>(result));
	}

	/**
	 * @brief Disables all interrupts by setting @c PRIMASK register to 1
	 */
	static inline void disable_interrupts() __attribute__((always_inline))
	{
		asm volatile(
				"cpsid i \n\t"
				"isb"
				: : : "memory");
	}

	/**
	 * @brief Enables all interrupts by setting @c PRIMASK to 0
	 * @warning This does not mean all peripheral interrupts are enabled, it means they are not masked by @c PRIMASK anymore
	 */
	static inline void enable_interrupts() __attribute__((always_inline))
	{
		asm volatile(
				"cpsie i \n\t"
				"isb"
				: : : "memory");
	}

	/**
	 * @brief Checks if PRIMASK register is set to @c 1
	 * @return @c true if @c PRIMASK is set to @c 1 (all interrupts disabled), @c false otherwise
	 */
	static inline bool is_primask() __attribute__((always_inline))
	{
		uint32_t value;
		asm volatile("mrs %[output], primask" : [output] "=r" (value));
		return value != 0;
	}

	/**
	 * @brief Enables the Floating Point Unit (FPU) on devices which have the FPU coprocessor
	 * @remark Calling this on a target without an FPU (Cortex-M3 in this codebase) is a
	 *         compile-time error: the @c CPACR register is reserved on those cores and
	 *         writing to it is undefined behaviour. The user project is responsible for
	 *         calling @c enable_fpu only on targets that have one.
	 * @tparam Enable Defaulted to @ref has_fpu so the compile-time error is reported at the
	 *         call site, not at the definition site, with a readable message.
	 */
	template<bool Enable = has_fpu>
	[[gnu::always_inline]] static void enable_fpu()
	{
		static_assert(Enable, "enable_fpu() requires a hardware FPU; do not call on cores without one (e.g. Cortex-M3)");
		memory_register<uint32_t>(ScbCpacrAddress).set(ScbCpacrEnableFpu);
		data_barrier();
		instruction_barrier();
	}

	/**
	 * @brief Loads a specific address and set the exclusive monitor
	 * @param ptr The address to load from
	 * @return The loaded address
	 * @remark It is possible only for a size of 1, 2 or 4 bytes
	 */
	template<typename T>
	static inline T load_exclusive(T* ptr)
	{
	    T result;

		if constexpr(sizeof(T) == 1)
		{
			asm volatile("ldrexb %[result], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr));
		}
		else if constexpr(sizeof(T) == 2)
		{
			asm volatile("ldrexh %[result], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr));
		}
		else if constexpr(sizeof(T) == 4)
		{
			asm volatile("ldrex %[result], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr));
		}
		else
		{
	        static_assert("Wrong template parameter size for load_exclusive");
		}

	    return result;
	}

	/**
	 * @brief Tries to store the value to a specific address with excluve monitor check
	 * @param ptr The pointer to store to
	 * @param value The value to store
	 * @return @c 0 if the store is effective, @c 1 otherwise
	 */
	template<typename T>
	static inline uint32_t store_exclusive(T* ptr, T value)
	{
	    uint32_t result;

	    if constexpr(sizeof(T) == 1)
	    {
			asm volatile("strexb %[result], %[value], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr), [value] "r" (value));
			}
	    else if constexpr(sizeof(T) == 2)
	    {
			asm volatile("strexh %[result], %[value], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr), [value] "r" (value));
			}
	    else if constexpr(sizeof(T) == 4)
	    {
			asm volatile("strex %[result], %[value], [%[ptr]]"
			: [result] "=r" (result)
			: [ptr] "r" (ptr), [value] "r" (value));
			}
	    else
	    {
	        static_assert("Wrong template parameter size for store_exclusive");
	    }

	    return result;
	}

	static inline uint32_t cycle_count()
	{
		return memory_register<uint32_t>(CycCntAddress).get();
	}

	static inline void cycle_count(uint32_t value)
	{
		memory_register<uint32_t>(CycCntAddress).set(value);
	}

private:

	static constexpr const uint32_t DwtAddress = 0xE0001000;

	static constexpr uint32_t CycCntAddress = DwtAddress + 0x004;

	static constexpr const uint32_t ScsAddress = 0xE000E000;

	static constexpr const uint32_t ScbAddress = ScsAddress + 0x0D00;
	static constexpr uint32_t NvicAddress = ScsAddress + 0x0100;
	static constexpr uint32_t SystickAddress = ScsAddress + 0x0010;

	static constexpr uint32_t ScbCpuidAddress = ScbAddress;
	static constexpr uint32_t ScbVtorAddress = ScbAddress + 0x08;
	static constexpr uint32_t ScbShpAddress = ScbAddress + 0x014;
	static constexpr uint32_t AircrAddress = ScbAddress + 0x0C;
	static constexpr uint32_t IcsrAddress = ScbAddress + 0x04;
	static constexpr uint32_t ScbCpacrAddress = ScbAddress + 0x88;

	static constexpr uint32_t SystickCtrlAddress = SystickAddress;
	static constexpr uint32_t SystickLoadAddress = SystickAddress + 0x04;
	static constexpr uint32_t SystickValAddress = SystickAddress + 0x08;
	static constexpr uint32_t SystickCalibAddress = SystickAddress + 0x0C;

	static constexpr uint32_t NvicIserAddress = NvicAddress + 0;
	static constexpr uint32_t NvicIcerAddress = NvicAddress + 0x080;
	static constexpr uint32_t NvicIsprAddress = NvicAddress + 0x100;
	static constexpr uint32_t NvicIcprAddress = NvicAddress + 0x180;
	static constexpr uint32_t NvicIabrAddress = NvicAddress + 0x200;
	static constexpr uint32_t NvicIpAddress = NvicAddress + 0x300;
	static constexpr uint32_t NvicIrqRegisterBits = 5;
	static constexpr uint32_t NvicIrqRegisterMask = (1 << NvicIrqRegisterBits) - 1;

	static constexpr std::size_t AircrPrigroupPos = 8;
	static constexpr uint32_t AircrPrigroupMsk = prigroup_max << AircrPrigroupPos;
	static constexpr std::size_t AircrVectkeyPos = 16;
	static constexpr uint32_t AircrVectkeyMsk = 0xFFFFu << AircrVectkeyPos;
	static constexpr uint32_t AircrVectkeyValue = 0x5FA << AircrVectkeyPos;
	static constexpr std::size_t AircrSysresetPos = 2;
	static constexpr std::size_t AircrSysreset = 1 << AircrSysresetPos;

	static constexpr std::size_t IcsrPendSvSetPos = 28;
	static constexpr uint32_t IcsrPendSvSetMsk = 1 << IcsrPendSvSetPos;
	static constexpr uint32_t IcsrPendSvSet = IcsrPendSvSetMsk;

	static constexpr std::size_t IcsrPendSvClrPos = 27;
	static constexpr uint32_t IcsrPendSvClrMsk = 1 << IcsrPendSvClrPos;
	static constexpr uint32_t IcsrPendSvClr = IcsrPendSvClrMsk;

	static constexpr uint32_t ScbCpacrEnableFpu = 0b1111 << 20;

	static constexpr std::size_t SystickReloadBits = 24;
	static constexpr uint32_t SystickReloadMask = (1 << SystickReloadBits) - 1;
	static constexpr std::size_t SystickCtrlClkSourcePos = 2;
	static constexpr uint32_t SystickCtrlClkSource = 1 << SystickCtrlClkSourcePos;
	static constexpr std::size_t SystickCtrlTickIntPos = 1;
	static constexpr uint32_t SystickCtrlTickInt = 1 << SystickCtrlTickIntPos;
	static constexpr std::size_t SystickCtrlEnablePos = 0;
	static constexpr uint32_t SystickCtrlEnable = 1 << SystickCtrlEnablePos;

	static constexpr uint32_t MaxIrq = 239; // Cortex-M can handle up to 240 external IRQs

	static constexpr std::size_t CpuidPartNoPos = 4;
	static constexpr std::size_t CpuidPartNoLength = 12;
	static constexpr std::size_t CpuidPartNoMask = (1 << CpuidPartNoLength) - 1;

	static uint8_t priority_grouping()
	{
		return static_cast<uint8_t>((memory_register<uint32_t>(AircrAddress).get() & AircrPrigroupMsk) >> AircrPrigroupPos);
	}

	static void priority_grouping(uint8_t value)
	{
		assert(value <= prigroup_max);
		memory_register<uint32_t>(AircrAddress).set((memory_register<uint32_t>(AircrAddress).get() & ~(AircrPrigroupMsk | AircrVectkeyMsk)) | (AircrVectkeyValue | static_cast<uint32_t>(value << AircrPrigroupPos)));
	}
};
}
