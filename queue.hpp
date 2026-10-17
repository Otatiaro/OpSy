/**
 ******************************************************************************
 * @file    queue.hpp
 * @brief   Fixed-capacity queue between one producer and one consumer, either
 *          of which may be an interrupt handler.
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
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "opsy_assert.hpp"
#include "scheduler.hpp"
#include "semaphore.hpp"

namespace opsy
{

/**
 * @brief A queue of up to @p Capacity elements between one producer and one consumer
 *
 *        The elements live in the queue itself; nothing is allocated. Each
 *        side may be a task or an interrupt handler: an interrupt handler
 *        pushes with @ref try_push and pops with @ref try_pop , which never
 *        block, and a task may also use @ref push and @ref pop , which wait.
 *
 *        Nothing is masked. Each side owns its own end of the ring and is the
 *        only one to write it; what they share is two counts -- the elements
 *        there, the places free -- each a @ref counting_semaphore , which is
 *        lock-free while nobody waits and makes what the producer constructed
 *        visible to the consumer that takes it.
 *
 *        A full queue refuses: @ref try_push returns @c false and the element
 *        is not stored. The refusals are counted (@ref dropped ), and so is
 *        the most the queue ever held (@ref high_water ), so that a loss is
 *        seen and a capacity chosen from what was measured.
 *
 * @tparam T What it holds. Moving and destroying one must not throw, and must
 *         be fit for an interrupt handler if one is a side of the queue: they
 *         run in the producer's and the consumer's contexts.
 * @tparam Capacity How many it holds at most.
 *
 * @warning One producer, one consumer: each end used from a single context --
 *          one task, or one interrupt handler. That is what makes it safe
 *          without masking anything; two producers would write the same end.
 *          Asserted in debug builds.
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
	 * @brief Stores a copy of @p value, if there is room
	 * @return @c false if the queue was full, in which case nothing is stored
	 *         and @ref dropped counts it
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	[[nodiscard]] bool try_push(const T& value) requires std::is_copy_constructible_v<T>
	{
		return try_emplace(value);
	}

	/** @copydoc try_push(const T&) */
	[[nodiscard]] bool try_push(T&& value)
	{
		return try_emplace(std::move(value));
	}

	/**
	 * @brief Constructs an element from @p arguments in place, if there is room
	 * @return @c false if the queue was full, in which case nothing is
	 *         constructed and @ref dropped counts it
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	template<typename... Arguments>
	[[nodiscard]] bool try_emplace(Arguments&&... arguments)
	{
		check_side(producer_);
		if (!spaces_.try_acquire())
		{
			dropped_.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		store(std::forward<Arguments>(arguments)...);
		return true;
	}

	/**
	 * @brief Stores @p value, waiting while the queue is full
	 * @warning From a task only.
	 */
	void push(T value)
	{
		check_side(producer_);
		spaces_.acquire();
		store(std::move(value));
	}

	/**
	 * @brief Stores @p value, waiting at most @p timeout while the queue is full
	 * @return @c false if the time ran out first, in which case nothing is
	 *         stored -- a wait that ran out is not counted by @ref dropped
	 * @warning From a task only.
	 */
	[[nodiscard]] bool push_for(T value, duration timeout)
	{
		check_side(producer_);
		if (!spaces_.try_acquire_for(timeout))
			return false;
		store(std::move(value));
		return true;
	}

	// --- consumer ----------------------------------------------------------

	/**
	 * @brief Takes the oldest element, if there is one
	 * @remark Never blocks: callable from an interrupt handler.
	 */
	[[nodiscard]] std::optional<T> try_pop()
	{
		check_side(consumer_);
		if (!items_.try_acquire())
			return std::nullopt;
		return take();
	}

	/**
	 * @brief Takes the oldest element, waiting while the queue is empty
	 * @warning From a task only.
	 */
	[[nodiscard]] T pop()
	{
		check_side(consumer_);
		items_.acquire();
		return take();
	}

	/**
	 * @brief Takes the oldest element, waiting at most @p timeout while the queue is empty
	 * @warning From a task only.
	 */
	[[nodiscard]] std::optional<T> pop_for(duration timeout)
	{
		check_side(consumer_);
		if (!items_.try_acquire_for(timeout))
			return std::nullopt;
		return take();
	}

	// --- either side ---------------------------------------------------------

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

	template<typename... Arguments>
	void store(Arguments&&... arguments)
	{
		::new (slot(head_)) T(std::forward<Arguments>(arguments)...);
		head_ = next(head_);

		const auto held = size_.fetch_add(1, std::memory_order_relaxed) + 1;
		if (held > high_water_.load(std::memory_order_relaxed))
			high_water_.store(held, std::memory_order_relaxed);   // the producer is the only writer

		items_.release();   // publishes the element to the consumer
	}

	T take()
	{
		T& stored = element(tail_);
		T value(std::move(stored));
		stored.~T();
		tail_ = next(tail_);

		size_.fetch_sub(1, std::memory_order_relaxed);
		spaces_.release();  // hands the place back to the producer
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
	// interrupt handler that fires that early, and lives in .bss.
	alignas(T) std::byte storage_[Capacity * sizeof(T)]{};
	std::size_t head_ = 0;   // the producer's end, written by it alone
	std::size_t tail_ = 0;   // the consumer's end, written by it alone
	counting_semaphore<static_cast<std::ptrdiff_t>(Capacity)> items_{ 0 };
	counting_semaphore<static_cast<std::ptrdiff_t>(Capacity)> spaces_{ static_cast<std::ptrdiff_t>(Capacity) };
	std::atomic<std::uint32_t> size_{ 0 };
	std::atomic<std::uint32_t> high_water_{ 0 };
	std::atomic<std::uint32_t> dropped_{ 0 };
};

}
