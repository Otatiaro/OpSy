/**
 ******************************************************************************
 * @file    queue.hpp
 * @brief   Fixed-capacity queue between one producer and one consumer, either
 *          of which may be an interrupt handler, with the interface of the
 *          proposed std::bounded_queue (P0260).
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
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "opsy_assert.hpp"
#include "scheduler.hpp"
#include "isr_lock.hpp"
#include "condition_variable.hpp"

namespace opsy
{

/**
 * @brief What a non-blocking queue operation did, as the proposed
 *        @c std::conqueue_status (P0260)
 */
enum class conqueue_status
{
	success,    ///< done
	empty,      ///< nothing to pop
	full,       ///< no room to push
	closed,     ///< the queue was closed
	busy,       ///< would have had to wait for internal synchronization (never returned here)
	busy_async, ///< would have had to schedule an asynchronous continuation (never returned here)
};

/**
 * @brief A queue of up to @p Capacity elements between one producer and one consumer
 *
 *        The interface of the proposed @c std::bounded_queue (P0260): @ref push
 *        and @ref pop wait, @ref try_push and @ref try_pop never do and report
 *        why they could not, and @ref close ends the queue for both sides.
 *        Where it differs: the capacity is a template argument and the
 *        elements live in the queue itself, so nothing is allocated; and it
 *        has one producer and one consumer, not any number.
 *
 *        Each side may be a task or an interrupt handler. An interrupt
 *        handler uses @ref try_push or @ref try_pop , a task may also wait.
 *        Nothing is masked on the way through: each side writes only its own
 *        end of the ring, and the count they share is published with release
 *        and read with acquire, so what the producer constructed is visible to
 *        the consumer that takes it. Only a side that has to wait, and one with
 *        a waiter to wake, go through the scheduler.
 *
 *        A full queue refuses: @ref try_push reports @c full and stores
 *        nothing. The refusals are counted (@ref dropped ), and so is the most
 *        the queue ever held (@ref high_water ), so that a loss is seen and a
 *        capacity chosen from what was measured.
 *
 * @tparam T What it holds. Moving and destroying one must not throw, and must
 *         be fit for an interrupt handler if one is a side of the queue: they
 *         run in the producer's and the consumer's contexts.
 * @tparam Capacity How many it holds at most.
 *
 * @warning One producer, one consumer: each end used from a single context --
 *          one task, or one interrupt handler. That is what makes it safe
 *          without masking anything; two producers would write the same end.
 *          Asserted in debug builds. @ref close and the observers may be
 *          called from anywhere.
 */
template<typename T, std::size_t Capacity>
class queue
{
	static_assert(Capacity > 0, "a queue holds at least one element");
	static_assert(Capacity <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()), "the counts are held in 32 bits");
	static_assert(std::is_nothrow_move_constructible_v<T>, "an element is moved in and out, and nothing may throw");
	static_assert(std::is_nothrow_destructible_v<T>, "an element is destroyed when popped, and nothing may throw");

public:

	using value_type = T;

	constexpr queue() = default;

	queue(const queue&) = delete;
	queue& operator=(const queue&) = delete;

	~queue()
	{
		if constexpr (!std::is_trivially_destructible_v<T>)
			for (auto remaining = size_.load(std::memory_order_relaxed); remaining > 0; --remaining)
			{
				element(tail_).~T();
				tail_ = next(tail_);
			}
	}

	/**
	 * @brief The most it holds
	 */
	[[nodiscard]] static constexpr std::size_t capacity() noexcept
	{
		return Capacity;
	}

	// --- producer ----------------------------------------------------------

	/**
	 * @brief Stores a copy of @p value, waiting while the queue is full
	 * @return @c false if the queue is closed, in which case nothing is stored
	 * @warning From a task only.
	 */
	bool push(const T& value) requires std::is_copy_constructible_v<T>
	{
		return emplace(value);
	}

	/** @copydoc push(const T&) */
	bool push(T&& value)
	{
		return emplace(std::move(value));
	}

	/**
	 * @brief Constructs an element from @p arguments, waiting while the queue is full
	 * @return @c false if the queue is closed, in which case nothing is constructed
	 * @warning From a task only.
	 */
	template<typename... Arguments>
	bool emplace(Arguments&&... arguments)
	{
		check_side(producer_);
		if (!wait_while_full(std::nullopt))
			return false;
		store(std::forward<Arguments>(arguments)...);
		return true;
	}

	/**
	 * @brief Stores a copy of @p value, if there is room
	 * @return @c success, @c full -- counted by @ref dropped -- or @c closed
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	[[nodiscard]] conqueue_status try_push(const T& value) requires std::is_copy_constructible_v<T>
	{
		return try_emplace(value);
	}

	/** @copydoc try_push(const T&) */
	[[nodiscard]] conqueue_status try_push(T&& value)
	{
		return try_emplace(std::move(value));
	}

	/**
	 * @brief Constructs an element from @p arguments in place, if there is room
	 * @return @c success, @c full -- counted by @ref dropped -- or @c closed
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	template<typename... Arguments>
	[[nodiscard]] conqueue_status try_emplace(Arguments&&... arguments)
	{
		check_side(producer_);
		if (closed_.load(std::memory_order_relaxed))
			return conqueue_status::closed;
		if (size_.load(std::memory_order_acquire) == Capacity)
		{
			dropped_.fetch_add(1, std::memory_order_relaxed);
			return conqueue_status::full;
		}
		store(std::forward<Arguments>(arguments)...);
		return conqueue_status::success;
	}

	/**
	 * @brief Stores @p value, waiting at most @p timeout while the queue is full
	 * @return @c success, @c full if the time ran out first -- a wait that ran
	 *         out is not counted by @ref dropped -- or @c closed
	 * @remark Not in P0260, which leaves waiting with a limit to its
	 *         asynchronous operations.
	 * @warning From a task only.
	 */
	[[nodiscard]] conqueue_status push_for(T value, duration timeout)
	{
		check_side(producer_);
		if (!wait_while_full(scheduler::now() + timeout))
			return closed_.load(std::memory_order_relaxed) ? conqueue_status::closed : conqueue_status::full;
		store(std::move(value));
		return conqueue_status::success;
	}

	// --- consumer ----------------------------------------------------------

	/**
	 * @brief Takes the oldest element, waiting while the queue is empty
	 * @return Nothing once the queue is closed and has nothing left: what was
	 *         stored before closing is still taken first
	 * @warning From a task only.
	 */
	[[nodiscard]] std::optional<T> pop()
	{
		check_side(consumer_);
		if (!wait_while_empty(std::nullopt))
			return std::nullopt;
		return take();
	}

	/**
	 * @brief Takes the oldest element, if there is one
	 * @return The element, or @c empty , or @c closed once the queue is closed
	 *         and has nothing left
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	[[nodiscard]] std::expected<T, conqueue_status> try_pop()
	{
		check_side(consumer_);
		if (size_.load(std::memory_order_acquire) == 0)
			return std::unexpected(closed_.load(std::memory_order_relaxed) ? conqueue_status::closed : conqueue_status::empty);
		return take();
	}

	/**
	 * @brief Takes the oldest element, waiting at most @p timeout while the queue is empty
	 * @return The element, or @c empty if the time ran out first, or @c closed
	 *         once the queue is closed and has nothing left
	 * @remark Not in P0260, which leaves waiting with a limit to its
	 *         asynchronous operations.
	 * @warning From a task only.
	 */
	[[nodiscard]] std::expected<T, conqueue_status> pop_for(duration timeout)
	{
		check_side(consumer_);
		if (!wait_while_empty(scheduler::now() + timeout))
			return std::unexpected(closed_.load(std::memory_order_relaxed) ? conqueue_status::closed : conqueue_status::empty);
		return take();
	}

	// --- either side, or anyone ----------------------------------------------

	/**
	 * @brief Ends the queue: no push succeeds any more, and whoever waits stops waiting
	 * @remark What is in it stays, and is still popped; a pop returns nothing
	 *         only once it is also empty. Callable from anywhere OpSy may be
	 *         called from, and more than once.
	 */
	void close() noexcept
	{
		closed_.store(true, std::memory_order_relaxed);
		std::lock_guard<isr_lock> guard(lock_);
		not_empty_.notify_all();
		not_full_.notify_all();
	}

	[[nodiscard]] bool is_closed() const noexcept
	{
		return closed_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief How many it holds now -- already stale if the other side is running
	 */
	[[nodiscard]] std::size_t size() const noexcept
	{
		return size_.load(std::memory_order_relaxed);
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return size() == 0;
	}

	/**
	 * @brief How many elements a full queue refused, since it was created
	 */
	[[nodiscard]] std::uint32_t dropped() const noexcept
	{
		return dropped_.load(std::memory_order_relaxed);
	}

	/**
	 * @brief The most it ever held at once, since it was created
	 * @remark Reaching @ref capacity means it was full at least once: a push
	 *         may have been refused, or have had to wait.
	 */
	[[nodiscard]] std::size_t high_water() const noexcept
	{
		return high_water_.load(std::memory_order_relaxed);
	}

private:

	static constexpr std::size_t next(std::size_t index) noexcept
	{
		return index + 1 == Capacity ? 0 : index + 1;
	}

	void* slot(std::size_t index) noexcept
	{
		return &storage_[index * sizeof(T)];
	}

	T& element(std::size_t index) noexcept
	{
		return *std::launder(static_cast<T*>(slot(index)));
	}

	/**
	 * @brief Waits for room, or for the queue to close
	 * @return @c true if there is room, @c false if closed or the deadline passed
	 * @remark The check and the wait are under the lock, which holds off every
	 *         handler that may call OpSy and every task switch: a pop cannot
	 *         free a place between the two, so none is missed.
	 */
	bool wait_while_full(std::optional<time_point> deadline)
	{
		if (closed_.load(std::memory_order_relaxed))
			return false;
		if (size_.load(std::memory_order_acquire) < Capacity)
			return true;

		std::lock_guard<isr_lock> guard(lock_);
		while (size_.load(std::memory_order_acquire) == Capacity && !closed_.load(std::memory_order_relaxed))
			if (!wait(not_full_, deadline))
				break;
		return !closed_.load(std::memory_order_relaxed) && size_.load(std::memory_order_acquire) < Capacity;
	}

	/**
	 * @brief Waits for an element, or for the queue to close with nothing left
	 * @return @c true if there is an element, @c false otherwise
	 */
	bool wait_while_empty(std::optional<time_point> deadline)
	{
		if (size_.load(std::memory_order_acquire) > 0)
			return true;

		std::lock_guard<isr_lock> guard(lock_);
		while (size_.load(std::memory_order_acquire) == 0 && !closed_.load(std::memory_order_relaxed))
			if (!wait(not_empty_, deadline))
				break;
		return size_.load(std::memory_order_acquire) > 0;
	}

	/** @return @c false if @p deadline passed */
	bool wait(condition_variable& condition, std::optional<time_point> deadline)
	{
		if (!deadline.has_value())
		{
			condition.wait(lock_);
			return true;
		}
		return condition.wait_until(lock_, *deadline) == cv_status::no_timeout;
	}

	/** @brief Wakes whoever waits on @p condition, without the lock when nobody does */
	void wake(condition_variable& condition)
	{
		// Read without the lock: a side only queues itself while holding it,
		// and holding it masks every handler that may call OpSy and every
		// task switch. So either it queued before the count changed, and is
		// seen here, or it will see the count when it looks.
		if (condition.waiting_list_.empty())
			return;
		std::lock_guard<isr_lock> guard(lock_);
		condition.notify_one();
	}

	template<typename... Arguments>
	void store(Arguments&&... arguments)
	{
		::new (slot(head_)) T(std::forward<Arguments>(arguments)...);
		head_ = next(head_);

		// Release: publishes the element to the consumer that reads the count.
		const auto held = size_.fetch_add(1, std::memory_order_release) + 1;
		if (held > high_water_.load(std::memory_order_relaxed))
			high_water_.store(held, std::memory_order_relaxed);   // the producer is the only writer

		wake(not_empty_);
	}

	T take()
	{
		T& stored = element(tail_);
		T value(std::move(stored));
		stored.~T();
		tail_ = next(tail_);

		// Release: hands the place back to the producer that reads the count.
		size_.fetch_sub(1, std::memory_order_release);
		wake(not_full_);
		return value;
	}

#ifndef NDEBUG
	/**
	 * @brief Who is running: an interrupt handler by its exception number, a
	 *        task by its control block, code before the scheduler starts as 1
	 */
	static std::uintptr_t context() noexcept
	{
		if (const auto exception = cortex_m::ipsr(); exception != 0)
			return 0x80000000u | exception;
		if (const auto* task = scheduler::current_task(); task != nullptr)
			return reinterpret_cast<std::uintptr_t>(task);
		return 1;
	}

	static void check_side(std::atomic<std::uintptr_t>& side) noexcept
	{
		std::uintptr_t expected = 0;
		const auto running = context();
		if (!side.compare_exchange_strong(expected, running, std::memory_order_relaxed))
			assert(expected == running);   // one producer, one consumer
	}

	std::atomic<std::uintptr_t> producer_{ 0 };
	std::atomic<std::uintptr_t> consumer_{ 0 };
#else
	static void check_side(int) noexcept {}
	static constexpr int producer_ = 0;
	static constexpr int consumer_ = 0;
#endif

	// Zero-initialized so that a queue is constant-initialized: one declared
	// at namespace scope is ready before any constructor runs, for an
	// interrupt handler that fires that early.
	alignas(T) std::byte storage_[Capacity * sizeof(T)]{};
	std::size_t head_ = 0;   // the producer's end, written by it alone
	std::size_t tail_ = 0;   // the consumer's end, written by it alone
	std::atomic<std::uint32_t> size_{ 0 };
	std::atomic<std::uint32_t> high_water_{ 0 };
	std::atomic<std::uint32_t> dropped_{ 0 };
	std::atomic<bool> closed_{ false };
	isr_lock lock_{ scheduler::os_callable_priority };
	condition_variable not_empty_{ scheduler::os_callable_priority };
	condition_variable not_full_{ scheduler::os_callable_priority };
};

}
