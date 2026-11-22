/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   The fault image's hooks: hook::fault, recorded for test_faults.cpp
 *          to compare with what the application's handler is given.
 *
 *          On the include path of opsy_qemu_faults only.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>

#include <cortex_m.hpp>

namespace opsy
{

struct exception_frame;

/** @brief What hook::fault saw last, and how many times it ran */
struct fault_seen
{
	static inline volatile uint32_t count = 0;
	static inline fault_status status{};
	static inline const exception_frame* frame = nullptr;
	static inline uint32_t exc_return = 0;
};

class hooks
{
public:
	static void fault(const fault_status& status, const exception_frame& frame, uint32_t exc_return)
	{
		fault_seen::status = status;
		fault_seen::frame = &frame;
		fault_seen::exc_return = exc_return;
		fault_seen::count = fault_seen::count + 1;
	}
};

} // namespace opsy
