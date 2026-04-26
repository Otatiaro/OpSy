/**
 ******************************************************************************
 * @file    embedded_list.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   A container that uses CRTP (Curiously Recurring Template Pattern)
 * 			to put forward and backward pointers inside the item itself.
 *
 * 			To use it, the contained data must inherit from embedded_node with
 * 			the @c Item template parameter set to itself, i.e. :
 * 			class MyClass : embedded_node<MyClass>
 * 			You can also use multiple inheritance and specify interface when
 * 			declaring the list type (which is by default the class itself).
 *
 * 			This allows to manipulate items with no memory allocations.
 * 			The main limitation is that an item can be in ONLY ONE list at a
 * 			given time (because it only has one pointer pair).
 * 			If you need to put items in multiple lists at the same time, use
 * 			multiple inheritance.
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

#include <type_traits>
#include <cstdint>
#include <iterator>
#include <limits>
#include <cassert>

namespace opsy
{

/**
 * @brief The base node class for the @c embedded_list contained
 * @tparam Item The @c Item type the @c embedded_node point to
 * @remark For a class to be used with @c embedded_list, it must inherit from @c embedded_node of itself (CRTP)
 * @remark It has no public interface, it only contains two pointer (forward and backward) to @c Item, that are used by @c embedded_list
 */
template<typename Item>
class embedded_node
{
	template<typename I, typename N>
	friend class embedded_iterator;
	template<typename I, typename N>
	friend class embedded_const_iterator;

private:

	Item* previous_ = nullptr;
	Item* next_ = nullptr;
};

/**
 * @brief The @c embedded_list iterator, used to iterate over list items
 */
template<typename Item, typename Interface = Item>
class embedded_iterator
{
	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<embedded_node<Item>, Interface>::value, "Interface must inherit (CRTP) from embedded_iterator<Item>");

	template<typename I, typename If>
	friend class embedded_list;

public:

	/**
	 * @brief The @c embedded_iterator type itself
	 */
	using self_type = embedded_iterator;

	/**
	 * @brief The value the @c embedded_iterator holds
	 */
	using value_type = Item;

	/**
	 * @brief A reference to a @c value_type
	 */
	using reference = Item&;

	/**
	 * @brief A pointer to a @c value_type
	 */
	using pointer = Item*;

	/**
	 * @brief The type of a @c embedded_iterator difference
	 */
	using difference_type = int32_t;

	/**
	 * @brief The category of the iterator, it is a bidirectional iterator (forward and backward)
	 */
	using iterator_category = std::bidirectional_iterator_tag;

	/**
	 * @ Creates an iterator pointing to an @c Item
	 * @param ptr The pointer to the @c Item
	 */
	constexpr explicit embedded_iterator(pointer ptr = nullptr) :
			ptr_(ptr)
	{

	}

	/**
	 * @brief Increment the @c embedded_iterator (move forward)
	 * @return The new value
	 */
	inline self_type operator++()
	{
		return self_type(ptr_ = next());
	}

	/**
	 * @brief Decrement the @c embedded_iterator (move backward)
	 * @return
	 */
	inline self_type operator--()
	{
		return self_type(ptr_ = previous());
	}

	/**
	 * @brief Dereference the @c embedded_iterator
	 * @return The @c Item the @c embedded_iterator points to
	 */
	constexpr inline reference operator*() const
	{
		return *ptr_;
	}

	/**
	 * @brief Compares to another @c embedded_iterator
	 * @param other The other @c embedded_iterator
	 * @return @c false if the two @c embedded_iterator points to the same object, @c true otherwise
	 */
	constexpr bool operator!=(embedded_iterator other) const
	{
		return ptr_ != other.ptr_;
	}

	/**
	 * @brief Compares to another @c embedded_iterator
	 * @param other The other @c embedded_iterator
	 * @return @c true if the two @c embedded_iterator points to the same object, @c false otherwise
	 */
	constexpr bool operator==(embedded_iterator other) const
	{
		return ptr_ == other.ptr_;
	}

	/**
	 * @brief Access the @c Item pointed to via dereference
	 * @return The pointer the @c embedded_iterator points to
	 */
	constexpr pointer operator->() const
	{
		return ptr_;
	}

private:

	inline void reset() const
	{
		assert(ptr_ != nullptr);
		ptr_->Interface::previous_ = ptr_->Interface::next_ = nullptr;
	}

	inline void next(pointer ptr)
	{
		assert(ptr_ != nullptr);
		ptr_->Interface::next_ = ptr;
	}

	inline void previous(pointer ptr)
	{
		assert(ptr_ != nullptr);
		ptr_->Interface::previous_ = ptr;
	}

	inline pointer next() const
	{
		assert(ptr_ != nullptr);
		return ptr_->Interface::next_;
	}

	inline pointer previous() const
	{
		assert(ptr_ != nullptr);
		return ptr_->Interface::previous_;
	}

	bool is_free() const
	{
		assert(ptr_ != nullptr);
		return previous() == nullptr && next() == nullptr;
	}

	Item* ptr()
	{
		return ptr_;
	}

	Item* ptr_;
};

/**
 * @brief The @c embedded_list constant iterator, used to iterator over list items in constant (non modifiable) context
 */
template<typename Item, typename Interface = Item>
class embedded_const_iterator
{

	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<embedded_node<Item>, Interface>::value, "Interface must inherit (CRTP) from embedded_iterator<Item>");

	template<typename I, typename If>
	friend class embedded_list;

public:

	/**
	 * @brief The @c embedded_const_iterator type itself
	 */
	using self_type = embedded_const_iterator;

	/**
	 * @brief The value the @c embedded_const_iterator holds
	 */
	using value_type = Item;

	/**
	 * @brief A const reference to a @c value_type
	 */
	using reference = const Item&;

	/**
	 * @brief A const pointer to a @c value_type
	 */
	using pointer = const Item*;

	/**
	 * @brief The type of a @c embedded_const_iterator difference
	 */
	using difference_type = int32_t;

	/**
	 * @brief The category of the iterator, it is a bidirectional iterator (forward and backward)
	 */
	using iterator_category = std::bidirectional_iterator_tag;

	/**
	 * @brief Creates a @c embedded_const_iterator from a pointer
	 * @param ptr The pointer this @c embedded_const_iterator points to
	 */
	constexpr explicit embedded_const_iterator(pointer ptr = nullptr) :
			ptr_(ptr)
	{

	}

	/**
	 * @brief Increment the @c embedded_const_iterator (move forward)
	 * @return The new value
	 */
	inline self_type operator++()
	{
		assert(ptr_ != nullptr);
		return ptr_ = ptr_->Interface::next_;
	}

	/**
	 * @brief Decrement the @c embedded_const_iterator (move backward)
	 * @return The new value
	 */
	inline self_type operator--()
	{
		assert(ptr_ != nullptr);
		return self_type(ptr_ = ptr_->Interface::previous_);
	}

	/**
	 * @brief Dereference the @c embedded_const_iterator
	 * @return The @c Item the @c embedded_const_iterator points to
	 */
	constexpr reference operator*() const
	{
		assert(ptr_ != nullptr);
		return *ptr_;
	}

	/**
	 * @brief Compares to another @c embedded_const_iterator
	 * @param other The other @c embedded_const_iterator
	 * @return @c false if the two @c embedded_const_iterator points to the same object, @c true otherwise
	 */
	constexpr bool operator!=(embedded_const_iterator other) const
	{
		return ptr_ != other.ptr_;
	}

	/**
	 * @brief Compares to another @c embedded_const_iterator
	 * @param other The other @c embedded_const_iterator
	 * @return @c true if the two @c embedded_const_iterator points to the same object, @c false otherwise
	 */
	constexpr bool operator==(embedded_const_iterator other) const
	{
		return ptr_ == other.ptr_;
	}

	/**
	 * @brief Access the @c Item pointed to via dereference
	 * @return The pointer the @c embedded_const_iterator points to
	 */
	constexpr pointer operator->() const
	{
		return ptr_;
	}

private:
	const Item* ptr_;
};

/**
 * @brief A linked list container that uses the contained item to store forward and backward pointers
 * @tparam Item The contained item type, which must inherit @c embedded_node of itself
 * @tparam Interface In case of multiple inheritance, the interface to use to access forward and backward pointer. By default, the @c Item type itself
 * @remark It is not copy constructible not copy assignable, but it is move constructible and move assignable
 */
template<class Item, class Interface = Item>
class embedded_list
{
	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<embedded_node<Item>, Interface>::value, "Interface must inherit (CRTP) from embedded_iterator<Item>");

public:

	/**
	 * @brief The type of the contained items
	 */
	using value_type = Item;

	/**
	 * @brief A reference to a contained item
	 */
	using reference = Item&;

	/**
	 * @brief A constant reference to a contained item
	 */
	using const_reference = const Item&;

	/**
	 * @brief The iterator type of the container
	 */
	using iterator = embedded_iterator<Item, Interface>;

	/**
	 * @brief The constant iterator type of the container
	 */
	using const_iterator = embedded_const_iterator<Item, Interface>;

	/**
	 * @brief The iterator difference type
	 */
	using difference_type = typename embedded_iterator<Item, Interface>::difference_type;

	/**
	 * @brief The type of the @c size
	 */
	using size_type = uint32_t;

	embedded_list(const embedded_list&) = delete;
	embedded_list& operator=(const embedded_list&) = delete;

	/**
	 * @brief Creates an empty @c embedded_list
	 */
	constexpr embedded_list() = default;

	/**
	 * @brief Move construct a @c embedded_list from another
	 * @param other The other @c embedded_list to move data from
	 */
	explicit embedded_list(embedded_list&& other) :
			first_(other.first_), size_(other.size_)
	{
		other.first_ = nullptr;
		other.size_ = 0;
	}

	/**
	 * @brief Move assign a @c embedded_list from another
	 * @param other The other @c embedded_list to move data from
	 * @return This @c embedded_list
	 */
	constexpr embedded_list& operator=(embedded_list&& other)
	{
		first_ = other.first_;
		size_ = other.size_;
		other.first_ = nullptr;
		other.size_ = 0;
		return *this;
	}

	/**
	 * @brief Checks if this @c embedded_list is empty
	 * @return
	 */
	constexpr inline bool empty() const
	{
		assert((first_ == nullptr) ^ (size_ != 0));
		return first_ == nullptr;
	}

	/**
	 * @brief Clears this @c embedded_list (remove all @c Item)
	 */
	void clear()
	{
		auto i = begin();

		while (i != end())
		{
			auto next = i.next();
			i.reset();
			i = next;
		}

		first_ = nullptr;
		size_ = 0;
	}

	/**
	 * @brief Gets an @c embedded_iterator to the beginning of this @c embedded_list
	 * @return An @c embedded_iterator to the beginning of this @c embedded_list
	 */
	constexpr inline iterator begin()
	{
		return iterator(first_);
	}

	/**
	 * @brief Gets an @c embedded_const_iterator to the beginning of this @c embedded_list
	 * @return An @c embedded_const_iterator to the beginning of this @c embedded_list
	 */
	constexpr const inline const_iterator begin() const
	{
		return first_;
	}

	/**
	 * @brief Gets an @c embedded_iterator to the end of this @c embedded_list
	 * @return An @c embedded_iterator to the end of this @c embedded_list
	 */
	constexpr inline iterator end()
	{
		return iterator();
	}

	/**
	 * @brief Gets an @c embedded_const_iterator to the end of this @c embedded_list
	 * @return An @c embedded_const_iterator to the end of this @c embedded_list
	 */
	constexpr inline const_iterator end() const
	{
		return const_iterator();
	}

	/**
	 * @brief Gets an @c embedded_iterator to the end of this @c embedded_list
	 * @return An @c embedded_iterator to the end of this @c embedded_list
	 */
	constexpr inline const_iterator cbegin() const
	{
		return first_;
	}

	/**
	 * @brief Gets an @c embedded_const_iterator to the end of this @c embedded_list
	 * @return An @c embedded_const_iterator to the end of this @c embedded_list
	 */
	constexpr inline const_iterator cend() const
	{
		return const_iterator();
	}

	/**
	 * @brief Compare this @c embedded_list to another @c embedded_list
	 * @param other The other @c embedded_list to compare to
	 * @return @c true if the two @c embedded_list are equal, @c false otherwise
	 */
	constexpr bool operator==(const embedded_list& other)
	{
		return this == &other;
	}

	/**
	 * @brief Compare this @c embedded_list to another @c embedded_list
	 * @param other The other @c embedded_list to compare to
	 * @return @c false if the two @c embedded_list are equal, @c true otherwise
	 */
	constexpr bool operator!=(const embedded_list& other)
	{
		return first_ == nullptr || first_ != other.first_;
	}

	/**
	 * @brief Gets the max @c size this list can hold
	 * @return The max @c size this list can hold
	 */
	constexpr size_type max_size() const
	{
		return std::numeric_limits<difference_type>::max();
	}

	/**
	 * @brief Gets the current size of this list (number of @c Item)
	 * @return The current size of this list (number of @c Item)
	 */
	constexpr size_type size() const
	{
		return size_;
	}

	/**
	 * @brief Add an @c Item to the beginning of the @c embedded_list
	 * @param item The @c Item to add to the beginning of the @c embedded_list
	 */
	void push_front(Item& item)
	{
		assert(iterator(&item).is_free());

		iterator(&item).next(first_);

		if (!empty())
			begin().previous(&item);

		first_ = &item;
		++size_;
	}

	/**
	 * @brief Removes the first @c Item from the @c embedded_list
	 */
	void pop_front()
	{
		assert(!empty());
		assert(iterator(first_).previous() == nullptr);

		auto i = begin();

		first_ = i.next();
		i.reset();
		--size_;

		if (!empty())
			begin().previous(nullptr);

	}

	/**
	 * @brief Gets the first @c Item in the @c embedded_list
	 * @return The first @c Item in the @c embedded_list
	 */
	Item& front()
	{
		assert(!empty());
		return *first_;
	}

	/**
	 * @brief Removes an item from the @c embedded_list
	 * @param item The @c Item to remove from the @c embedded_list
	 * @return An @c embedded_iterator pointing to the @c Item that was just after the removed @c Item
	 */
	iterator erase(Item& item)
	{
		auto i = iterator(&item);

		if (empty())
			return end();

		if (i.previous() == nullptr) // no previous element, first and / or only element
		{
			if (i.next() == nullptr) // only element or not in the list
			{
				if (first_ == i.ptr())
				{
					first_ = nullptr;
					--size_;
				}
				return end();
			}
			else // first but not last element in the list
			{
				first_ = i.next();
				i.next(nullptr);
				iterator(first_).previous(nullptr);
				--size_;
				return begin();
			}
		}
		else // previous is not null, assume element is "somewhere" in the list not in front, problem will appear later if we removed an item from another list ...
		{
			auto next = i.next();
			iterator(i.previous()).next(next); // stitch first side
			if (next != nullptr)
				iterator(next).previous(i.previous()); // stitch other side
			i.reset();
			--size_;
			return iterator(next);
		}
	}

	/**
	 * @brief Inserts an @c Item in the @c embedded_list
	 * @param previous The @c embedded_iterator that points to the @c Item just before where the new @c Item is to be inserted
	 * @param item The @c Item to insert in the @c embedded_list
	 * @return An @c embedded_iterator pointing to the @c Item inserted
	 */
	iterator insert(iterator previous, Item& item)
	{
		assert(iterator(&item).is_free());

		if (empty() || previous.ptr() == nullptr) // list is empty or previous is null (before the list)
		{
			push_front(item); // push_front manages size_ itself
			return iterator(&item);
		}

		auto i = iterator(&item);
		auto next = previous.next();

		i.next(next); // prepare insertion
		i.previous(previous.ptr());

		previous.next(i.ptr()); // and stitch both sides
		if (next != nullptr)
			iterator(next).previous(i.ptr());

		++size_;
		return iterator(&item);
	}


	/**
	 * @brief Inserts an @c Item when a @p predicate becomes @c false
	 * @tparam Comparator Any callable with signature @c bool(const_reference, const_reference)
	 * @param predicate The predicate to use to compare the current node with the @c Item to insert
	 * @param item The @c Item to insert in the @c embedded_list
	 * @return An @c embedded_iterator pointing to the @c Item inserted
	 */
	template<std::invocable<const_reference, const_reference> Comparator>
	iterator insert_when(Comparator&& predicate, Item& item)
	{
		if (empty() || predicate(item, *first_))
		{
			push_front(item);
			return begin();
		}
		else
		{
			iterator previous = begin();
			iterator current = iterator(previous.next());

			while ((current != end()) && (predicate(item, *current) == false))
			{
				previous = current;
				++current;
			}

			return insert(previous, item);
		}

	}

private:

	Item* first_ = nullptr;
	size_type size_ = 0;

};

}
