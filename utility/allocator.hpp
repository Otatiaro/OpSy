/**
 ******************************************************************************
 * @file    allocator.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   A fixed-size, heap-free memory allocator
 *
 * 			Manages a single contiguous storage area of @c Size slots and hands
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
#include <limits>

namespace opsy::utility
{

template<std::size_t Size, bool UseDummy = false, int Dummy = 0xBAADF00D>
class allocator
{
public:

	using size_type = std::size_t;
	using element_type = int;

	static_assert(Size > 3, "Size must be at least 3 to be able to allocate");
	static_assert(sizeof(element_type) == sizeof(void*), "Mismatch of size between pointer and element size");

	constexpr allocator()
	{
		if constexpr (UseDummy)
			data_.fill(Dummy);

		data_[0] = data_[Size - 1] = Size - 2;
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

		const auto available_slots = data_[Size - 1]; // the available slots
		const auto needed_slots = static_cast<element_type>((bytes - 1) / sizeof(element_type) + 1); // the number of slots needed to allocate

		if (needed_slots > available_slots) // not enough free slots to allocate, return null
			return nullptr;

		const auto previous_index = Size - 2 - available_slots; // this is the index of the beginning of the free space
		const auto new_index = previous_index + needed_slots + 2; // this is the index of the end of the new allocation
		data_[previous_index] = data_[new_index - 1] = -needed_slots; // creates the new allocation
		data_[new_index] = data_[Size - 1] = available_slots - needed_slots - 2; // and update the remaining slots
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
		auto ptr_index = reinterpret_cast<const element_type*>(ptr) - data_.data(); // ptr_index is the slot index of the pointer
		auto allocated_slots = -data_[ptr_index - 1]; // the size of the allocation
		assert(allocated_slots > 0 && (data_[ptr_index + allocated_slots] == -allocated_slots)); // check the indicators are coherent
		data_[ptr_index - 1] = data_[ptr_index + allocated_slots] = allocated_slots; // release the allocation

		if constexpr (UseDummy)
			for (auto i = ptr_index; i < ptr_index + allocated_slots; ++i)
				data_[i] = Dummy;

		if (ptr_index != 1) // the allocation was not the first one, so check if the previous allocation is also free
		{
			const auto previous_allocation_slots = data_[ptr_index - 2];
			if (previous_allocation_slots > 0) // the previous allocation if free, merge the two
			{
				const auto combined_slots = allocated_slots + previous_allocation_slots + 2;
				data_[ptr_index - 3 - previous_allocation_slots] = data_[ptr_index + allocated_slots] = combined_slots;
				if constexpr (UseDummy)
					data_[ptr_index - 1] = data_[ptr_index - 2] = Dummy;

				// update variable in case there is also a free allocation after the current one
				allocated_slots = combined_slots;
				ptr_index -= previous_allocation_slots + 2;
			}
		}

		if (static_cast<size_type>(ptr_index + allocated_slots + 1) < Size - 2) // the allocation was not the last one, check if next if also free
		{
			const auto next_allocation_slots = data_[ptr_index + allocated_slots + 1];
			if (next_allocation_slots > 0) // next slot is free, merge the two
			{
				const auto combined_slots = allocated_slots + next_allocation_slots + 2;
				data_[ptr_index - 1] = data_[ptr_index + combined_slots] = combined_slots;
				if constexpr (UseDummy)
					data_[ptr_index + allocated_slots] = data_[ptr_index + allocated_slots + 1] = Dummy;
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
		assert(reinterpret_cast<unsigned int>(ptr) % std::alignment_of<element_type>::value == 0); // the pointer is necessary aligned with the slots
		const auto index = reinterpret_cast<const element_type*>(ptr) - data_.data(); // gets the pointer difference between data start and ptr
		return index > 0 && index < static_cast<ptrdiff_t>(Size - 1); // ptr in owned if between the allocator boundaries minus indicators
	}

	/**
	 * Gets the size of the allocator, which is the size of the biggest slot that can be allocated
	 * @return The size of the allocator, which is the size of the biggest slot that can be allocated
	 */
	constexpr size_type size() const
	{
		return (Size - 2) * sizeof(element_type);
	}

	/**
	 * Gets the currently available free memory for allocation
	 * @return The currently available free memory for allocation
	 */
	constexpr size_type available() const
	{
		const auto last_slot = data_[Size - 1];
		return last_slot > 0 ? last_slot * sizeof(element_type) : 0;
	}

	/**
	 * Checks whether the allocator is empty (no active allocation)
	 * @return @c true if the allocator is empty, @c false otherwise
	 */
	constexpr bool empty() const
	{
		return available() == size();
	}

	/**
	 * Runs some coherence checks on the allocator and its content
	 * @return @c true if the memory is coherent (no error found), @c false otherwise
	 */
	constexpr bool run_check() const
	{
		auto start_indicator = 0; // start at the beginning of the data

		while (true)
		{
			const auto size = data_[start_indicator]; // gets the size of the first space
			const auto end_indicator_index = static_cast<size_type>(start_indicator + 1 + std::abs(size)); // position of the end of chunk indicator

			if ((end_indicator_index > Size - 1) || data_[end_indicator_index] != size) // the allocation points outside of the data or the end indicator has a different value
				return false;

			if constexpr (UseDummy)
				if (size > 0) // chunk was not allocated, verify it contains dummy value
					for (auto i = start_indicator + 1U; i < end_indicator_index; ++i)
						if (data_[i] != Dummy)
							return false;

			start_indicator = end_indicator_index + 1;

			if (start_indicator == Size) // reached the end of the data
				return true;
		}
	}

private:

	std::array<element_type, Size> data_;
};

}
