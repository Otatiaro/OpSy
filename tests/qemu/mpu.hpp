/**
 ******************************************************************************
 * @file    mpu.hpp
 * @brief   Makes a piece of RAM read-only, for an image that needs a store to
 *          go nowhere or a MemManage fault to happen.
 *
 *          The boards have no flash and no unmapped RAM to fault on: their
 *          code memory is writable SRAM. The MPU stands in. One region, the
 *          rest of the map as it was.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <bit>
#include <cstdint>

namespace qemu_test::mpu
{

inline volatile uint32_t& reg(uint32_t address)
{
	return *reinterpret_cast<volatile uint32_t*>(address);
}

constexpr uint32_t shcsr = 0xE000ED24U;
constexpr uint32_t ctrl = 0xE000ED94U;
constexpr uint32_t rnr = 0xE000ED98U;
constexpr uint32_t rbar = 0xE000ED9CU;
constexpr uint32_t rasr_rlar = 0xE000EDA0U;
constexpr uint32_t mair0 = 0xE000EDC0U;

/**
 * @brief Region 0 over @p bytes from @p base: readable by privileged code, nothing else
 *
 *        Enables the MPU with the default map kept outside the region, and
 *        MemManage as its own exception, or the fault escalates to HardFault.
 *
 * @param base The start of the region. PMSAv7 (ARMv7-M) needs it aligned to @p bytes.
 * @param bytes The size. PMSAv7 needs a power of two, from 32 up; PMSAv8 a multiple of 32.
 */
inline void make_read_only(uint32_t base, uint32_t bytes)
{
	reg(rnr) = 0;
#ifdef __ARM_ARCH_8M_MAIN__
	// PMSAv8: attribute 0 is normal memory, non-cacheable. RBAR holds the base,
	// AP = 0b10 (read-only, privileged only), XN; RLAR the inclusive limit,
	// attribute index 0, enabled.
	reg(mair0) = 0x44U;
	reg(rbar) = base | (0b10U << 1) | 1U;
	reg(rasr_rlar) = ((base + bytes - 1) & ~0x1FU) | 1U;
#else
	// PMSAv7: RBAR holds the base (aligned to the size), RASR XN, AP = 0b101
	// (read-only, privileged only), TEX = 0b001 (normal, non-cacheable), the
	// size as log2(bytes) - 1, enabled.
	(void) mair0;
	reg(rbar) = base;
	reg(rasr_rlar) = (1U << 28) | (0b101U << 24) | (0b001U << 19) | ((static_cast<uint32_t>(std::countr_zero(bytes)) - 1U) << 1) | 1U;
#endif

	// PRIVDEFENA: everything outside the region keeps the default map.
	reg(ctrl) = (1U << 2) | 1U;
	reg(shcsr) = reg(shcsr) | (1U << 16); // MEMFAULTENA
	asm volatile("dsb\n\tisb" ::: "memory");
}

} // namespace qemu_test::mpu
