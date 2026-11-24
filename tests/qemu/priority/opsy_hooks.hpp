/**
 ******************************************************************************
 * @file    opsy_hooks.hpp
 * @brief   The priority image's hooks: every hook::task_priority_changed,
 *          recorded in order for test_priority_hook.cpp to compare with the
 *          effective priorities the scheduler went through.
 *
 *          On the include path of opsy_qemu_priority only.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace opsy
{

enum class task_priority : std::uint8_t;

/** @brief The priority changes reported so far, oldest first */
struct priority_seen
{
	struct change
	{
		const task_control_block* task;
		task_priority priority;
	};

	static constexpr std::size_t capacity = 32;

	static inline std::array<change, capacity> changes{};

	/** @brief How many were reported, which may exceed @c capacity : the rest are lost */
	static inline std::atomic<std::size_t> count = 0;
};

class hooks
{
public:
	// Called under the scheduler's lock, so one report at a time. auto&: the
	// task's class is not complete where this header is read.
	static void task_priority_changed(auto& task)
	{
		const auto index = priority_seen::count.load(std::memory_order_relaxed);
		if (index < priority_seen::capacity)
			priority_seen::changes[index] = {&task, task.current_priority()};
		priority_seen::count.store(index + 1, std::memory_order_relaxed);
	}
};

} // namespace opsy
