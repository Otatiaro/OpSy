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
	alignas(std::max_align_t) std::byte bytes[Size]{};
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
#if defined(__OPTIMIZE__)
			// Fails the build rather than the run. See the declaration for why
			// this is the only place the frame size can be checked, and why it
			// takes an optimised build.
			if(size > Size)
				the_routine_frame_does_not_fit_its_storage();
#endif

			assert(size <= Size); // the routine does not fit: raise Size

			if(size > Size)
				return nullptr;

			storage.used = size;
			return storage.bytes;
		}

		static void operator delete(void*, std::size_t) noexcept
		{
			// Nothing to free: the frame is the caller's storage.
		}

		static routine get_return_object_on_allocation_failure() noexcept
		{
			return routine{};
		}

		routine get_return_object() noexcept
		{
			return routine{std::coroutine_handle<promise_type>::from_promise(*this)};
		}

		// Suspended before its first statement: the routine starts when the
		// caller resumes it, not when it is created.
		std::suspend_always initial_suspend() const noexcept { return {}; }

		/**
		 * @brief Leaves the routine suspended at its end rather than ending it
		 *
		 * @remark Suspending here, instead of letting the frame destroy
		 *         itself, is what makes @c done() answerable. A routine that
		 *         destroys its own frame leaves every handle to it dangling,
		 *         and the handler's @c resume() -- or the @c done() guarding
		 *         it -- then reads freed storage. The frame is destroyed by
		 *         @ref routine instead, which knows when nobody holds it any
		 *         more.
		 */
		std::suspend_always final_suspend() const noexcept { return {}; }

		void return_void() const noexcept {}

		// Required by the language even with -fno-exceptions, where nothing
		// can call it.
		void unhandled_exception() const noexcept {}
	};

	constexpr routine() = default;

	explicit constexpr routine(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

	/**
	 * @brief Destroys the frame, running the destructors of whatever the
	 *        routine still held
	 *
	 * @remark The storage itself is the caller's and is not freed -- there is
	 *         nowhere to free it to. What this releases is the objects living
	 *         in it, for a routine abandoned part-way through.
	 */
	~routine()
	{
		if(handle_)
			handle_.destroy();
	}

	// Not copyable: two handles to one frame would each destroy it.
	routine(const routine&) = delete;
	routine& operator=(const routine&) = delete;

	constexpr routine(routine&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

	constexpr routine& operator=(routine&& other) noexcept
	{
		if(this == &other)
			return *this;

		// Whatever this one held is dropped before taking the other's, or the
		// frame it was running would be left with nothing pointing at it.
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
		if(handle_ && !handle_.done())
			handle_.resume();
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
