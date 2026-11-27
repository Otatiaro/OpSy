/**
 ******************************************************************************
 * @file    semaphore.hpp
 * @brief   Counting semaphore a task can block on and an interrupt handler
 *          can release.
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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "opsy_assert.hpp"
#include "config.hpp"
#include "cortex_m.hpp"
#include "hooks.hpp"
#include "scheduler.hpp"
#include "condition_variable.hpp"

namespace opsy
{

/**
 * @brief A counting semaphore, with the interface of @c std::counting_semaphore
 *
 *        @ref release adds to the count and @ref acquire takes one from it,
 *        blocking while it is zero. A task may do either; an interrupt
 *        handler may release and try to acquire, but not block.
 *
 *        Lock-free when it can be: taking a count that is there, or releasing
 *        with nobody waiting, is a single compare-and-swap on the count, with
 *        nothing masked. Only a task that has to wait, and a release that has
 *        someone to wake, go through the scheduler, masked to
 *        @c scheduler::service_call_priority and without a service call, as
 *        @c mutex::lock blocks: nothing has to be configured for an interrupt
 *        handler to release it.
 *
 *        Waiters are released in priority order, the most important first,
 *        and keep that order when their priority changes while they wait.
 *
 *        As with @c std::counting_semaphore , a release synchronizes with
 *        the acquire that takes its count: what was written before releasing
 *        is visible after acquiring.
 *
 * @tparam LeastMaxValue The largest count it has to hold. Releasing past it
 *         is a precondition violation, asserted. A @c binary_semaphore an
 *         interrupt handler releases is the usual case: the handler may run
 *         again before the task has taken the first release, and the second
 *         one is past the maximum. The handler coalesces them -- a flag it
 *         sets and the task clears, or a count of its own -- rather than
 *         releasing for every event.
 *
 * @warning As for every OpSy primitive, an interrupt handler has to run strictly
 *          below OpSy's preemption level to use it.
 */
template<std::ptrdiff_t LeastMaxValue = std::numeric_limits<int32_t>::max()>
class counting_semaphore
{
	static_assert(LeastMaxValue >= 0, "a semaphore cannot hold a negative count");
	static_assert(LeastMaxValue <= std::numeric_limits<int32_t>::max(), "the count is held in 32 bits");

public:

	/**
	 * @brief The largest count this semaphore holds
	 */
	[[nodiscard]] static constexpr std::ptrdiff_t max() noexcept
	{
		return LeastMaxValue;
	}

	/**
	 * @brief Creates a semaphore holding @p desired
	 */
	constexpr explicit counting_semaphore(std::ptrdiff_t desired) :
			count_(static_cast<int32_t>(desired))
	{
		assert(desired >= 0 && desired <= max());
	}

	counting_semaphore(const counting_semaphore&) = delete("a semaphore is waited on by address: it stays where it is, like std::counting_semaphore");
	counting_semaphore& operator=(const counting_semaphore&) = delete("a semaphore is waited on by address: it stays where it is, like std::counting_semaphore");
	counting_semaphore(counting_semaphore&&) = delete("a semaphore is waited on by address: it stays where it is, like std::counting_semaphore");
	counting_semaphore& operator=(counting_semaphore&&) = delete("a semaphore is waited on by address: it stays where it is, like std::counting_semaphore");
	~counting_semaphore() = default;

	/**
	 * @brief Adds @p update to the count, waking as many waiters as it lets through
	 * @remark Callable from a task or from an interrupt handler strictly below
	 *         OpSy's preemption level. Never blocks.
	 */
	void release(std::ptrdiff_t update = 1)
	{
		assert(scheduler::is_os_callable());
		assert(update >= 0);
		[[maybe_unused]] const auto previous = count_.fetch_add(static_cast<int32_t>(update), std::memory_order_release);
		assert(previous <= max() - update); // released past max()

		// Read unmasked: a waiter looks at the count and queues itself in one
		// step, masked to OpSy's own level, so nothing that could run this runs
		// in between. So either it queued before the count was raised, and is
		// seen here, or it will see the count when it looks. wake_one masks
		// around the one step that needs it.
		//
		// size() rather than empty(): SysTick takes a waiter whose deadline
		// passed off the list, and can do so in the middle of this read.
		// empty() checks the list's two fields against each other and would
		// see them disagree; size() is one word, read whole, and a hint only --
		// wake_one looks again under the mask.
		for (std::ptrdiff_t i = 0; i < update && available_.waiting_list_.size() != 0; ++i) // NOLINT(readability-container-size-empty): see above
		{
			hook::condition_variable_notify_one(available_);
			scheduler::wake_one(available_);
		}
	}

	/**
	 * @brief Takes one from the count, blocking while it is zero
	 * @warning From a task only.
	 */
	void acquire()
	{
		if (!try_acquire())
			(void) wait_for_count(std::nullopt);
	}

	/**
	 * @brief Takes one from the count if it is not zero
	 * @return @c true if it took one
	 * @remark Callable from anywhere OpSy may be called from. Never blocks.
	 */
	[[nodiscard]] bool try_acquire() noexcept
	{
		assert(scheduler::is_os_callable());
		auto current = count_.load(std::memory_order_relaxed);
		while (current > 0)
			if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire, std::memory_order_relaxed))
				return true;
		return false;
	}

	/**
	 * @brief Takes one from the count, blocking for at most @p timeout while it is zero
	 * @return @c true if it took one, @c false if the time ran out first
	 * @warning From a task only.
	 */
	[[nodiscard]] bool try_acquire_for(duration timeout)
	{
		return try_acquire_until(scheduler::now() + timeout);
	}

	/**
	 * @brief Takes one from the count, blocking until @p deadline at the latest while it is zero
	 * @return @c true if it took one, @c false if the deadline passed first
	 * @warning From a task only.
	 */
	[[nodiscard]] bool try_acquire_until(time_point deadline)
	{
		return try_acquire() || wait_for_count(deadline);
	}

private:

	/**
	 * @brief Blocks until a count can be taken, or @p deadline passes
	 * @return @c true once it took one
	 */
	bool wait_for_count(std::optional<time_point> deadline)
	{
		assert(cortex_m::ipsr() == 0); // an interrupt handler cannot block

		while (true)
		{
			const auto previous = cortex_m::exchange_basepri(scheduler::service_call_priority);
			assert(previous.value() == 0); // under a mask PendSV could not switch away, and the task would run on as if it waited

			if (try_acquire())
			{
				cortex_m::exchange_basepri(previous);
				return true;
			}

			if (!scheduler::wait_masked(available_, deadline))
			{
				cortex_m::exchange_basepri(previous);
				return false;
			}

			cortex_m::set_basepri(previous); // with its barrier: PendSV runs here, and
			                                 // this task resumes below once woken

			if (scheduler::current_task()->wait_timed_out())
				return try_acquire(); // released at the last moment still counts
		}
	}

	std::atomic<int32_t> count_;
	// The scheduler's wait queue: only its waiting list is used, under the
	// scheduler's own mask, never its notify or wait.
	condition_variable available_;
};

/**
 * @brief A semaphore holding at most one, with the interface of @c std::binary_semaphore
 */
using binary_semaphore = counting_semaphore<1>;

}
