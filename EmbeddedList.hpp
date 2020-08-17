/**
 ******************************************************************************
 * @file    EmbeddedList.hpp
 * @author  EZNOV SAS
 * @version V0.1
 * @date    01-March-2019
 * @brief   A container that uses CRTP (Curiously Recurring Template Pattern)
 * 			to put forward and backward pointers inside the item itself.
 *
 * 			To use it, the contained data must inherit from EmbeddedNode with
 * 			the @c Item template parameter set to itself, i.e. :
 * 			class MyClass : EmbeddedNode<MyClass>
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
 * @copyright Copyright 2019 EZNOV SAS under the MIT License
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
 * @see https://www.opsy.tech
 * @see https://github.com/eznovsas/OpSy
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
 * @brief The base node class for the @c EmbeddedList contained
 * @tparam Item The @c Item type the @c EmbeddedNode point to
 * @remark For a class to be used with @c EmbeddedList, it must inherit from @c EmbeddedNode of itself (CRTP)
 * @remark It has no public interface, it only contains two pointer (forward and backward) to @c Item, that are used by @c EmbeddedList
 */
template<typename Item>
class EmbeddedNode
{
	template<typename I, typename N>
	friend class EmbeddedIterator;
	template<typename I, typename N>
	friend class EmbeddedConstIterator;

private:

	Item* m_previous = nullptr;
	Item* m_next = nullptr;
};

/**
 * @brief The @c EmbeddedList iterator, used to iterate over list items
 */
template<typename Item, typename Interface = Item>
class EmbeddedIterator
{
	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<EmbeddedNode<Item>, Interface>::value, "Interface must inherit (CRTP) from EmbeddedIterator<Item>");

	template<typename I, typename If>
	friend class EmbeddedList;

public:

	/**
	 * @brief The @c EmbeddedIterator type itself
	 */
	using self_type = EmbeddedIterator;

	/**
	 * @brief The value the @c EmbeddedIterator holds
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
	 * @brief The type of a @c EmbeddedIterator difference
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
	constexpr explicit EmbeddedIterator(pointer ptr = nullptr) :
			m_ptr(ptr)
	{

	}

	/**
	 * @brief Increment the @c EmbeddedIterator (move forward)
	 * @return The new value
	 */
	inline self_type operator++()
	{
		return self_type(m_ptr = next());
	}

	/**
	 * @brief Decrement the @c EmbeddedIterator (move backward)
	 * @return
	 */
	inline self_type operator--()
	{
		return m_ptr = previous();
	}

	/**
	 * @brief Dereference the @c EmbeddedIterator
	 * @return The @c Item the @c EmbeddedIterator points to
	 */
	constexpr inline reference operator*() const
	{
		return *m_ptr;
	}

	/**
	 * @brief Compares to another @c EmbeddedIterator
	 * @param other The other @c EmbeddedIterator
	 * @return @c false if the two @c EmbeddedIterator points to the same object, @c true otherwise
	 */
	constexpr bool operator!=(EmbeddedIterator other) const
	{
		return m_ptr != other.m_ptr;
	}

	/**
	 * @brief Compares to another @c EmbeddedIterator
	 * @param other The other @c EmbeddedIterator
	 * @return @c true if the two @c EmbeddedIterator points to the same object, @c false otherwise
	 */
	constexpr bool operator==(EmbeddedIterator other) const
	{
		return m_ptr == other.m_ptr;
	}

	/**
	 * @brief Access the @c Item pointed to via dereference
	 * @return The pointer the @c EmbeddedIterator points to
	 */
	constexpr pointer operator->() const
	{
		return m_ptr;
	}

private:

	inline void reset() const
	{
		assert(m_ptr != nullptr);
		m_ptr->Interface::m_previous = m_ptr->Interface::m_next = nullptr;
	}

	inline void next(pointer ptr)
	{
		assert(m_ptr != nullptr);
		m_ptr->Interface::m_next = ptr;
	}

	inline void previous(pointer ptr)
	{
		assert(m_ptr != nullptr);
		m_ptr->Interface::m_previous = ptr;
	}

	inline pointer next() const
	{
		assert(m_ptr != nullptr);
		return m_ptr->Interface::m_next;
	}

	inline pointer previous() const
	{
		assert(m_ptr != nullptr);
		return m_ptr->Interface::m_previous;
	}

	bool is_free() const
	{
		assert(m_ptr != nullptr);
		return previous() == nullptr && next() == nullptr;
	}

	Item* ptr()
	{
		return m_ptr;
	}

	Item* m_ptr;
};

/**
 * @brief The @c EmbeddedList constant iterator, used to iterator over list items in constant (non modifiable) context
 */
template<typename Item, typename Interface = Item>
class EmbeddedConstIterator
{

	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<EmbeddedNode<Item>, Interface>::value, "Interface must inherit (CRTP) from EmbeddedIterator<Item>");

	template<typename I, typename If>
	friend class EmbeddedList;

public:

	/**
	 * @brief The @c EmbeddedConstIterator type itself
	 */
	using self_type = EmbeddedConstIterator;

	/**
	 * @brief The value the @c EmbeddedConstIterator holds
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
	 * @brief The type of a @c EmbeddedConstIterator difference
	 */
	using difference_type = int32_t;

	/**
	 * @brief The category of the iterator, it is a bidirectional iterator (forward and backward)
	 */
	using iterator_category = std::bidirectional_iterator_tag;

	/**
	 * @brief Creates a @c EmbeddedConstIterator from a pointer
	 * @param ptr The pointer this @c EmbeddedConstIterator points to
	 */
	constexpr explicit EmbeddedConstIterator(pointer ptr = nullptr) :
			m_ptr(ptr)
	{

	}

	/**
	 * @brief Increment the @c EmbeddedConstIterator (move forward)
	 * @return The new value
	 */
	inline self_type operator++()
	{
		assert(m_ptr != nullptr);
		return m_ptr = m_ptr->Interface::m_next;
	}

	/**
	 * @brief Decrement the @c EmbeddedConstIterator (move backward)
	 * @return The new value
	 */
	inline self_type operator--()
	{
		assert(m_ptr != nullptr);
		return m_ptr = m_ptr->Interface::m_previous;
	}

	/**
	 * @brief Dereference the @c EmbeddedConstIterator
	 * @return The @c Item the @c EmbeddedConstIterator points to
	 */
	constexpr reference operator*() const
	{
		assert(m_ptr != nullptr);
		return *m_ptr;
	}

	/**
	 * @brief Compares to another @c EmbeddedConstIterator
	 * @param other The other @c EmbeddedConstIterator
	 * @return @c false if the two @c EmbeddedConstIterator points to the same object, @c true otherwise
	 */
	constexpr bool operator!=(EmbeddedConstIterator other) const
	{
		return m_ptr != other.m_ptr;
	}

	/**
	 * @brief Compares to another @c EmbeddedConstIterator
	 * @param other The other @c EmbeddedConstIterator
	 * @return @c true if the two @c EmbeddedConstIterator points to the same object, @c false otherwise
	 */
	constexpr bool operator==(EmbeddedConstIterator other) const
	{
		return m_ptr != other.m_ptr;
	}

	/**
	 * @brief Access the @c Item pointed to via dereference
	 * @return The pointer the @c EmbeddedConstIterator points to
	 */
	constexpr pointer operator->() const
	{
		return m_ptr;
	}

private:
	const Item* m_ptr;
};

/**
 * @brief A linked list container that uses the contained item to store forward and backward pointers
 * @tparam Item The contained item type, which must inherit @c EmbeddedNode of itself
 * @tparam Interface In case of multiple inheritance, the interface to use to access forward and backward pointer. By default, the @c Item type itself
 * @remark It is not copy constructible not copy assignable, but it is move constructible and move assignable
 */
template<class Item, class Interface = Item>
class EmbeddedList
{
	static_assert(std::is_base_of<Interface, Item>::value, "Item must inherit from Interface");
	static_assert(std::is_base_of<EmbeddedNode<Item>, Interface>::value, "Interface must inherit (CRTP) from EmbeddedIterator<Item>");

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
	using iterator = EmbeddedIterator<Item, Interface>;

	/**
	 * @brief The constant iterator type of the container
	 */
	using const_iterator = EmbeddedConstIterator<Item, Interface>;

	/**
	 * @brief The iterator difference type
	 */
	using difference_type = typename EmbeddedIterator<Item, Interface>::difference_type;

	/**
	 * @brief A comparator type, used for @c insertWhen
	 */
	using comparator = bool(*)(const_reference, const_reference);


	/**
	 * @brief The type of the @c size
	 */
	using size_type = uint32_t;

	EmbeddedList(const EmbeddedList&) = delete;
	EmbeddedList& operator=(const EmbeddedList&) = delete;

	/**
	 * @brief Creates an empty @c EmbeddedList
	 */
	constexpr EmbeddedList() = default;

	/**
	 * @brief Move construct a @c EmbeddedList from another
	 * @param other The other @c EmbeddedList to move data from
	 */
	explicit EmbeddedList(EmbeddedList&& other) :
			m_first(other.m_first), m_size(other.m_size)
	{
		other.m_first = nullptr;
		other.m_size = 0;
	}

	/**
	 * @brief Move assign a @c EmbeddedList from another
	 * @param other The other @c EmbeddedList to move data from
	 * @return This @c EmbeddedList
	 */
	constexpr EmbeddedList& operator=(EmbeddedList&& other)
	{
		m_first = other.m_first;
		m_size = other.m_size;
		other.m_first = nullptr;
		other.m_size = 0;
		return *this;
	}

	/**
	 * @brief Checks if this @c EmbeddedList is empty
	 * @return
	 */
	constexpr inline bool empty() const
	{
		assert((m_first == nullptr) ^ (m_size != 0));
		return m_first == nullptr;
	}

	/**
	 * @brief Clears this @c EmbeddedList (remove all @c Item)
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

		m_first = nullptr;
		m_size = 0;
	}

	/**
	 * @brief Gets an @c EmbeddedIterator to the beginning of this @c EmbeddedList
	 * @return An @c EmbeddedIterator to the beginning of this @c EmbeddedList
	 */
	constexpr inline iterator begin()
	{
		return iterator(m_first);
	}

	/**
	 * @brief Gets an @c EmbeddedConstIterator to the beginning of this @c EmbeddedList
	 * @return An @c EmbeddedConstIterator to the beginning of this @c EmbeddedList
	 */
	constexpr const inline const_iterator begin() const
	{
		return m_first;
	}

	/**
	 * @brief Gets an @c EmbeddedIterator to the end of this @c EmbeddedList
	 * @return An @c EmbeddedIterator to the end of this @c EmbeddedList
	 */
	constexpr inline iterator end()
	{
		return iterator();
	}

	/**
	 * @brief Gets an @c EmbeddedConstIterator to the end of this @c EmbeddedList
	 * @return An @c EmbeddedConstIterator to the end of this @c EmbeddedList
	 */
	constexpr inline const_iterator end() const
	{
		return const_iterator();
	}

	/**
	 * @brief Gets an @c EmbeddedIterator to the end of this @c EmbeddedList
	 * @return An @c EmbeddedIterator to the end of this @c EmbeddedList
	 */
	constexpr inline const_iterator cbegin() const
	{
		return m_first;
	}

	/**
	 * @brief Gets an @c EmbeddedConstIterator to the end of this @c EmbeddedList
	 * @return An @c EmbeddedConstIterator to the end of this @c EmbeddedList
	 */
	constexpr inline const_iterator cend() const
	{
		return const_iterator();
	}

	/**
	 * @brief Compare this @c EmbeddedList to another @c EmbeddedList
	 * @param other The other @c EmbeddedList to compare to
	 * @return @c true if the two @c EmbeddedList are equal, @c false otherwise
	 */
	constexpr bool operator==(const EmbeddedList& other)
	{
		return this == &other;
	}

	/**
	 * @brief Compare this @c EmbeddedList to another @c EmbeddedList
	 * @param other The other @c EmbeddedList to compare to
	 * @return @c false if the two @c EmbeddedList are equal, @c true otherwise
	 */
	constexpr bool operator!=(const EmbeddedList& other)
	{
		return m_first == nullptr || m_first != other.m_first;
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
		return m_size;
	}

	/**
	 * @brief Add an @c Item to the beginning of the @c EmbeddedList
	 * @param item The @c Item to add to the beginning of the @c EmbeddedList
	 */
	void push_front(Item& item)
	{
		assert(iterator(&item).is_free());

		iterator(&item).next(m_first);

		if (!empty())
			begin().previous(&item);

		m_first = &item;
		++m_size;
	}

	/**
	 * @brief Removes the first @c Item from the @c EmbeddedList
	 */
	void pop_front()
	{
		assert(!empty());
		assert(iterator(m_first).previous() == nullptr);

		auto i = begin();

		m_first = i.next();
		i.reset();
		--m_size;

		if (!empty())
			begin().previous(nullptr);

	}

	/**
	 * @brief Gets the first @c Item in the @c EmbeddedList
	 * @return The first @c Item in the @c EmbeddedList
	 */
	Item& front()
	{
		assert(!empty());
		return *m_first;
	}

	/**
	 * @brief Removes an item from the @c EmbeddedList
	 * @param item The @c Item to remove from the @c EmbeddedList
	 * @return An @c EmbeddedIterator pointing to the @c Item that was just after the removed @c Item
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
				if (m_first == i.ptr())
				{
					m_first = nullptr;
					--m_size;
				}
				return end();
			}
			else // first but not last element in the list
			{
				m_first = i.next();
				i.next(nullptr);
				iterator(m_first).previous(nullptr);
				--m_size;
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
			--m_size;
			return iterator(next);
		}
	}

	/**
	 * @brief Inserts an @c Item in the @c EmbeddedList
	 * @param previous The @c EmbeddedIterator that points to the @c Item just before where the new @c Item is to be inserted
	 * @param item The @c Item to insert in the @c EmbeddedList
	 * @return An @c EmbeddedIterator pointing to the @c Item inserted
	 */
	iterator insert(iterator previous, Item& item)
	{
		assert(iterator(&item).is_free());

		++m_size;
		if (empty() || previous.ptr() == nullptr) // list is empty or previous is null (before the list)
		{
			push_front(item);
			return iterator(&item);
		}

		auto i = iterator(&item);
		auto next = previous.next();

		i.next(next); // prepare insertion
		i.previous(previous.ptr());

		previous.next(i.ptr()); // and stitch both sides
		if (next != nullptr)
			iterator(next).previous(i.ptr());

		return iterator(&item);
	}


	/**
	 * @brief Inserts an @c Item when a @p predicate becomes @c false
	 * @param predicate The @c comparator to use to compare the current node with the @c Item to insert
	 * @param item The @c Item to insert in the @c EmbeddedList
	 * @return An @c EmbeddedIterator pointing to the @c Item inserted
	 */
	iterator insertWhen(const comparator predicate, Item& item)
	{
		if (empty() || predicate(item, *m_first))
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

	Item* m_first = nullptr;
	size_type m_size = 0;

};

}
