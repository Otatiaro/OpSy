/**
 ******************************************************************************
 * @file    CortexM.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Cortex-M (3-4-7) low level features and system peripherals access
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

#include "Config.hpp"
#include "IsrPriority.hpp"

namespace
{

template<typename Type>
class MemoryRegister
{
public:

	explicit constexpr MemoryRegister(uint32_t address) :
			m_address(address)
	{

	}

	constexpr uint32_t address() const
	{
		return m_address;
	}

	Type inline get() const
	{
		return *reinterpret_cast<Type*>(m_address);
	}

	inline void set(Type value) const
	{
		*reinterpret_cast<Type*>(m_address) = value;
	}

private:
	const uint32_t m_address;
	MemoryRegister& operator=(const MemoryRegister& other) = delete;

};
}

namespace opsy
{

/**
 * The maximum value of the @c PRIGROUP register
 * @warning It is the maximum value, not the total number of values, with is this value + 1 (ending up at 8)
 */
static constexpr std::size_t kPrigroupMax = 7;

/**
 * @brief Contains all addresses and static methods to access Cortex-M low level features and system peripherals (NVIC, Systick, etc.)
 */
class CortexM
{
public:

	/**
	 * @brief An Interrupt Service Routine (ISR) entry point definition, ISR are static or global methods with no return type and no arguments
	 */
	using IsrHandler = void(*)(void);

	/**
	 * @brief Number of system interrupt requests in the Cortex-M
	 */
	static constexpr uint32_t kSystemIrqs = 16;

	/**
	 * @brief Alignment of the SCB VTOR register. This alignment is MANDATORY to declare a RAM interrupt vector.
	 */
	static constexpr uint32_t kVtorAlignment = 0x200;

	/**
	 * @brief System interrupt requests
	 */
	enum class SystemIrq
		: uint32_t
		{
			InitialSp = 0, ///< Value set to MSP at startup (it is not an IsrHandler but a memory pointer)
		Reset = 1, ///< First code to be executed at system reset
		NonMaskableInterrupt = 2, ///< Non maskable interrupt
		HardFault = 3, ///< Hard Fault
		ServiceCall = 11, ///< Service Call, used by OpSy for precise calls and automatic interrupt masking
		PendSV = 14, ///< PendSV, used by OpSy for task switch
		Systick = 15, ///< Systick, used by OpSy as main clock source
	};

	/**
	 * @brief Cortex-M types
	 * @remark There are many more but only Cortex-M4 and Cortex-M7 are officially supported
	 */
	enum class Type
		: uint16_t
		{
			M4 = 0xc24, ///< Cortex-M4
		M7 = 0xc27, ///< Cortex-M7
	};

	/**
	 * @brief Lowest priority available in the system
	 * @remark Still an ISR priority, a task runs at no priority, so an ISR with this priority still preempts tasks
	 */
	static constexpr IsrPriority kLowestPriority = IsrPriority(0xFF);

	/**
	 * @brief Highest priority available in the system
	 */
	static constexpr IsrPriority kHighestPriority = IsrPriority(0);

	/**
	 * @brief Gets the current executing Cortex-M type
	 * @return The Cortex-M type (only M4 and M7 implemented in the enumeration)
	 */
	static Type getType()
	{
		const auto cpuid = MemoryRegister<uint32_t>(ScbCpuidAddress).get();
		return static_cast<Type>((cpuid >> CpuidPartNoPos) & CpuidPartNoMask);
	}

	/**
	 * @brief Gets the current number of preemption bits
	 * @return The current number of preemption bits
	 */
	static uint8_t preemptBits()
	{
		return static_cast<uint8_t>(kPrigroupMax + 1u - priorityGrouping());
	}

	/**
	 * @brief Sets the number of preemption bits
	 * @param value The required number of preemption bits
	 * @warning The @p value should be between @c 0 and the number of priority bits actually implemented in the Cortex
	 */
	static void preemptBits(uint8_t value)
	{
		priorityGrouping(static_cast<uint8_t>(kPrigroupMax + 1u - value));
	}

	/**
	 * @brief Enables the System Tick (Systick) counter with the specified reload counter
	 * @param reload The reload counter, counter is decremented each clock cycle and a Systick interrupt is generated when it reaches @c 0, then reloaded with this value
	 */
	static void enableSystick(uint32_t reload)
	{
		assert(reload != 0);
		assert(reload - 1 <= SystickReloadMask);
		MemoryRegister<uint32_t>(SystickCtrlAddress).set(0); // stop timer in case it was already started
		MemoryRegister<uint32_t>(SystickLoadAddress).set(reload - 1); // set the reload value
		MemoryRegister<uint32_t>(SystickValAddress).set(0); // reset the counter
		MemoryRegister<uint32_t>(SystickCtrlAddress).set(SystickCtrlClkSource | SystickCtrlTickInt | SystickCtrlEnable); // and start the timer
	}

	/**
	 * @brief Gets the current Systick counter value
	 * @return The current Systick value
	 * @remark This value is actually going up, and is the difference between the reload value and the current value of the timer (which decrements)
	 */
	static uint32_t systickCount()
	{
		return MemoryRegister<uint32_t>(SystickLoadAddress).get() - MemoryRegister<uint32_t>(SystickValAddress).get();
	}

	/**
	 * @brief Enables a peripheral interrupt
	 * @param irq The interrupt request to enable
	 * @warning Make sure the handler and priority are set before you enable an interrupt
	 */
	static void enableInterrupt(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		MemoryRegister<uint32_t>(NvicIserAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Disables a peripheral interrupt
	 * @param irq The interrupt request to disable
	 */
	static void disableInterrupt(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		MemoryRegister<uint32_t>(NvicIcerAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Checks if a peripheral interrupt is pending
	 * @param irq The interrupt request to check
	 * @return @c true if the interrupt is pending, @c false otherwise
	 */
	static bool isPending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return (MemoryRegister<uint32_t>(NvicIsprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).get() & (1u << (irq & NvicIrqRegisterMask))) != 0;
	}

	/**
	 * @brief Sets the pending status for a peripheral interrupt
	 * @param irq The interrupt request to set pending flag for
	 */
	static void setPending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		MemoryRegister<uint32_t>(NvicIsprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Clears the pending flag for a peripheral interrupt
	 * @param irq The interrupt request to clear pending flag for
	 */
	static void clearPending(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		MemoryRegister<uint32_t>(NvicIcprAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).set(1u << (irq & NvicIrqRegisterMask));
	}

	/**
	 * @brief Triggers an instruction synchronization barrier
	 */
	static inline void instructionBarrier()
	{
		asm volatile("isb");
	}

	/**
	 * @brief  Triggers a data synchronization barrier
	 */
	static inline void dataBarrier()
	{
		asm volatile("dsb");
	}

	/**
	 * @brief Checks if a peripheral interrupt is active
	 * @param irq The interrupt request to check
	 * @return @c true if the interrupt is active, @c false otherwise
	 */
	static bool isActive(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return (MemoryRegister<uint32_t>(NvicIabrAddress + sizeof(uint32_t) * (irq >> NvicIrqRegisterBits)).get() & (1u << (irq & NvicIrqRegisterMask))) != 0;
	}

	/**
	 * @brief Sets the priority for a peripheral interrupt
	 * @param irq The interrupt request to set priority for
	 * @param priority The priority
	 */
	static void setPriority(uint32_t irq, IsrPriority priority)
	{
		assert(irq <= MaxIrq);
		MemoryRegister<uint8_t>(NvicIpAddress + irq).set(priority.value());
	}

	/**
	 * @brief Gets the current priority for a peripheral interrupt
	 * @param irq The interrupt request to get priority for
	 * @return The interrupt priority
	 */
	static IsrPriority getPriority(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return IsrPriority(MemoryRegister<uint8_t>(NvicIpAddress + irq).get());
	}

	/**
	 * @brief Sets the priority for a system interrupt
	 * @param irq The interrupt request to set priority for
	 * @param priority The priority
	 * @warning Only @c SystemIrq::NonMaskableInterrupt, @c SystemIrq::HardFault, @c SystemIrq::ServiceCall, @c SystemIrq::PendSV and @c SystemIrq::Systick are configurable
	 */
	static void setPriority(SystemIrq irq, IsrPriority priority)
	{
		switch (irq)
		{
		case SystemIrq::NonMaskableInterrupt:
		case SystemIrq::HardFault:
		case SystemIrq::ServiceCall:
		case SystemIrq::PendSV:
		case SystemIrq::Systick:
			MemoryRegister<uint8_t>(ScbShpAddress + static_cast<uint32_t>(irq)).set(priority.value());
			break;
		case SystemIrq::InitialSp:
		case SystemIrq::Reset:
		default:
			assert(false);
			break;
		}
	}

	/**
	 * @brief Gets the current priority for a system interrupt
	 * @param irq The interrupt request to get priority for
	 * @return The current priority
	 * @warning Only @c SystemIrq::NonMaskableInterrupt, @c SystemIrq::HardFault, @c SystemIrq::ServiceCall, @c SystemIrq::PendSV and @c SystemIrq::Systick are configurable
	 */
	static IsrPriority getPriority(SystemIrq irq)
	{
		switch (irq)
		{
		case SystemIrq::NonMaskableInterrupt:
		case SystemIrq::HardFault:
		case SystemIrq::ServiceCall:
		case SystemIrq::PendSV:
		case SystemIrq::Systick:
			return IsrPriority(MemoryRegister<uint8_t>(ScbShpAddress + static_cast<uint32_t>(irq)).get());
			break;
		case SystemIrq::InitialSp:
		case SystemIrq::Reset:
		default:
			assert(false);
			return IsrPriority(0);
		}
	}

	/**
	 * @brief Minimum (lowest) preemption priority available
	 * @tparam PreemptBits Number of preemption bits
	 * @return The minimum (lowest) preemption priority available
	 */
	template<std::size_t PreemptBits = kPreemptionBits>
	static constexpr uint8_t minSub()
	{
		return (1 << (kPrigroupMax + 1 - PreemptBits)) - 1;
	}

	/**
	 * @brief Minimum (lowest) sub-priority available
	 * @tparam PreemptBits Number of preemption bits
	 * @return The minimum (lowest) sub-priority available
	 */
	template<std::size_t PreemptBits = kPreemptionBits>
	static constexpr uint8_t minPreempt()
	{
		return (1 << PreemptBits) - 1;
	}

	/**
	 * @brief Generates a system reset
	 * @remark This call only sets the required bit, but it may take a couple more cycles before the reset is effective
	 */
	static void __attribute__((noreturn)) reset()
	{
		MemoryRegister<uint32_t>(AircrAddress).set(AircrVectkeyValue | AircrSysreset);
	}

	/**
	 * @brief Gets the current interrupt handler vector
	 * @return The current interrupt handler vector
	 */
	static IsrHandler* getVtor()
	{
		return reinterpret_cast<IsrHandler*>(MemoryRegister<uint32_t>(ScbVtorAddress).get());
	}

	/**
	 * @brief Moves the interrupt handler vector to a new location
	 * @param vtor The new interrupt handler vector
	 * @param copySize Number of handlers to copy from the previous to the new one
	 * @remark It is most preferable to move interrupt handler vector at system startup before any interrupt is active
	 */
	static void moveVtor(IsrHandler* vtor, std::size_t copySize = 0)
	{
		assert((vtor != nullptr) && (reinterpret_cast<uint32_t>(vtor) % kVtorAlignment == 0)); // check vtor is not null and correctly aligned
		assert(copySize <= MaxIrq + kSystemIrqs);

		for (auto i = 0u; i < copySize; ++i)
			vtor[i] = getVtor()[i];

		MemoryRegister<uint32_t>(ScbVtorAddress).set(reinterpret_cast<uint32_t>(vtor));
	}

	/**
	 * @brief Gets the current interrupt handler for a system interrupt
	 * @param irq The interrupt request to get the handler for
	 * @return The current handler of the specified system interrupt
	 */
	static IsrHandler getIsrHandler(SystemIrq irq)
	{
		switch (irq)
		{
		case SystemIrq::NonMaskableInterrupt:
		case SystemIrq::HardFault:
		case SystemIrq::ServiceCall:
		case SystemIrq::PendSV:
		case SystemIrq::Systick:
		case SystemIrq::Reset:
		{
			return getVtor()[static_cast<std::size_t>(irq)];
		}
		case SystemIrq::InitialSp:
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
	static IsrHandler getIsrHandler(uint32_t irq)
	{
		assert(irq <= MaxIrq);
		return getVtor()[irq + kSystemIrqs];
	}

	/**
	 * @brief Gets the value set to main stack pointer (MSP) at system reset (or startup)
	 * @return The value set to main stack pointer (MSP) at system reset (or startup)
	 */
	static uint32_t* mspAtReset()
	{
		return reinterpret_cast<uint32_t*>(*getVtor());
	}

	/**
	 * Sets the interrupt service routine for the specified system interrupt
	 * @param irq The interrupt request to set the handler for
	 * @param handler The interrupt service routine handler
	 * @remark This call first checks if the current ISR is already correctly set, and tries to set it otherwise
	 * @warning If you use this method to change the current value, make sure the interrupt handler vector is in writable memory (i.e. not FLASH)
	 */
	static void setIsrHandler(SystemIrq irq, IsrHandler handler)
	{
		if (getIsrHandler(irq) != handler)
			getVtor()[static_cast<std::size_t>(irq)] = handler;
	}

	/**
	 * @brief Sets the interrupt service routine for the specified peripheral interrupt
	 * @param irq The interrupt request to set the handler for
	 * @param handler The interrupt service routine handler
	 * @remark This call first checks if the current ISR is already correctly set, and tries to set it otherwise
	 * @warning If you use this method to change the current value, make sure the interrupt handler vector is in writable memory (i.e. not FLASH)
	 */
	static void setIsrHandler(uint32_t irq, IsrHandler handler)
	{
		assert(irq <= MaxIrq);
		if (getIsrHandler(irq) != handler)
			getVtor()[irq + kSystemIrqs] = handler;
	}

	/**
	 * @brief Tries to get the priority of the currently executing interrupt service routine
	 * @return @c nullopt if no ISR currently executing, the @c IsrPriority of the current ISR otherwise
	 */
	static std::optional<IsrPriority> currentPriority()
	{
		auto ipsrValue = ipsr();

		if (ipsrValue == 0)
			return std::nullopt;

		if (ipsrValue < kSystemIrqs)
			return getPriority(static_cast<SystemIrq>(ipsrValue));
		else
			return getPriority(ipsrValue - kSystemIrqs);
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
	static uint32_t* getMsp() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, msp" : "=r" (result));
		return reinterpret_cast<uint32_t*>(result);
	}

	/**
	 * @brief Sets the @c MSP (Main Stack Pointer) value
	 * @param msp The value to set @c MSP to
	 */
	static void setMsp(uint32_t* msp) __attribute__((always_inline))
	{
		asm volatile("msr msp, %0" : : "r"(msp) : "sp");
	}

	/**
	 * @brief Gets the current @c PSP (Process Stack Pointer) value
	 * @return The current @c PSP value
	 */
	static uint32_t* getPsp() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, psp" : "=r" (result));
		return reinterpret_cast<uint32_t*>(result);
	}

	/**
	 * @brief Sets the @c PSP (Process Stack Pointer) value
	 * @param psp The value to set @c PSP to
	 */
	static void setPsp(uint32_t* psp) __attribute__((always_inline))
	{
		asm volatile("msr psp, %0" : : "r"(psp) : "sp");
	}

	/**
	 * @brief Sets the @c CONTROL register value
	 * @param control The value to set @c CONTROL register to
	 */
	static void setControl(uint32_t control) __attribute__((always_inline))
	{
		asm volatile("msr control, %0" : : "r" (control) : "memory");
	}

	/**
	 * @brief Gets the current @c CONTROL register value
	 * @return The current @c CONTROL register value
	 */
	static uint32_t getControl() __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile("mrs %0, control" : "=r" (result));
		return result;
	}

	/**
	 * @brief Triggers the pending state for system interrupt @c PendSV
	 */
	static inline void triggerPendSv()
	{
		MemoryRegister<uint32_t>(IcsrAddress).set(IcsrPendSvSet);
	}

	/**
	 * @brief Clears the pending state for system interrupt @c PendSV
	 */
	static inline void clearPendSv()
	{
		MemoryRegister<uint32_t>(IcsrAddress).set(IcsrPendSvClr);
	}

	/**
	 * @brief Sets the @c BASEPRI register value, and gets back its previous value
	 * @param priority The new @c IsrPriority to set @c BASEPRI register to
	 * @return The previous value of @c BASEPRI register
	 */
	static inline IsrPriority setBasepri(IsrPriority priority = IsrPriority(0)) __attribute__((always_inline))
	{
		uint32_t result;
		asm volatile(
				"mrs %[output], basepri \n\t" // get previous value of basepri
				"msr basepri, %[input] \n\t"// set the new value
				"isb"// make sure it is into effect before returning
				: [output] "=&r" (result)
				: [input] "r" (priority.value())
				: );
		return IsrPriority(static_cast<uint8_t>(result));
	}

	/**
	 * @brief Disables all interrupts by setting @c PRIMASK register to 1
	 */
	static inline void disableInterrupts() __attribute__((always_inline))
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
	static inline void enableInterrupts() __attribute__((always_inline))
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
	static inline bool isPrimask() __attribute__((always_inline))
	{
		uint32_t value;
		asm volatile("mrs %[output], primask" : [output] "=r" (value));
		return value != 0;
	}

	/**
	 * @brief Enabled the Floating Point Unit (FPU) on devices which have the FPU coprocessor
	 */
	static inline void enableFpu() __attribute__((always_inline))
	{
		MemoryRegister<uint32_t>(ScbCpacrAddress).set(ScbCpacrEnableFpu);
		dataBarrier();
		instructionBarrier();
	}

	/**
	 * @brief Loads a specific address and set the exclusive monitor
	 * @param ptr The address to load from
	 * @return The loaded address
	 * @remark It is possible only for a size of 1, 2 or 4 bytes
	 */
	template<typename T>
	static inline T loadExclusive(T* ptr)
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
	        static_assert("Wrong template parameter size for loadExclusive");
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
	static inline uint32_t storeExclusive(T* ptr, T value)
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
	        static_assert("Wrong template parameter size for storeExclusive");
	    }

	    return result;
	}

	static inline uint32_t cycleCount()
	{
		return MemoryRegister<uint32_t>(CycCntAddress).get();
	}

	static inline void cycleCount(uint32_t value)
	{
		MemoryRegister<uint32_t>(CycCntAddress).set(value);
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
	static constexpr uint32_t AircrPrigroupMsk = kPrigroupMax << AircrPrigroupPos;
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

	static uint8_t priorityGrouping()
	{
		return static_cast<uint8_t>((MemoryRegister<uint32_t>(AircrAddress).get() & AircrPrigroupMsk) >> AircrPrigroupPos);
	}

	static void priorityGrouping(uint8_t value)
	{
		assert(value <= kPrigroupMax);
		MemoryRegister<uint32_t>(AircrAddress).set((MemoryRegister<uint32_t>(AircrAddress).get() & ~(AircrPrigroupMsk | AircrVectkeyMsk)) | (AircrVectkeyValue | static_cast<uint32_t>(value << AircrPrigroupPos)));
	}
};
}
