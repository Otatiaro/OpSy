/**
 ******************************************************************************
 * @file    task.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   The @c Task primitive, equivalent of @c std::thread
 *
 * 			This is the base primitive of OpSy, tasks are execution agents.
 * 			They can be preempted by the system, which make sure the most
 * 			important @c Task is always given processing resources (CPU time).
 *
 * 			OpSy manipulates @c TaskControlBlock, but the most preferred class
 * 			to create a task is @c Task, which already contains a stack space
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
//#include "Mutex.hpp"

namespace opsy
{

/**
 * @brief Task priority
 * @remark You can create custom ones by casting an uint8_t to this type. Lower values means higher priority.
 */
enum class Priority
	: uint8_t
	{
		Lowest = 0xFF, ///< Lowest priority
	Low = 0x40,  ///< Low priority
	Normal = 0x80, ///< Normal priority
	High = 0xC0,  ///< High priority
	Highest = 0x0, ///< Highest priority
};

class TaskControlBlock;

namespace TaskLists
{
class Timeout: public EmbeddedNode<TaskControlBlock>
{

};

class Handle: public EmbeddedNode<TaskControlBlock>
{

};

class Waiting: public EmbeddedNode<TaskControlBlock>
{

};

}

/**
 * @brief A pointer to executable code
 */
using CodePointer = void(*)(void);


namespace
{

struct StackFrame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	CodePointer lr;
	CodePointer pc;
	uint32_t psr;
};

struct FpStackFrame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	CodePointer lr;
	CodePointer pc;
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

struct Context
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

struct FpContext
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

class ConditionVariable;

/**
 * @brief A @c Task control block, that contains all the necessary data to manipulate it
 * @remark You should normally not manipulate this type, only create and manipulate @c Task which inherit from @c TaskControlBlock
 */
class TaskControlBlock: private TaskLists::Timeout, private TaskLists::Waiting, private TaskLists::Handle
{
	template<typename T, typename I>
	friend class EmbeddedIterator;
	template<typename T, typename I>
	friend class EmbeddedConstIterator;
	friend class Scheduler;
	friend class Hooks;

public:

	/**
	 * @brief The type of a stack item
	 */
	using StackItem = uint32_t;

	/**
	 * @brief Constructs a @c TaskControlBlock giving it a stack memory area through @p stackBase and @p stackSize
	 * @param stackBase The pointer to the base of the stack
	 * @param stackSize The size of the stack, in @c StackItem increment
	 */
	constexpr TaskControlBlock(StackItem* stackBase, std::size_t stackSize) :
			stack_base_(stackBase), stack_size_(stackSize)
	{

	}

	/**
	 * @brief Starts the @c TaskControlBlock and make it call @p entry when it execute
	 * @param entry The @c Callback that will be execute by the @c TaskControlBlock
	 * @param name The new name of the @c TaskControlBlock (optional)
	 * @return @c true if the @c TaskControlBlock successfully started, @c false otherwise (already started)
	 * @remark Defined inline at the bottom of @c scheduler.hpp (calls
	 *         @c Scheduler::add_task and references @c Scheduler::terminate_task;
	 *         see the cycle-breaking note in @c scheduler_inl.hpp).
	 */
	[[nodiscard]] bool start(Callback<void(void)> && entry, const char* name = nullptr);

	/**
	 * @brief Stops the @c TaskControlBlock whatever its state
	 * @return @c true is the @c TaskControlBlock has been stopped, @c false otherwise (not started)
	 * @remark Defined inline in @c scheduler_inl.hpp (issues an @c SVC using
	 *         @c Scheduler::ServiceCallNumber).
	 */
	[[nodiscard]] bool stop();

	/**
	 * @brief Checks if the @c TaskControlBlock is started
	 * @return @c true if the @c TaskControlBlock is started, @c false otherwise
	 */
	bool is_started() const
	{
		return active_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief Gets the current @c Priority of the @c TaskControlBlock
	 * @return The current @c Priority of the @c TaskControlBlock
	 */
	constexpr inline Priority priority() const
	{
		return priority_;
	}

	/**
	 * @brief Dynamically change the @c Priority of the @c TaskControlBlock
	 * @param newPriority the new @c Priority
	 * @remark This may trigger a @c TaskControlBlock switch from the system to make sure the most important @c TaskControlBlock is always executed
	 * @remark Defined inline in @c scheduler_inl.hpp (calls @c Scheduler::update_priority).
	 */
	void priority(Priority newPriority);

	/**
	 * @brief Gets the current name of the @c TaskControlBlock
	 * @return The current name of the @c TaskControlBlock
	 */
	constexpr const char* name() const
	{
		return name_;
	}

	/**
	 * @brief Sets the name of the @c TaskControlBlock
	 * @param name The new name of the @c TaskControlBlock
	 */
	constexpr void set_name(const char* name)
	{
		name_ = name;
	}

	/**
	 * @brief Compares priority of two @c TaskControlBlock
	 * @param left The left operand
	 * @param right The right operand
	 * @return @c true if @p left is more important that @p right, @c false otherwise
	 */
	static constexpr bool priority_is_lower(const TaskControlBlock& left, const TaskControlBlock& right)
	{
		if (left.priority() > right.priority())
			return false;
		if (left.priority() < right.priority())
			return true;
		return left.last_started_ < right.last_started_;
	}

private:

	static constexpr uint32_t Dummy = 0xDEADBEEF;

	StackItem* const stack_base_;
	const std::size_t stack_size_;
	std::atomic_bool active_ { false };
	StackItem* stack_pointer_ = nullptr;
	Priority priority_ = Priority::Lowest;
	time_point last_started_ = Startup;
	std::optional<time_point> wait_until_;
	const char* name_ = nullptr;
	Callback<void(void)> entry_;
	ConditionVariable* waiting_ = nullptr;
	Mutex* mutex_ = nullptr;

	static constexpr uint32_t kFpFlag = 0b10000; // if this bit is NOT set in LR at exception, then the stack frame and saved context both use floating point context

	/**
	 * @brief Trampoline used as the initial PC of every task
	 * @param thisPtr Pointer to the @c TaskControlBlock being started
	 * @remark Defined inline in @c scheduler_inl.hpp because it terminates the
	 *         task via @c Scheduler::terminate_task once @c entry_ returns.
	 */
	static void task_starter(TaskControlBlock* thisPtr);

	void set_return_value(uint32_t value)
	{
		auto ptr = stack_pointer_;
		Context* context = reinterpret_cast<Context*>(ptr);

		if ((context->lr & kFpFlag) == 0) // there is a floating point context
			ptr += (sizeof(Context) + sizeof(FpContext)) / sizeof(StackItem);
		else
			ptr += sizeof(Context) / sizeof(StackItem);

		StackFrame* frame = reinterpret_cast<StackFrame*>(ptr); // we are at the top of the frame stacked by cortex on exception entry

		frame->r0 = value;
	}
};

/**
 * @brief Implementation detail: provides initialized stack storage as a base class,
 *        ensuring the stack buffer is constructed before any TaskControlBlock that references it.
 * @tparam N The number of stack items
 */
template<std::size_t N>
struct StackStorage {
	std::array<uint32_t, N> stack_{};
};

/**
 * @brief A concrete implementation of @c TaskControlBlock with a dedicated stack memory
 * @tparam StackSize The stack size in @c StackItem increment
 */
template<std::size_t StackSize>
class Task: private StackStorage<StackSize>, public TaskControlBlock
{
public:

	static_assert(StackSize >= 2* (sizeof(StackFrame) + sizeof(Context)) / sizeof(TaskControlBlock::StackItem), "Stack too small");

	/**
	 * @brief Constructs a new @c Task
	 */
	constexpr Task() :
			StackStorage<StackSize>{},
			TaskControlBlock(StackStorage<StackSize>::stack_.data(), StackSize)
	{
	}

};

/**
 * @brief A special @c TaskControlBlock only used for idle tasks, i.e. what the system does when there is no more @c Task to run
 */
class IdleTaskControlBlock
{
	friend class Scheduler;

public:

	/**
	 * @brief Constructs a @c IdleTaskControlBlock giving it a stack memory area through @p stackBase and @p stackSize
	 * @param stackBase The pointer to the base of the stack
	 * @param stackSize The size of the stack, in @c StackItem increment
	 * @param entry The @c CodePointer to the idle code
	 */
	IdleTaskControlBlock(uint32_t* stackBase, std::size_t stackSize, const CodePointer entry) :
			stack_base_(stackBase), stack_pointer_(&stackBase[stackSize])
	{
		stack_pointer_ -= sizeof(StackFrame) / sizeof(uint32_t);
		const auto frame = reinterpret_cast<StackFrame*>(stack_pointer_);

		frame->psr = 1 << 24;
		frame->pc = entry;
		frame->lr = reinterpret_cast<CodePointer>(reinterpret_cast<uint32_t>(no_return) + 2);

		stack_pointer_ -= sizeof(Context) / sizeof(uint32_t);
		const auto context = reinterpret_cast<Context*>(stack_pointer_);

		context->lr = 0xFFFFFFFD;
		context->control = 0b10;
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
 * @brief A concrete implementation of @c IdleTaskControlBlock with a dedicated stack memory
 * @tparam StackSize The stack size in @c StackItem increment
 */
template<std::size_t StackSize>
class IdleTask: private StackStorage<StackSize>, public IdleTaskControlBlock
{

public:

	/**
	 * @brief Constructs a @c IdleTask to run code pointed by @p entry
	 * @param entry The code to execute when system is idle
	 */
	constexpr explicit IdleTask(const CodePointer entry) :
			StackStorage<StackSize>{},
			IdleTaskControlBlock(StackStorage<StackSize>::stack_.data(), StackSize, entry)
	{
		static_assert(StackSize >= 2* (sizeof(StackFrame) + sizeof(Context)) / sizeof(uint32_t), "Stack too small");
	}

};

/**
 * @brief The default implementation for @c IdleTask, which runs @c WFI in a loop
 * @tparam StackSize The stack size in @c StackItem increment, 64 is enough for this default implementation
 */
template<std::size_t StackSize = 64>
IdleTask<StackSize> DefaultIdle = IdleTask<StackSize>([]()
{
	while(true)
	{
#ifndef NDEBUG
		CortexM::nop();
#else
		CortexM::wfi();
#endif
	}
});
}
