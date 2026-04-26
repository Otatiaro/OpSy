/**
 ******************************************************************************
 * @file    task.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   The @c task primitive, equivalent of @c std::thread
 *
 * 			This is the base primitive of OpSy, tasks are execution agents.
 * 			They can be preempted by the system, which make sure the most
 * 			important @c task is always given processing resources (CPU time).
 *
 * 			OpSy manipulates @c task_control_block, but the most preferred class
 * 			to create a task is @c task, which already contains a stack space
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

#include <array>
#include <tuple>
#include <atomic>
#include <optional>

#include "config.hpp"
#include "embedded_list.hpp"
#include "cortex_m.hpp"
#include "callback.hpp"
//#include "mutex.hpp"

namespace opsy
{

/**
 * @brief task priority
 * @remark You can create custom ones by casting an uint8_t to this type. Lower values means higher priority.
 */
enum class task_priority
	: uint8_t
	{
		Lowest = 0xFF, ///< Lowest priority
	Low = 0x40,  ///< Low priority
	Normal = 0x80, ///< Normal priority
	High = 0xC0,  ///< High priority
	Highest = 0x0, ///< Highest priority
};

class task_control_block;

namespace task_lists
{
class Timeout: public embedded_node<task_control_block>
{

};

class Handle: public embedded_node<task_control_block>
{

};

class Waiting: public embedded_node<task_control_block>
{

};

}

/**
 * @brief A pointer to executable code
 */
using code_pointer = void(*)(void);


namespace
{

struct stack_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	code_pointer lr;
	code_pointer pc;
	uint32_t psr;
};

struct fp_stack_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	code_pointer lr;
	code_pointer pc;
	uint32_t psr;
	float s0;
	float s1;
	float s2;
	float s3;
	float s4;
	float s5;
	float s6;
	float s7;
	float s8;
	float s9;
	float s10;
	float s11;
	float s12;
	float s13;
	float s14;
	float s15;
	uint32_t fpscr;
	uint32_t pfuit;
};

struct context
{
	uint32_t lr;
	uint32_t control;
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;
};

struct fp_context
{
	float s16;
	float s17;
	float s18;
	float s19;
	float s20;
	float s21;
	float s22;
	float s23;
	float s24;
	float s25;
	float s26;
	float s27;
	float s28;
	float s29;
	float s30;
	float s31;
};

}

class condition_variable;

/**
 * @brief A @c task control block, that contains all the necessary data to manipulate it
 * @remark You should normally not manipulate this type, only create and manipulate @c task which inherit from @c task_control_block
 */
class task_control_block: private task_lists::Timeout, private task_lists::Waiting, private task_lists::Handle
{
	template<typename T, typename I>
	friend class embedded_iterator;
	template<typename T, typename I>
	friend class embedded_const_iterator;
	friend class scheduler;
	friend class hooks;

public:

	/**
	 * @brief The type of a stack item
	 */
	using stack_item = uint32_t;

	/**
	 * @brief Constructs a @c task_control_block giving it a stack memory area through @p stackBase and @p stackSize
	 * @param stackBase The pointer to the base of the stack
	 * @param stackSize The size of the stack, in @c stack_item increment
	 */
	constexpr task_control_block(stack_item* stackBase, std::size_t stackSize) :
			stack_base_(stackBase), stack_size_(stackSize)
	{

	}

	/**
	 * @brief Starts the @c task_control_block and make it call @p entry when it execute
	 * @param entry The @c callback that will be execute by the @c task_control_block
	 * @param name The new name of the @c task_control_block (optional)
	 * @return @c true if the @c task_control_block successfully started, @c false otherwise (already started)
	 * @remark Defined inline at the bottom of @c scheduler.hpp (calls
	 *         @c scheduler::add_task and references @c scheduler::terminate_task;
	 *         see the cycle-breaking note in @c scheduler_inl.hpp).
	 */
	[[nodiscard]] bool start(callback<void(void)> && entry, const char* name = nullptr);

	/**
	 * @brief Stops the @c task_control_block whatever its state
	 * @return @c true is the @c task_control_block has been stopped, @c false otherwise (not started)
	 * @remark Defined inline in @c scheduler_inl.hpp (issues an @c SVC using
	 *         @c scheduler::service_call_number).
	 */
	[[nodiscard]] bool stop();

	/**
	 * @brief Checks if the @c task_control_block is started
	 * @return @c true if the @c task_control_block is started, @c false otherwise
	 */
	bool is_started() const
	{
		return active_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief Gets the current @c task_priority of the @c task_control_block
	 * @return The current @c task_priority of the @c task_control_block
	 */
	constexpr inline task_priority priority() const
	{
		return priority_;
	}

	/**
	 * @brief Dynamically change the @c task_priority of the @c task_control_block
	 * @param newPriority the new @c task_priority
	 * @remark This may trigger a @c task_control_block switch from the system to make sure the most important @c task_control_block is always executed
	 * @remark Defined inline in @c scheduler_inl.hpp (calls @c scheduler::update_priority).
	 */
	void priority(task_priority newPriority);

	/**
	 * @brief Gets the current name of the @c task_control_block
	 * @return The current name of the @c task_control_block
	 */
	constexpr const char* name() const
	{
		return name_;
	}

	/**
	 * @brief Sets the name of the @c task_control_block
	 * @param name The new name of the @c task_control_block
	 */
	constexpr void set_name(const char* name)
	{
		name_ = name;
	}

	/**
	 * @brief Compares priority of two @c task_control_block
	 * @param left The left operand
	 * @param right The right operand
	 * @return @c true if @p left is more important that @p right, @c false otherwise
	 */
	static constexpr bool priority_is_lower(const task_control_block& left, const task_control_block& right)
	{
		if (left.priority() > right.priority())
			return false;
		if (left.priority() < right.priority())
			return true;
		return left.last_started_ < right.last_started_;
	}

private:

	static constexpr uint32_t Dummy = 0xDEADBEEF;

	stack_item* const stack_base_;
	const std::size_t stack_size_;
	std::atomic_bool active_ { false };
	stack_item* stack_pointer_ = nullptr;
	task_priority priority_ = task_priority::Lowest;
	time_point last_started_ = Startup;
	std::optional<time_point> wait_until_;
	const char* name_ = nullptr;
	callback<void(void)> entry_;
	condition_variable* waiting_ = nullptr;
	mutex* mutex_ = nullptr;

	static constexpr uint32_t kFpFlag = 0b10000; // if this bit is NOT set in LR at exception, then the stack frame and saved context both use floating point context

	/**
	 * @brief Trampoline used as the initial PC of every task
	 * @param thisPtr Pointer to the @c task_control_block being started
	 * @remark Defined inline in @c scheduler_inl.hpp because it terminates the
	 *         task via @c scheduler::terminate_task once @c entry_ returns.
	 */
	static void task_starter(task_control_block* thisPtr);

	void set_return_value(uint32_t value)
	{
		auto ptr = stack_pointer_;
		context* ctx = reinterpret_cast<context*>(ptr);

		if ((ctx->lr & kFpFlag) == 0) // there is a floating point context
			ptr += (sizeof(context) + sizeof(fp_context)) / sizeof(stack_item);
		else
			ptr += sizeof(context) / sizeof(stack_item);

		stack_frame* frame = reinterpret_cast<stack_frame*>(ptr); // we are at the top of the frame stacked by cortex on exception entry

		frame->r0 = value;
	}
};

/**
 * @brief Implementation detail: provides initialized stack storage as a base class,
 *        ensuring the stack buffer is constructed before any task_control_block that references it.
 * @tparam N The number of stack items
 */
template<std::size_t N>
struct stack_storage {
	std::array<uint32_t, N> stack_{};
};

/**
 * @brief A concrete implementation of @c task_control_block with a dedicated stack memory
 * @tparam StackSize The stack size in @c stack_item increment
 */
template<std::size_t StackSize>
class task: private stack_storage<StackSize>, public task_control_block
{
public:

	static_assert(StackSize >= 2* (sizeof(stack_frame) + sizeof(context)) / sizeof(task_control_block::stack_item), "Stack too small");

	/**
	 * @brief Constructs a new @c task
	 */
	constexpr task() :
			stack_storage<StackSize>{},
			task_control_block(stack_storage<StackSize>::stack_.data(), StackSize)
	{
	}

};

/**
 * @brief A special @c task_control_block only used for idle tasks, i.e. what the system does when there is no more @c task to run
 */
class idle_task_control_block
{
	friend class scheduler;

public:

	/**
	 * @brief Constructs a @c idle_task_control_block giving it a stack memory area through @p stackBase and @p stackSize
	 * @param stackBase The pointer to the base of the stack
	 * @param stackSize The size of the stack, in @c stack_item increment
	 * @param entry The @c code_pointer to the idle code
	 */
	idle_task_control_block(uint32_t* stackBase, std::size_t stackSize, const code_pointer entry) :
			stack_base_(stackBase), stack_pointer_(&stackBase[stackSize])
	{
		stack_pointer_ -= sizeof(stack_frame) / sizeof(uint32_t);
		const auto frame = reinterpret_cast<stack_frame*>(stack_pointer_);

		frame->psr = 1 << 24;
		frame->pc = entry;
		frame->lr = reinterpret_cast<code_pointer>(reinterpret_cast<uint32_t>(no_return) + 2);

		stack_pointer_ -= sizeof(context) / sizeof(uint32_t);
		const auto ctx = reinterpret_cast<context*>(stack_pointer_);

		ctx->lr = 0xFFFFFFFD;
		ctx->control = 0b10;
	}

private:

	uint32_t* const stack_base_;
	uint32_t* stack_pointer_;

	static void __attribute__((naked)) no_return()
	{
		asm volatile(
				"nop \n\t"
				"bkpt 0");
	}
};

/**
 * @brief A concrete implementation of @c idle_task_control_block with a dedicated stack memory
 * @tparam StackSize The stack size in @c stack_item increment
 */
template<std::size_t StackSize>
class idle_task: private stack_storage<StackSize>, public idle_task_control_block
{

public:

	/**
	 * @brief Constructs a @c idle_task to run code pointed by @p entry
	 * @param entry The code to execute when system is idle
	 */
	constexpr explicit idle_task(const code_pointer entry) :
			stack_storage<StackSize>{},
			idle_task_control_block(stack_storage<StackSize>::stack_.data(), StackSize, entry)
	{
		static_assert(StackSize >= 2* (sizeof(stack_frame) + sizeof(context)) / sizeof(uint32_t), "Stack too small");
	}

};

/**
 * @brief The default implementation for @c idle_task, which runs @c WFI in a loop
 * @tparam StackSize The stack size in @c stack_item increment, 64 is enough for this default implementation
 */
template<std::size_t StackSize = 64>
idle_task<StackSize> DefaultIdle = idle_task<StackSize>([]()
{
	while(true)
	{
#ifndef NDEBUG
		cortex_m::nop();
#else
		cortex_m::wfi();
#endif
	}
});
}
