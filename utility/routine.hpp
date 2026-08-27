/**
 ******************************************************************************
 * @file    routine.hpp
 * @brief   Interrupt state machines written as straight-line code.
 *
 * @copyright Copyright (c) 2026 Thomas Legrand
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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

#include <coroutine>
#include <cstddef>
#include <utility>
#include <cstring>

#include "../opsy_assert.hpp"

namespace opsy::utility
{

/**
 * @brief Declared and never defined, so that calling it fails the build
 *
 *        A routine's frame size is worked out by the compiler and is not
 *        available to the program: there is no standard way to ask for it, no
 *        constant to put in a @c static_assert . So the check is made where
 *        the size *is* known -- inside the allocation, after the compiler has
 *        substituted it as a constant -- and reported by calling a function
 *        that cannot be called.
 *
 *        The name is what the user reads in the error, so it says the whole
 *        problem.
 *
 * @remark Only in optimised builds. Without @c -O the allocation is a real
 *         call, its size argument is not a constant there, and the comparison
 *         cannot be folded away -- the build would fail whatever the size. An
 *         unoptimised build gets the runtime assert instead, which catches
 *         the same mistake one step later.
 */
[[gnu::error("this routine's frame does not fit the routine_storage it was given: raise its Size")]]
void the_routine_frame_does_not_fit_its_storage();

/**
 * @brief Storage for one suspended routine, provided by the caller
 * @tparam Size The size in bytes
 *
 * @remark Not @c stack_storage : a routine has no stack. What lives here is
 *         the frame the compiler builds -- the parameters, the locals that
 *         survive a suspension, and where to resume -- whose size it works
 *         out at compile time. Sizing it is checked, not guessed: a routine
 *         that does not fit reports it rather than overflowing.
 */
template<std::size_t Size>
struct routine_storage
{
	/**
	 * @brief Bytes reserved at the front, ahead of the frame
	 *
	 *        A routine's frame is released through @c operator @c delete ,
	 *        which is handed the frame's address and nothing else -- no
	 *        storage, no size parameter it could deduce one from. So the
	 *        storage leaves room in front of the frame to record where its
	 *        own bookkeeping is, and the release reads it back from there.
	 *
	 *        Sized by alignment rather than by @c sizeof(void*) so the frame
	 *        still starts on a boundary every type can live on.
	 */
	static constexpr std::size_t reserved = alignof(std::max_align_t);

	static_assert(reserved >= sizeof(void*), "no room in front of the frame for the back-pointer");
	static_assert(Size > reserved, "Size leaves no room for a frame at all");

	alignas(std::max_align_t) std::byte bytes[Size]{};

	/**
	 * @brief How many bytes the routine living here took, or 0 if it is free
	 *
	 * @remark Doubles as what says the storage is in use. A second routine
	 *         built into storage that still holds a live one would overwrite
	 *         it -- and the overwritten one's destructors never run, while the
	 *         handle to it goes on pointing at what is now someone else's
	 *         frame. @c operator @c new refuses that.
	 *
	 * @remark Useful in its own right for sizing: run once, read this, set
	 *         @c Size to it.
	 */
	std::size_t used = 0;
};

/**
 * @brief What a resumable interrupt routine returns
 *
 *        A routine is an interrupt's state machine written as straight-line
 *        code: it runs until it needs the next hardware event, suspends, and
 *        the handler resumes it when that event arrives. The state the
 *        handler would otherwise keep by hand -- which step, which byte,
 *        which retry -- is the routine's own locals.
 *
 * @warning It runs in interrupt context, so it must not call into OpSy:
 *          no @c mutex , no @c sleep_for , no @c condition_variable . Those
 *          suspend a task, and an interrupt has no task to suspend.
 */
class routine
{
public:

	class promise_type
	{
	public:

		/**
		 * @brief Places the frame in the storage the routine was given
		 *
		 * @remark The compiler passes @c operator @c new the routine's own
		 *         arguments, which is how the storage reaches it: a routine
		 *         takes its @ref routine_storage as first parameter, so each
		 *         one has its own and nothing is shared behind the scenes.
		 *
		 * @remark Declaring it here is also what keeps the heap out. Without
		 *         it the compiler calls the global @c operator @c new , and
		 *         the linker pulls in @c malloc and @c _sbrk behind it -- in
		 *         a system that has neither.
		 */
		template<std::size_t Size, typename... Arguments>
		static void* operator new(std::size_t size, routine_storage<Size>& storage, Arguments&&...) noexcept
		{
			constexpr std::size_t reserved = routine_storage<Size>::reserved;
			constexpr std::size_t available = Size - reserved;

#if defined(__OPTIMIZE__)
			// Fails the build rather than the run. See the declaration for why
			// this is the only place the frame size can be checked, and why it
			// takes an optimised build.
			if(size > available)
				the_routine_frame_does_not_fit_its_storage();
#endif

			// Storage that still holds a live routine. Building a second one
			// here would overwrite the first, whose destructors would never
			// run and whose handle would go on pointing at what is now this
			// frame -- so destroying the old handle would tear down the new
			// routine. Destroy the first routine before starting a second.
			assert(storage.used == 0);

			assert(size <= available); // the routine does not fit: raise Size

			if(size > available || storage.used != 0)
				return nullptr;

			storage.used = size;

			// Where the release will find its way back to `used`. Written with
			// memcpy rather than through a cast: the bytes are aligned well
			// enough, but saying so to the compiler means a cast it is right
			// to refuse under -Wcast-align. memcpy of a pointer compiles to
			// the same single store.
			std::size_t* const slot = &storage.used;
			std::memcpy(storage.bytes, &slot, sizeof(slot));

			return storage.bytes + reserved;
		}

		/**
		 * @brief Marks the storage free again
		 * @remark No memory is returned: the frame is the caller's storage.
		 *         What this releases is the storage's claim on it, so another
		 *         routine can be built there.
		 */
		static void operator delete(void* frame, std::size_t) noexcept
		{
			const auto* const front = static_cast<std::byte*>(frame) - alignof(std::max_align_t);

			std::size_t* slot = nullptr;
			std::memcpy(&slot, front, sizeof(slot));

			*slot = 0;
		}

		static routine get_return_object_on_allocation_failure() noexcept
		{
			return routine{};
		}

		routine get_return_object() noexcept
		{
			return routine{std::coroutine_handle<promise_type>::from_promise(*this)};
		}

		std::suspend_always initial_suspend() const noexcept { return {}; }

		/**
		 * @brief Leaves the routine suspended at its end rather than ending it
		 * @remark A frame that destroys itself leaves every handle dangling,
		 *         so @c done() would read freed storage. @ref routine
		 *         destroys it instead.
		 */
		std::suspend_always final_suspend() const noexcept { return {}; }

		void return_void() const noexcept {}

#ifndef NDEBUG
		bool running_ = false;   // read by resume(), which refuses to re-enter
#endif

		void unhandled_exception() const noexcept {}
	};

	constexpr routine() = default;

	explicit constexpr routine(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

	/** @brief Destroys the frame; the storage itself belongs to the caller. */
	~routine()
	{
		if(handle_)
			handle_.destroy();
	}

	routine(const routine&) = delete;
	routine& operator=(const routine&) = delete;

	constexpr routine(routine&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

	constexpr routine& operator=(routine&& other) noexcept
	{
		if(this == &other)
			return *this;

		if(handle_)
			handle_.destroy();

		handle_ = std::exchange(other.handle_, {});
		return *this;
	}

	/**
	 * @brief Whether the routine was created, and did not run out of storage
	 */
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return handle_ != nullptr; }

	/**
	 * @brief Whether the routine has run to its end
	 */
	[[nodiscard]] bool done() const noexcept { return !handle_ || handle_.done(); }

	/**
	 * @brief Runs the routine on to its next suspension, or to its end
	 * @remark Doing nothing once it is finished is deliberate: a handler can
	 *         call this on every interrupt without testing first.
	 */
	void resume() const
	{
		if(!handle_ || handle_.done())
			return;

#ifndef NDEBUG
		// Re-entering a frame that is already running destroys it: the two
		// executions share one set of locals and one resume point, and the
		// inner one leaves the frame at a step the outer one is not at.
		//
		// It is easier to arrange than it looks. A routine resumed by a task
		// may call into the scheduler -- it runs on the task's stack, in
		// thread mode, so sleep_for and the rest are legal. That suspends the
		// task *inside* the frame, and an interrupt resuming the same routine
		// while it is parked there re-enters it. Nothing about either half
		// looks wrong on its own.
		//
		// Without this assert the failure is a fault with nothing pointing at
		// the cause. A routine that a handler may resume should not call into
		// the scheduler at all; if it does, the task and the handler must not
		// be able to reach it at the same time.
		assert(!handle_.promise().running_); // re-entered while already running
		handle_.promise().running_ = true;
#endif

		handle_.resume();

#ifndef NDEBUG
		// The frame outlives its final suspension -- final_suspend suspends
		// rather than ending -- so the promise is still there to write to,
		// whether the routine suspended again or ran to its end.
		handle_.promise().running_ = false;
#endif
	}

private:

	std::coroutine_handle<promise_type> handle_{};
};

/**
 * @brief Suspends the routine until it is resumed, handing back @p Value
 * @tparam Value What the resumer passes back, typically the event that woke it
 *
 * @remark @c co_await @c suspended<uint32_t>{ &status } evaluates to what
 *         @c status holds when the routine runs again -- so a routine reads
 *         the hardware at the instant it resumes, not at the instant it
 *         suspended. That is the whole point: a status register only says
 *         something once the interrupt has fired.
 *
 * @remark To wait for the next event without reading anything, use the
 *         standard @c co_await @c std::suspend_always{} . This one is
 *         @c [[nodiscard]] on purpose -- reading a register and discarding
 *         what it said is a mistake worth catching, and there is already a
 *         way to say "just wait".
 *
 * @warning That @c [[nodiscard]] is not enforced everywhere: clang warns on a
 *          discarded @c co_await , GCC 14 does not. So it catches the mistake
 *          in a build compiled by clang, and says nothing in one compiled by
 *          GCC. Do not read it as a guarantee.
 */
template<typename Value>
struct suspended
{
	const volatile Value* source;

	[[nodiscard]] constexpr bool await_ready() const noexcept { return false; }
	constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
	[[nodiscard]] Value await_resume() const noexcept { return *source; }
};

}
