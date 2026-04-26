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
		lowest = 0xFF, ///< Lowest priority
	low = 0x40,  ///< Low priority
	normal = 0x80, ///< Normal priority
	high = 0xC0,  ///< High priority
	highest = 0x0, ///< Highest priority
};

class task_control_block;

namespace task_lists
{
class timeout: public embedded_node<task_control_block>
{

};

class handle: public embedded_node<task_control_block>
{

};

class waiting: public embedded_node<task_control_block>
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
class task_control_block: private task_lists::timeout, private task_lists::waiting, private task_lists::handle
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
	 * @brief Constructs a zero-initialized @c task_control_block
	 * @remark Every member defaults to zero (or @c nullptr / @c task_priority{0}),
	 *         which lets the linker place a globally-declared @c task<StackSize>
	 *         entirely in @c .bss instead of @c .data. The non-zero values
	 *         (@c stack_base_, @c stack_size_, @c priority_ = @c lowest) are
	 *         written by @c start_impl when the task is actually launched.
	 *
	 *         Putting them here (as constexpr defaults) would otherwise force
	 *         the entire @c task<N> object — including its multi-KiB stack
	 *         buffer — into @c .data, costing one byte of flash and one byte of
	 *         RAM-init copy per byte of stack at startup, for no benefit
	 *         (a task that has not been started has no business reading any of
	 *         these fields, and the scheduler only ever touches them via tasks
	 *         it has already accepted via @c start_impl).
	 */
	constexpr task_control_block() = default;

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
	 * @param new_priority the new @c task_priority
	 * @remark This may trigger a @c task_control_block switch from the system to make sure the most important @c task_control_block is always executed
	 * @remark Defined inline in @c scheduler_inl.hpp (calls @c scheduler::update_priority).
	 */
	void priority(task_priority new_priority);

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

protected:

	/**
	 * @brief Configures the storage and priority, then launches the task
	 * @param stack_base Pointer to the base of the task's stack
	 * @param stack_size Size of the stack, in @c stack_item increments
	 * @param entry The @c callback executed by the task
	 * @param name Optional task name
	 * @return @c true if the task has been launched, @c false if already started
	 * @remark Called by @c task<StackSize>::start. Writes @c stack_base_,
	 *         @c stack_size_ and @c priority_ on first launch so that the
	 *         default-constructed @c task_control_block can stay in @c .bss
	 *         until the user actually starts the task.
	 *         Defined inline at the bottom of @c scheduler.hpp (calls
	 *         @c scheduler::add_task and references @c scheduler::terminate_task;
	 *         see the cycle-breaking note in @c scheduler_inl.hpp).
	 */
	[[nodiscard]] bool start_impl(stack_item* stack_base, std::size_t stack_size,
	                              callback<void(void)>&& entry, const char* name);

private:

	static constexpr uint32_t dummy_pattern = 0xDEADBEEF;

	// All defaults below are zero so that a default-constructed task_control_block
	// (and therefore a default-constructed task<N>, including its stack buffer)
	// lands in .bss. See the note on the default constructor for why this matters.
	stack_item* stack_base_ = nullptr;
	std::size_t stack_size_ = 0;
	std::atomic_bool active_ { false };
	stack_item* stack_pointer_ = nullptr;
	task_priority priority_ {};                   // set to task_priority::lowest by start_impl
	time_point last_started_ = startup;           // startup == time_point{0}, BSS-friendly
	std::optional<time_point> wait_until_;
	const char* name_ = nullptr;
	callback<void(void)> entry_;
	condition_variable* waiting_ = nullptr;
	mutex* mutex_ = nullptr;

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

		if ((ctx->lr & cortex_m::exc_return_fp_flag) == 0) // there is a floating point context
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
	 * @brief Constructs a new @c task with all members zero-initialized
	 * @remark Both bases (@c stack_storage<StackSize> and @c task_control_block)
	 *         are now zero-default-constructible, so a globally-declared
	 *         @c task<N> goes to @c .bss. The stack pointer / size / priority
	 *         are written later by @c start (see @c task_control_block::start_impl).
	 */
	constexpr task() = default;

	/**
	 * @brief Starts the task with the given @p entry callback
	 * @param entry The @c callback the task will execute
	 * @param name Optional name for the task
	 * @return @c true if the task was successfully started, @c false if it was already active
	 * @remark Forwards the stack of the embedded @c stack_storage<StackSize> base
	 *         to @c task_control_block::start_impl, which performs the actual
	 *         registration with the scheduler.
	 */
	[[nodiscard]] bool start(callback<void(void)>&& entry, const char* name = nullptr)
	{
		return task_control_block::start_impl(
			stack_storage<StackSize>::stack_.data(), StackSize,
			std::move(entry), name);
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
	 * @brief Constructs a zero-initialized @c idle_task_control_block
	 * @remark Same rationale as @c task_control_block: every member defaults
	 *         to zero so a globally-declared @c idle_task<StackSize> (in
	 *         particular @c default_idle) lands in @c .bss instead of
	 *         @c .data, sparing the multi-hundred-byte stack buffer the
	 *         flash + boot-time @c .data copy. The non-zero values
	 *         (@c stack_base_, @c stack_pointer_ and the initial stack
	 *         frame written into the bottom of the stack) are produced by
	 *         @c prepare_impl when @c scheduler::start launches the system.
	 */
	constexpr idle_task_control_block() = default;

protected:

	/**
	 * @brief Wires the idle stack and writes its initial Cortex-M frame
	 * @param stack_base Base of the idle stack
	 * @param stack_size Size of the idle stack, in @c uint32_t increments
	 * @param entry Function the idle task should run
	 * @remark Called by @c idle_task<StackSize>::prepare on @c scheduler::start.
	 *         Must run before the scheduler kicks the first context switch:
	 *         @c scheduler::start uses @c stack_base_ for @c PSPLIM and
	 *         @c stack_pointer_ for @c PSP.
	 */
	void prepare_impl(uint32_t* stack_base, std::size_t stack_size, code_pointer entry)
	{
		stack_base_ = stack_base;
		stack_pointer_ = &stack_base[stack_size];

		stack_pointer_ -= sizeof(stack_frame) / sizeof(uint32_t);
		const auto frame = reinterpret_cast<stack_frame*>(stack_pointer_);

		frame->psr = 1 << 24;
		frame->pc = entry;
		frame->lr = reinterpret_cast<code_pointer>(reinterpret_cast<uint32_t>(no_return) + 2);

		stack_pointer_ -= sizeof(context) / sizeof(uint32_t);
		const auto ctx = reinterpret_cast<context*>(stack_pointer_);

		ctx->lr = cortex_m::exc_return_thread_psp_basic;
		ctx->control = cortex_m::control_thread_psp_privileged;
	}

private:

	// Both default to nullptr so a default-constructed idle_task_control_block
	// (and any task<N> embedding it) is BSS-eligible. Filled in by prepare_impl.
	uint32_t* stack_base_ = nullptr;
	uint32_t* stack_pointer_ = nullptr;

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

	static_assert(StackSize >= 2* (sizeof(stack_frame) + sizeof(context)) / sizeof(uint32_t), "Stack too small");

	/**
	 * @brief Constructs a new @c idle_task with all members zero-initialized
	 * @remark Same shape as @c task<StackSize>::task: defaulted so that a
	 *         globally-declared @c idle_task<StackSize> (in particular
	 *         @c default_idle) lands in @c .bss. The actual stack and stack
	 *         frame are produced later by @c prepare(entry), which the
	 *         scheduler calls on launch.
	 */
	constexpr idle_task() = default;

	/**
	 * @brief Wires this idle task to the given @p entry function
	 * @param entry The function to run when the system has nothing else to do
	 * @remark Called by @c scheduler::start before the first context switch.
	 *         Forwards the embedded @c stack_storage<StackSize> base to
	 *         @c idle_task_control_block::prepare_impl, which lays down the
	 *         Cortex-M stack frame entry returns to.
	 */
	void prepare(code_pointer entry)
	{
		idle_task_control_block::prepare_impl(
			stack_storage<StackSize>::stack_.data(), StackSize, entry);
	}

};

/**
 * @brief Default idle behaviour: spin on @c WFI (or @c NOP when @c NDEBUG is
 *        not defined, so the debugger can step through reliably).
 * @remark Pulled out of @c default_idle so that the latter can be a plain
 *         zero-initialized variable. @c scheduler::start passes this as the
 *         default @c entry argument.
 */
[[noreturn]] inline void default_idle_loop()
{
	while(true)
	{
#ifndef NDEBUG
		cortex_m::nop();
#else
		cortex_m::wfi();
#endif
	}
}

/**
 * @brief The default @c idle_task instance used by @c scheduler::start
 * @tparam StackSize The stack size in @c stack_item increment, 64 is enough
 * @remark Now zero-initialized so it lives in @c .bss. The entry function is
 *         supplied by @c scheduler::start (default @c default_idle_loop).
 */
template<std::size_t StackSize = 64>
inline idle_task<StackSize> default_idle{};

}
