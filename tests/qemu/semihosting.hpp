/**
 ******************************************************************************
 * @file    semihosting.hpp
 * @brief   Minimal ARM semihosting: the only I/O the QEMU images have.
 *
 *          The boards are run with @c -semihosting-config @c enable=on , so a
 *          @c BKPT @c 0xAB traps to QEMU, which services the request on the
 *          host. That is how a test image prints and how it reports its
 *          verdict, with no UART to drive and no C library to link.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <array>
#include <charconv>
#include <cstdint>

namespace qemu
{

/** @brief Issues one semihosting call. */
inline int semihosting_call(int operation, const void* argument)
{
	register int r0 asm("r0") = operation; // NOLINT(misc-const-correctness): the asm statement below writes it
	register const void* r1 asm("r1") = argument;
	asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
	return r0;
}

/** @brief SYS_WRITE0: writes a NUL-terminated string to the host's stdout. */
inline void write(const char* text)
{
	semihosting_call(0x04, text);
}

/** @brief Writes an unsigned value in decimal. */
inline void write_unsigned(uint32_t value)
{
	std::array<char, 11> digits{}; // 4294967295, then the NUL
	std::to_chars(digits.data(), digits.data() + digits.size() - 1, value);
	write(digits.data());
}

/**
 * @brief Stops the machine with the given exit code.
 *
 * @remark On AArch32, plain SYS_EXIT (0x18) takes the reason code *in r1*,
 *         not a pointer — passing the {reason, code} block there makes QEMU
 *         read the pointer itself as the reason, decide the run ended
 *         abnormally, and exit 1 even on success. The block form is
 *         SYS_EXIT_EXTENDED (0x20), which is what carries an exit code on
 *         32-bit targets.
 *
 * @warning QEMU collapses every non-zero application exit code to a process
 *          status of 1, so the status only separates pass from fail. Anything
 *          finer has to be printed.
 */
[[noreturn]] inline void exit(int code)
{
	constexpr uint32_t application_exit = 0x20026U;   // ADP_Stopped_ApplicationExit
	constexpr uint32_t run_time_error   = 0x20023U;   // ADP_Stopped_RunTimeError

	const std::array<uint32_t, 2> parameters = { application_exit, static_cast<uint32_t>(code) };
	semihosting_call(0x20, parameters.data());        // SYS_EXIT_EXTENDED

	// Only reached if the host does not implement the extended call: fall back
	// to the plain form, where the reason alone has to carry pass or fail.
	semihosting_call(0x18, reinterpret_cast<const void*>(
		static_cast<uintptr_t>(code == 0 ? application_exit : run_time_error)));

	for (;;) {} // not reached; keeps the [[noreturn]] contract if semihosting is off
}

} // namespace qemu
