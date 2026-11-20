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
	return (first & 0xF800u) >= 0xE800u ? 4u : 2u;
}

} // namespace qemu_test
