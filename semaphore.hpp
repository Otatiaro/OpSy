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
#include <mutex>

#include "opsy_assert.hpp"
#include "scheduler.hpp"
#include "isr_lock.hpp"
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
 *        no service call and nothing masked. Only a task that has to wait, and
 *        a release that has someone to wake, go through the scheduler, under
 *        a mask at @c scheduler::os_callable_priority : nothing has to be
 *        configured for an interrupt handler to release it.
 *
 *        Waiters are released in priority order, the most important first,
 *        and keep that order when their priority changes while they wait.
 *
 * @tparam LeastMaxValue The largest count it has to hold. Releasing past it
 *         is a precondition violation, asserted.
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

	counting_semaphore(const counting_semaphore&) = delete;
	counting_semaphore& operator=(const counting_semaphore&) = delete;

	/**
	 * @brief Adds @p update to the count, waking as many waiters as it lets through
	 * @remark Callable from a task or from an interrupt handler strictly below
	 *         OpSy's preemption level. Never blocks.
	 */
	void release(std::ptrdiff_t update = 1)
	{
		assert(scheduler::is_os_callable());
		assert(update >= 0);
		[[maybe_unused]] const auto previous = count_.fetch_add(static_cast<int32_t>(update), std::memory_order_relaxed);
		assert(previous <= max() - update); // released past max()

		// Read without the lock: a waiter only queues itself while holding it,
		// and holding it masks every handler that may call OpSy and every task
		// switch, so nothing that could run this. So either it queued before the count was raised, and is
		// seen here, or it will see the count when it looks.
		if (available_.waiting_list_.empty())
			return;

		std::lock_guard<isr_lock> guard(lock_);
		for (std::ptrdiff_t i = 0; i < update && !available_.waiting_list_.empty(); ++i)
			available_.notify_one();
	}

	/**
	 * @brief Takes one from the count, blocking while it is zero
	 * @warning From a task only.
	 */
	void acquire()
	{
		if (try_acquire())
			return;

		assert(cortex_m::ipsr() == 0); // an interrupt handler cannot block

		std::lock_guard<isr_lock> guard(lock_);
		while (!try_acquire())
			available_.wait(lock_);
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
			if (count_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
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
		if (try_acquire())
			return true;

		assert(cortex_m::ipsr() == 0); // an interrupt handler cannot block

		std::lock_guard<isr_lock> guard(lock_);
		while (!try_acquire())
			if (available_.wait_until(lock_, deadline) == cv_status::timeout)
				return try_acquire(); // released at the last moment still counts
		return true;
	}

private:

	std::atomic<int32_t> count_;
	isr_lock lock_{ scheduler::os_callable_priority };
	condition_variable available_{ scheduler::os_callable_priority };
};

/**
 * @brief A semaphore holding at most one, with the interface of @c std::binary_semaphore
 */
using binary_semaphore = counting_semaphore<1>;

}
