/**
 ******************************************************************************
 * @file    thumb.hpp
 * @brief   The length of the Thumb instruction at an address, for a fault
 *          handler that steps over it.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>

namespace qemu_test
{

/** @brief 2 or 4: a first halfword from 0b11101 up opens a 32-bit instruction. */
inline uint32_t thumb_instruction_length(uint32_t pc)
{
	const auto first = *reinterpret_cast<const volatile uint16_t*>(pc);
	return (first & 0xF800U) >= 0xE800U ? 4U : 2U;
}

} // namespace qemu_test
