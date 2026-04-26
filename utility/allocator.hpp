/**
 ******************************************************************************
 * @file    allocator.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   A fixed-size, heap-free memory allocator
 *
 * 			Manages a single contiguous storage area of @c N slots and hands
 * 			out aligned blocks from it. Free space is tracked in-band with
 * 			boundary tags, so adjacent free blocks are coalesced on
 * 			@c deallocate. Optionally fills freed memory with a sentinel
 * 			pattern (@c UseDummy) to help catch use-after-free.
 *
 ******************************************************************************
 * @copyright Copyright 2026 Thomas Legrand under the MIT License
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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace opsy::utility
{

/**
 * @brief Fixed-size, heap-free allocator.
 *
 *        Manages a single std::array<int, N> as a sequence of free and
 *        allocated chunks separated by boundary tags. Each chunk has two
 *        indicator slots — one at its head, one at its tail — that hold the
 *        chunk size as a positive int when free and as a negative int when
 *        allocated. @ref deallocate coalesces adjacent free chunks.
 *
 *        @ref allocate only ever splits the trailing free chunk: any free
 *        chunk created in the middle of the buffer by @ref deallocate stays
 *        unusable until the surrounding allocations are released and the
 *        regions merge into the trailing chunk. The allocator is therefore
 *        not a general-purpose freelist; it is a "stack with backwards
 *        cleanup" that suits embedded code where allocation order is
 *        usually LIFO.
 *
 * @tparam N        Total slot count of the underlying buffer (= sizeof(int)
 *                  bytes per slot). Must be at least 4 — the minimum useful
 *                  size is "2 head/tail indicators of a single chunk + 2
 *                  data slots".
 * @tparam UseDummy When @c true, freed slots are filled with @p Dummy and
 *                  @ref run_check verifies the pattern. Useful to catch
 *                  use-after-free.
 * @tparam Dummy    The sentinel pattern used by @c UseDummy (default
 *                  @c 0xBAADF00D).
 */
template<std::size_t N, bool UseDummy = false, int Dummy = static_cast<int>(0xBAADF00D)>
class allocator
{
public:

	using size_type = std::size_t;
	using element_type = int;

	static_assert(N > 3, "N must be at least 3 to be able to allocate");
	static_assert(sizeof(element_type) == sizeof(void*), "Mismatch of size between pointer and element size");

	constexpr allocator()
	{
		if constexpr (UseDummy)
			data_.fill(Dummy);

		data_[0] = data_[N - 1] = N - 2;
	}

	/**
	 * Allocates some memory
	 * @param bytes The requested number of bytes to allocate
	 * @warning if the allocator cannot allocate memory (typically not enough memory available), it will return nullptr, so please check return value against nullptr
	 */
	void* allocate(size_type bytes)
	{
		if (bytes == 0) // cannot allocate zero bytes
			return nullptr;

		const auto available_slots = data_[N - 1]; // the available slots
		const auto needed_slots = static_cast<element_type>((bytes - 1) / sizeof(element_type) + 1); // the number of slots needed to allocate

		// Splitting the trailing free chunk creates a new (smaller) free chunk
		// after the allocation, which itself needs 2 indicator slots — so the
		// usable headroom is `available_slots - 2`, not `available_slots`.
		// Without this margin a maximum-sized allocation would write past the
		// buffer end and corrupt the trailing indicator.
		if (needed_slots + 2 > available_slots) // not enough free slots to allocate, return null
			return nullptr;

		const auto previous_index = N - 2 - static_cast<size_type>(available_slots); // this is the index of the beginning of the free space
		const auto new_index = previous_index + static_cast<size_type>(needed_slots) + 2; // this is the index of the end of the new allocation
		data_[previous_index] = data_[new_index - 1] = -needed_slots; // creates the new allocation
		data_[new_index] = data_[N - 1] = available_slots - needed_slots - 2; // and update the remaining slots
		return reinterpret_cast<void*>(&data_[previous_index + 1]);
	}

	template<typename T>
	T* allocate(size_type count = 1)
	{
		return reinterpret_cast<T*>(allocate(count * sizeof(T)));
	}

	/**
	 * Deallocates some memory
	 * @param ptr The pointer to the memory to deallocate
	 */
	void deallocate(const void* ptr)
	{
		assert(owns(ptr)); // to deallocate, the ptr must be handled by the allocator
		auto ptr_index = static_cast<size_type>(reinterpret_cast<const element_type*>(ptr) - data_.data()); // ptr_index is the slot index of the pointer
		auto allocated_slots = -data_[ptr_index - 1]; // the size of the allocation
		assert(allocated_slots > 0 && (data_[ptr_index + static_cast<size_type>(allocated_slots)] == -allocated_slots)); // check the indicators are coherent
		data_[ptr_index - 1] = data_[ptr_index + static_cast<size_type>(allocated_slots)] = allocated_slots; // release the allocation

		if constexpr (UseDummy)
			for (auto i = ptr_index; i < ptr_index + static_cast<size_type>(allocated_slots); ++i)
				data_[i] = Dummy;

		if (ptr_index != 1) // the allocation was not the first one, so check if the previous allocation is also free
		{
			const auto previous_allocation_slots = data_[ptr_index - 2];
			if (previous_allocation_slots > 0) // the previous allocation if free, merge the two
			{
				const auto combined_slots = allocated_slots + previous_allocation_slots + 2;
				data_[ptr_index - 3 - static_cast<size_type>(previous_allocation_slots)] = data_[ptr_index + static_cast<size_type>(allocated_slots)] = combined_slots;
				if constexpr (UseDummy)
					data_[ptr_index - 1] = data_[ptr_index - 2] = Dummy;

				// update variable in case there is also a free allocation after the current one
				allocated_slots = combined_slots;
				ptr_index -= static_cast<size_type>(previous_allocation_slots) + 2;
			}
		}

		if (ptr_index + static_cast<size_type>(allocated_slots) + 1 < N - 2) // the allocation was not the last one, check if next if also free
		{
			const auto next_allocation_slots = data_[ptr_index + static_cast<size_type>(allocated_slots) + 1];
			if (next_allocation_slots > 0) // next slot is free, merge the two
			{
				const auto combined_slots = allocated_slots + next_allocation_slots + 2;
				data_[ptr_index - 1] = data_[ptr_index + static_cast<size_type>(combined_slots)] = combined_slots;
				if constexpr (UseDummy)
					data_[ptr_index + static_cast<size_type>(allocated_slots)] = data_[ptr_index + static_cast<size_type>(allocated_slots) + 1] = Dummy;
			}
		}
	}

	/**
	 * Checks if a pointer is owned (allocated by) the current allocator
	 * @param ptr The pointer to check
	 * @return @c true if the pointer is owned by the current allocator, @c false otherwise
	 */
	bool owns(const void* ptr) const
	{
		assert(reinterpret_cast<std::uintptr_t>(ptr) % alignof(element_type) == 0); // the pointer is necessary aligned with the slots
		const auto index = reinterpret_cast<const element_type*>(ptr) - data_.data(); // gets the pointer difference between data start and ptr
		return index > 0 && index < static_cast<std::ptrdiff_t>(N - 1); // ptr in owned if between the allocator boundaries minus indicators
	}

	/**
	 * Gets the size of the allocator, which is the size of the biggest slot that can be allocated
	 * @return The size of the allocator, which is the size of the biggest slot that can be allocated
	 */
	constexpr size_type size() const
	{
		return (N - 2) * sizeof(element_type);
	}

	/**
	 * Gets the currently available free memory for allocation, in bytes.
	 *
	 * This is what you can actually pass to @ref allocate right now: the
	 * size of the trailing free chunk minus the 2 slots that will be spent
	 * on indicators when the chunk is split.
	 *
	 * @remark The allocator only consumes the trailing free chunk, so any
	 *         free chunk left in the middle of the buffer by a previous
	 *         @ref deallocate is NOT counted here until the surrounding
	 *         allocations are freed and the regions coalesce.
	 *
	 * @return The currently allocatable size in bytes (0 if the trailing
	 *         chunk is too small to host any allocation).
	 */
	constexpr size_type available() const
	{
		const auto last_slot = data_[N - 1];
		return last_slot > 2 ? static_cast<size_type>(last_slot - 2) * sizeof(element_type) : 0;
	}

	/**
	 * Checks whether the allocator is empty (no active allocation).
	 *
	 * @return @c true if the allocator is empty, @c false otherwise
	 */
	constexpr bool empty() const
	{
		// The trailing free chunk spans the whole buffer minus its own 2
		// indicators (start at data_[0], end at data_[N-1]) iff there is no
		// active allocation.
		return data_[N - 1] == static_cast<element_type>(N - 2);
	}

	/**
	 * Runs some coherence checks on the allocator and its content
	 * @return @c true if the memory is coherent (no error found), @c false otherwise
	 */
	constexpr bool run_check() const
	{
		size_type start_indicator = 0; // start at the beginning of the data

		while (true)
		{
			const auto chunk_size = data_[start_indicator]; // gets the size of the first space
			// Manual |chunk_size|: std::abs(int) lives in <cstdlib> and is not constexpr
			// in libstdc++/libc++ before C++23, which would make run_check() unusable from
			// a static_assert.
			const auto chunk_size_abs = chunk_size < 0 ? -chunk_size : chunk_size;
			const auto end_indicator_index = start_indicator + 1U + static_cast<size_type>(chunk_size_abs); // position of the end of chunk indicator

			if ((end_indicator_index > N - 1) || data_[end_indicator_index] != chunk_size) // the allocation points outside of the data or the end indicator has a different value
				return false;

			if constexpr (UseDummy)
				if (chunk_size > 0) // chunk was not allocated, verify it contains dummy value
					for (auto i = start_indicator + 1U; i < end_indicator_index; ++i)
						if (data_[i] != Dummy)
							return false;

			start_indicator = end_indicator_index + 1;

			if (start_indicator == N) // reached the end of the data
				return true;
		}
	}

private:

	std::array<element_type, N> data_;
};

}
