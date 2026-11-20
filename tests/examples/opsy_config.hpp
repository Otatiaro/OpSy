/**
 ******************************************************************************
 * @file    opsy_config.hpp
 * @brief   Example configuration: three preemption bits instead of two.
 *
 *          Level 0 stays above OpSy, for handlers that never call it; levels
 *          2 to 7 are for handlers that do, where the defaults leave two.
 *          Everything else is as in config.hpp.
 *
 *          config.hpp includes this file inside namespace opsy, in place of
 *          its own defaults: every name below is required, and none is
 *          wrapped in a namespace here. The README shows this file as is.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

extern "C"
{
	extern uint32_t SystemCoreClock;
}

/** @brief The system core clock in Hz, which SysTick is programmed from. */
inline uint32_t core_clock()
{
	return SystemCoreClock;
}

/** @brief Priority bits the core implements: the vendor's __NVIC_PRIO_BITS. */
constexpr uint32_t priority_bits = 4;

/** @brief Of which this many decide who preempts whom; the rest is sub-priority. */
constexpr uint32_t preemption_bits = 3;

/** @brief The preemption level OpSy runs at. Handlers call OpSy from the level below, and only from there down. */
constexpr uint32_t opsy_preemption = 1;

/** @brief The time base of timeouts and sleep. */
using duration = std::chrono::duration<int64_t, std::milli>;

/** @brief How far priority inheritance follows a chain of blocked tasks. */
constexpr std::size_t max_inheritance_depth = 8;

/** @brief Where OpSy stops on an internal error it cannot recover from. */
[[noreturn]] inline void trap()
{
	__builtin_trap();
}
