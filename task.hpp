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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "config.hpp"
#include "embedded_list.hpp"
#include "cortex_m.hpp"
#include "callback.hpp"
#include "hooks.hpp"

// Where an idle entry function that returns lands: a breakpoint, defined by
// the asm in scheduler.cpp. Only its address is taken.
extern "C" [[noreturn]] void opsy_idle_returned();
//#include "mutex.hpp"

namespace opsy
{

namespace detail
{
/**
 * @brief What a task's or the idle task's stack is filled with when it starts
 * @remark What is still this value was never written: how much of it is left
 *         is how the most stack ever used is measured.
 */
inline constexpr uint32_t stack_fill = 0xDEADBEEF;
}

/**
 * @brief task priority
 * @remark You can create custom ones by casting an uint8_t to this type. Lower values means higher priority.
 */
enum class task_priority
	: uint8_t
	{
		lowest = 0xFF, ///< Lowest priority
	low = 0xC0,  ///< Low priority
	normal = 0x80, ///< Normal priority
	high = 0x40,  ///< High priority
	highest = 0x0, ///< Highest priority
};

class task_control_block;
class mutex;
class isr_lock;

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
using code_pointer = void(*)();

// The frames the core and the context switch stack, as laid out in memory.
namespace detail
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

/**
 * @brief The alignment of a stack pointer at a public interface (AAPCS)
 */
inline constexpr std::size_t stack_alignment = 8;

/**
 * @brief The Thumb bit of xPSR: the core faults returning through a frame without it
 */
inline constexpr uint32_t xpsr_thumb = 1U << 24;

/**
 * @brief Moves @p stack_pointer down by one @p Frame, and returns that frame
 * @remark A stack grows down: the frame pushed last is at the lowest address.
 */
template<typename Frame>
Frame* push(uint32_t*& stack_pointer)
{
	stack_pointer -= sizeof(Frame) / sizeof(uint32_t); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a stack pointer moves through its stack
	return reinterpret_cast<Frame*>(stack_pointer); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): the frame is laid out on the stack
}

/**
 * @brief Returns the @p Frame at @p stack_pointer , and moves it up past that frame
 */
template<typename Frame>
Frame* pop(uint32_t*& stack_pointer)
{
	auto* const frame = reinterpret_cast<Frame*>(stack_pointer); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): the frame is laid out on the stack
	stack_pointer += sizeof(Frame) / sizeof(uint32_t); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a stack pointer moves through its stack
	return frame;
}

}

class condition_variable;

/**
 * @brief A @c task control block, that contains all the necessary data to manipulate it
 * @remark You should normally not manipulate this type, only create and manipulate @c task which inherit from @c task_control_block
 */
// NOLINTNEXTLINE(misc-multiple-inheritance): each base is the node of one intrusive list
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
	 * @brief Destroys the control block, refusing to do so while it is started
	 *
	 * @warning Destroying a started task leaves the scheduler holding a pointer
	 *          into freed storage — in @c all_tasks_ , and in @c ready_ or
	 *          @c timeouts_ — plus a stack that is about to be reused. Nothing
	 *          detects it; the system faults later, somewhere else.
	 *
	 *          @c ~std::thread calls @c std::terminate when the thread is still
	 *          joinable, precisely so this is loud rather than silent. OpSy has
	 *          no exceptions and no @c std::terminate , so the equivalent here
	 *          is an assert: it costs nothing in a release build, and turns
	 *          @code { opsy::task<1024> t; (void) t.start(body); } @endcode
	 *          — which reads as perfectly ordinary — into an immediate stop
	 *          during development.
	 *
	 *          Stopping the task first is the caller's job, since @ref stop is
	 *          an abrupt termination and cannot be made implicit safely.
	 */
	~task_control_block()
	{
		assert(!is_started());
	}

	task_control_block(const task_control_block&) = delete("a task is linked by address into the scheduler's lists");
	task_control_block(task_control_block&&) = delete("a task is linked by address into the scheduler's lists");
	task_control_block& operator=(const task_control_block&) = delete("a task is linked by address into the scheduler's lists");
	task_control_block& operator=(task_control_block&&) = delete("a task is linked by address into the scheduler's lists");

	/**
	 * @brief Terminates the task immediately, whatever it is doing
	 * @return @c true if the task was active and has been terminated, @c false if it was not started
	 *
	 * @warning Abrupt and asynchronous, which is why it is not called @c stop :
	 *          the task is torn out of the scheduler at an arbitrary
	 *          instruction. Its stack is never unwound, so no destructor of any
	 *          local runs, and any mutex it holds stays held. The standard
	 *          offers no equivalent — @c std::jthread::request_stop is
	 *          cooperative — so reach for @ref request_stop unless the task is
	 *          known to hold nothing.
	 */
	[[nodiscard]] bool kill();

	/**
	 * @brief Asks the task to stop, cooperatively
	 *
	 *        Sets a flag the task observes through @ref stop_requested and acts
	 *        on where it chooses, so it unwinds its own stack and releases what
	 *        it holds. This is @c std::jthread::request_stop , under the same
	 *        name and with the same meaning.
	 *
	 * @warning It does NOT wake a blocked task. One parked in
	 *          @c condition_variable::wait() with no timeout never observes the
	 *          request; write such loops with @c wait_for , or notify the
	 *          condition variable after requesting. There is no
	 *          @c std::stop_callback here.
	 *
	 * @remark The flag is cleared when the task is next started, so a task slot
	 *         can be reused.
	 * @warning The task must be started. On one that is not, the request is
	 *          lost: launching a task clears the flag so a reused task slot
	 *          begins with none pending. Asserted in debug.
	 *
	 * @remark Safe to call from an interrupt handler, including one more
	 *         urgent than OpSy: it writes a relaxed atomic and touches
	 *         nothing else.
	 */
	void request_stop();

	/**
	 * @brief Records the lock a condition variable wait must release
	 * @param lock The lock held by this task, or @c monostate for none
	 *
	 * @remark Lives here rather than on @c condition_variable because it only
	 *         ever writes this task's state. Putting it there meant making
	 *         @c condition_variable a friend of this class for one assignment.
	 */
	void record_released_lock(std::variant<std::monostate, mutex*, isr_lock*> lock)
	{
		released_lock_ = lock;
	}

	/**
	 * @brief Whether this task's last wait on a condition variable ended on its deadline
	 * @remark Read by the task itself once it runs again, after the wait.
	 */
	[[nodiscard]] bool wait_timed_out() const
	{
		return wait_timed_out_;
	}

	/**
	 * @brief Whether a cooperative stop has been requested
	 * @return @c true once @ref request_stop has been called and the task has
	 *         not been restarted since
	 *
	 * @remark Meant to be polled by the task itself:
	 *         @code while (!self.stop_requested()) { ... } @endcode
	 */
	[[nodiscard]] bool stop_requested() const
	{
		return stop_requested_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief Whether this task is sitting in the scheduler's ready list
	 * @return @c true if it is runnable but not the one running
	 *
	 * @remark Derived rather than stored: a started task is in @c ready_
	 *         unless it is the current one, blocked on a mutex, waiting on a
	 *         condition variable, or sleeping on a timeout. Keeping a flag
	 *         would be a second copy of that state.
	 */
	[[nodiscard]] bool is_ready() const;

	/**
	 * @brief Checks if the @c task_control_block is started
	 * @return @c true if the @c task_control_block is started, @c false otherwise
	 */
	[[nodiscard]] bool is_started() const
	{
		return active_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief The size of this task's stack, in bytes
	 */
	[[nodiscard]] std::size_t stack_size() const noexcept
	{
		return stack_size_ * sizeof(stack_item);
	}

	/**
	 * @brief The lowest address of this task's stack
	 *
	 *        The stack grows down towards it from @ref stack_base plus
	 *        @ref stack_size, where it starts. A fault handler reading the
	 *        stack of the task that faulted, from its stack pointer up, reads
	 *        no further than that end.
	 *
	 * @return @c nullptr for a task that was never started
	 */
	[[nodiscard]] const stack_item* stack_base() const noexcept
	{
		return stack_base_;
	}

	/**
	 * @brief The most of its stack this task has used since it was started, in bytes
	 *
	 *        The stack is filled with a pattern when the task starts; this is
	 *        how much of it no longer holds the pattern, so it is the deepest
	 *        the task ever went, not how deep it is now. Compared with
	 *        @ref stack_size , it is what a stack is sized from. A value
	 *        equal to @ref stack_size means the stack was used to its last
	 *        word, or overflowed.
	 *
	 * @remark Reads the whole untouched part of the stack, so it costs a loop
	 *         over it. Called from a task, the scan runs in the scheduler's
	 *         critical section; from an interrupt handler, no task runs during
	 *         it. Either way the task measured cannot be writing its stack
	 *         while it is read.
	 * @return @c 0 for a task that was never started
	 */
	[[nodiscard]] std::size_t stack_high_water() const;

	/**
	 * @brief The priority this task was given
	 * @return The base priority, set through @ref base_priority(task_priority)
	 *
	 * @remark What the task runs at while it owes nothing: a donation through
	 *         a mutex it holds can raise it above this, which
	 *         @ref current_priority reports.
	 */
	[[nodiscard]] constexpr task_priority base_priority() const
	{
		return base_priority_;
	}

	/**
	 * @brief Dynamically change the base @c task_priority of the @c task_control_block
	 * @param new_priority the new @c task_priority
	 * @remark This may trigger a @c task_control_block switch from the system to make sure the most important @c task_control_block is always executed
	 * @remark The current priority follows unless a donation outranks it.
	 *
	 * @warning The task must be started, and this must not be called from an
	 *          interrupt handler more urgent than OpSy. Both are asserted in
	 *          debug; see the definition for what each one prevents.
	 */
	void base_priority(task_priority new_priority);

	/**
	 * @brief The priority this task is scheduled by right now
	 * @return The base priority, or higher while a task waiting on a mutex
	 *         this one holds donates its own
	 *
	 * @remark The scheduler writes it under its lock: read it from a hook,
	 *         which runs under that lock, or before the scheduler starts.
	 *         Read anywhere else, it races with the write.
	 *         @c hook::task_priority_changed is called each time it moves.
	 */
	[[nodiscard]] constexpr task_priority current_priority() const
	{
		return priority_;
	}

	/**
	 * @brief Orders two tasks by which one should run first
	 * @param left The left operand
	 * @param right The right operand
	 * @return @c true if @p left is more important than @p right
	 *
	 * @remark Compares @ref current_priority , the effective priority, and not
	 *         @ref base_priority() , which is the priority the caller asked
	 *         for. The two differ exactly while priority inheritance has
	 *         raised a task that holds a mutex a more important one is waiting
	 *         for -- which is the case this ordering exists to handle.
	 *         Ordering by the requested priority instead leaves the raise with
	 *         no effect whatsoever: the holder is recorded as raised and still
	 *         scheduled behind the middle-priority task it was raised to
	 *         overtake, which is the inversion inheritance is there to close.
	 *
	 * @remark Equal priorities are not ordered: a task is inserted behind
	 *         those it ties with, so tasks of equal priority take turns in
	 *         the order they became ready, the one preempted or woken last
	 *         going last.
	 * @remark A type rather than a function, and two byte compares: that is
	 *         what the compiler generates in the walk of @c insert_when in
	 *         every translation unit alike. Passed as a function reference
	 *         and breaking ties on a 64-bit time stamp, it was a call in some
	 *         units and not in others, and the linker, which keeps one copy
	 *         of the template, gave the image whichever it met first.
	 */
	struct priority_is_lower
	{
		constexpr bool operator()(const task_control_block& left, const task_control_block& right) const
		{
			return left.priority_ < right.priority_;
		}
	};

protected:

	/**
	 * @brief Configures the storage and priority, then launches the task
	 * @param stack_base Pointer to the base of the task's stack
	 * @param stack_size Size of the stack, in @c stack_item increments
	 * @param entry The @c callback executed by the task
	 * @return @c true if the task has been launched, @c false if already started
	 * @remark Called by @c task<StackSize>::start. Writes @c stack_base_,
	 *         @c stack_size_ and @c priority_ on first launch so that the
	 *         default-constructed @c task_control_block can stay in @c .bss
	 *         until the user actually starts the task.
	 */
	[[nodiscard]] bool start_impl(stack_item* stack_base, std::size_t stack_size,
	                              callback<void()>&& entry);

private:

	static constexpr uint32_t dummy_pattern = detail::stack_fill;

	// All defaults below are zero so that a default-constructed task_control_block
	// (and therefore a default-constructed task<N>, including its stack buffer)
	// lands in .bss. See the note on the default constructor for why this matters.
	stack_item* stack_base_ = nullptr;
	std::size_t stack_size_ = 0;
	std::atomic_bool active_ { false };
	std::atomic_bool stop_requested_ { false };   // cooperative stop, see request_stop
	stack_item* stack_pointer_ = nullptr;
	// Two priorities, because they can differ: base_priority_ is what the user
	// asked for, priority_ is what the scheduler actually orders by. They are
	// equal until priority inheritance raises the effective one so a task
	// holding a mutex cannot be starved by a middle-priority task while a
	// higher-priority one waits behind it.
	task_priority base_priority_ {};              // set to task_priority::lowest by start_impl
	task_priority priority_ {};                   // effective; base_priority_ unless inherited
	std::optional<time_point> wait_until_;
	callback<void()> entry_;
	condition_variable* waiting_ = nullptr;

	// The mutex this task is blocked on, or nullptr: the other end of its
	// place in that mutex's waiters_, which is where the task is queued
	// then, on the task_lists::waiting node that ready_ and a condition
	// variable's waiting_list_ use otherwise. The two ends change together,
	// in scheduler::block_on, hand_over, requeue and terminate_task only.
	mutex* blocked_on_ = nullptr;
	// The lock a condition variable wait released on this task's behalf, to be
	// re-acquired on wake. A variant rather than two pointers or a tagged one:
	// the two kinds are re-acquired by completely different means — an
	// isr_lock restores BASEPRI during the context switch, a mutex takes
	// ownership back when the task is woken — and the type system should be
	// the thing that keeps them apart.
	std::variant<std::monostate, mutex*, isr_lock*> released_lock_;
	// How the last condition variable wait ended, written by whoever ends it
	// (a notification or SysTick) and read by the task when it runs again. In
	// the task rather than in the frame stacked on its service call: a task
	// that blocks by masking and pending PendSV has no such frame.
	bool wait_timed_out_ = false;

	/**
	 * @brief Trampoline used as the initial PC of every task
	 * @param thisPtr Pointer to the @c task_control_block being started
	 */
	static void task_starter(task_control_block* thisPtr);
};

/**
 * @brief Implementation detail: provides initialized stack storage as a base class,
 *        ensuring the stack buffer is constructed before any task_control_block that references it.
 * @tparam N The number of stack items
 */
template<std::size_t N>
struct stack_storage {
	// AAPCS requires SP to be 8-byte aligned at a public interface, and a task
	// starts executing on this buffer. std::array<uint32_t, N> only carries
	// alignof 4, which would leave the alignment of a task's stack down to
	// where the linker happened to place the object.
	alignas(detail::stack_alignment) std::array<uint32_t, N> stack_{};
};

/**
 * @brief A concrete implementation of @c task_control_block with a dedicated stack memory
 * @tparam StackSize The stack size in @c stack_item increment
 */
template<std::size_t StackSize>
class task: private stack_storage<StackSize>, public task_control_block
{
public:

	static_assert(StackSize >= 2* (sizeof(detail::stack_frame) + sizeof(detail::context)) / sizeof(task_control_block::stack_item), "Stack too small");

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
	 * @return @c true if the task was successfully started, @c false if it was already active
	 * @remark Forwards the stack of the embedded @c stack_storage<StackSize> base
	 *         to @c task_control_block::start_impl, which performs the actual
	 *         registration with the scheduler.
	 */
	[[nodiscard]] bool start(callback<void()>&& entry)
	{
		return task_control_block::start_impl(
			stack_storage<StackSize>::stack_.data(), StackSize,
			std::move(entry));
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

	/**
	 * @brief The size of the idle task's stack, in bytes
	 */
	[[nodiscard]] std::size_t stack_size() const noexcept
	{
		return stack_size_ * sizeof(uint32_t);
	}

	/**
	 * @brief The most of its stack the idle task has used, in bytes
	 * @remark As @ref task_control_block::stack_high_water : what the idle
	 *         loop and the hooks it calls reached, to size it from.
	 */
	[[nodiscard]] std::size_t stack_high_water() const;

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
		stack_size_ = stack_size;
		const std::span stack(stack_base, stack_size);
		for (auto& word : stack)
			word = detail::stack_fill;   // what stack_high_water measures against
		stack_pointer_ = std::to_address(stack.end());

		auto* const frame = detail::push<detail::stack_frame>(stack_pointer_);
		frame->psr = detail::xpsr_thumb;
		frame->pc = entry;
		frame->lr = &opsy_idle_returned;

		auto* const ctx = detail::push<detail::context>(stack_pointer_);
		ctx->lr = cortex_m::exc_return_thread_psp_basic;
		ctx->control = cortex_m::control_thread_psp_privileged;
	}

private:

	// Both default to nullptr so a default-constructed idle_task_control_block
	// (and any task<N> embedding it) is BSS-eligible. Filled in by prepare_impl.
	uint32_t* stack_base_ = nullptr;
	uint32_t* stack_pointer_ = nullptr;
	std::size_t stack_size_ = 0;
};

/**
 * @brief A concrete implementation of @c idle_task_control_block with a dedicated stack memory
 * @tparam StackSize The stack size in @c stack_item increment
 */
template<std::size_t StackSize>
class idle_task: private stack_storage<StackSize>, public idle_task_control_block
{

public:

	static_assert(StackSize >= 2* (sizeof(detail::stack_frame) + sizeof(detail::context)) / sizeof(uint32_t), "Stack too small");

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
 * @brief The stack of the default idle task, in @c stack_item increments: enough for @c default_idle_loop
 */
inline constexpr std::size_t default_idle_stack_size = 64;

/**
 * @brief Default idle behaviour: wait for an interrupt, through @c hook::idle_wait
 * @remark Pulled out of @c default_idle so that the latter can be a plain
 *         zero-initialized variable. @c scheduler::start passes this as the
 *         default @c entry argument.
 */
[[noreturn]] inline void default_idle_loop()
{
	while(true)
		hook::idle_wait();
}

/**
 * @brief The default @c idle_task instance used by @c scheduler::start
 * @tparam StackSize The stack size in @c stack_item increment
 * @remark Now zero-initialized so it lives in @c .bss. The entry function is
 *         supplied by @c scheduler::start (default @c default_idle_loop).
 */
template<std::size_t StackSize = default_idle_stack_size>
constinit inline idle_task<StackSize> default_idle{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the idle task's stack, written as it runs

inline void task_control_block::request_stop()
{
	// Requesting a stop on a task that has not been started is lost: start
	// clears the flag so that a reused task slot begins with none pending.
	// The task would then run to completion having never seen the request.
	assert(is_started());

	// No context assert here, deliberately: the flag is a relaxed atomic and
	// nothing else is touched, so this is safe to call from any interrupt
	// handler, including one more urgent than OpSy. Asking a task to stop is
	// exactly the kind of thing a handler has reason to do.
	stop_requested_.store(true, std::memory_order_relaxed);
}

}
