/**
 ******************************************************************************
 * @file    callback.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Replacement for @c std::function
 *
 * 		 Compared to @c std::function, @c callback does not:
 * 		  - allocate any memory on the heap
 * 		    it does not compile if the storage is not big enough
 * 		    to contain the required data
 * 		    the storage size is a template parameter with default value
 * 		  - throw exception
 * 		    if the empty callback has no return value, calling does nothing
 * 		    if the callback target has return value, the callback returns
 * 		    an @c std::optional<ReturnType> and empty callback returns @c nullopt
 * 		  - allow copy, only moves are allowed
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
#include <cstddef>
#include <utility>
#include <new>
#include <optional>
#include <memory>

namespace opsy
{
/**
 * Defines the default storage size for callback data
 */
static constexpr std::size_t default_callback_storage_size = 3;

/**
 * @brief The base definition of a @c callback, which will be specialized by @c callback<ReturnType(Arguments...), StorageSize> for concrete implementation
 */
template<typename, std::size_t StorageSize = default_callback_storage_size>
class callback;

enum callback_validity_t
{
	invalid = 0, valid_no_destructor, valid_destructor,
};

/**
 * @brief A callback.
 * A callback is used as a way to defer code execution. For example, when you ask a peripheral to do something, you may want
 * the peripheral to execute code when it has finished, calling you back (hence the name) so that you can e.g. set a flag.
 * @tparam StorageSize The size available to store the callback data (in @c void* increment)
 * @tparam ReturnType The type of the return value (if none, use @c void)
 * @tparam Arguments The arguments of the callback (id none, use @c void)
 * @remark It is a replacement for @c std::function
 */
template<std::size_t StorageSize, typename ReturnType, typename ... Arguments>
class callback<ReturnType(Arguments...), StorageSize>
{
    template<typename, std::size_t>
    friend class callback;

public:

	/**
	 * Creates an empty @c callback.
	 */
	constexpr callback() = default;

	/**
	 * Creates a @c callback executing a function
	 * @param function The function to execute when the @c callback is called
	 * @tparam Function The type of the function
	 */
	template<typename Function>
	callback(Function&& function) :
			valid_(std::is_destructible_v<std::decay_t<Function>> && !std::is_trivially_destructible_v<std::decay_t<Function>> ? valid_destructor : valid_no_destructor)
	{
		static_assert(sizeof(callback_impl<std::decay_t<Function>>) < FullSize, "Cannot store the invokable in the callback");
		static_assert(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<Function>>, callback>, "Do not wrap callback in a callback, you probably meant to move it");

		new (&storage_) callback_impl<std::decay_t<Function>>(std::forward<Function>(function));
	}

	callback(const callback&) = delete;
	callback& operator=(const callback&) = delete;

	/**
	 * Creates a @c callback by moving data from another @c callback
	 * @param from The @c callback to move data from
	 */
    template<std::size_t FromSize>
	constexpr callback(callback<ReturnType(Arguments...), FromSize>&& from) :
			valid_(from.valid_)
	{
        static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");        
        *reinterpret_cast<typename callback<ReturnType(Arguments...), FromSize>::storage*>(&storage_) = from.storage_; // copy only necessary data
		from.valid_ = invalid;
	}

	/**
	 * Assigns @c callback from another @c callback by moving data
	 * @param from The @c callback to move from
	 */
    template<std::size_t FromSize>
	constexpr callback& operator=(callback<ReturnType(Arguments...), FromSize>&& from)
	{
        static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");

		if (valid_ == valid_destructor)
			std::destroy_at(get());

		valid_ = from.valid_;
		if (valid_ != invalid)
			*reinterpret_cast<typename callback<ReturnType(Arguments...), FromSize>::storage*>(&storage_) = from.storage_; // copy only necessary data
		from.valid_ = invalid;
		return *this;
	}

	/**
	 * Assigns @c callback from a function
	 * @param function The function to encapsulate
	 */
	template<typename Function>
	constexpr callback& operator=(Function&& function)
	{
		static_assert(sizeof(callback_impl<std::decay_t<Function>>) < FullSize, "Cannot store the invokable in the callback");
		static_assert(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<Function>>, callback>, "Do not wrap callback in a callback, you probably meant to move it");

		valid_ = (std::is_destructible_v<std::decay_t<Function>> && !std::is_trivially_destructible_v<std::decay_t<Function>>) ? valid_destructor : valid_no_destructor;
		new (&storage_) callback_impl<std::decay_t<Function>>(std::forward<Function>(function));
		return *this;
	}

	/**
	 * Calls the @c callback function if it contains one
	 * @tparam The arguments types
	 * @param args The arguments to pass to the function
	 * @return If the @c callback is empty and its return type is void, does nothing, otherwise return @c nullopt, if the @c callback is not empty, execute the function and return its result
	 * @remark This is the @c const version of the operator
	 */
	template<typename ... Args>
	auto operator()(Args&&... args) const
	{
		if constexpr(std::is_void_v<ReturnType>)
		{
			if(valid_ != invalid)
			get()->apply(std::forward<Args>(args)...);
		}
		else
		{
			if(valid_ != invalid)
			return std::optional<ReturnType>
			{	get()->apply(std::forward<Args>(args)...)};
			else
			return std::optional<ReturnType>
			{	std::nullopt};
		}
	}

	/**
	 * Calls the @c callback function if it contains one
	 * @tparam The arguments types
	 * @param args The arguments to pass to the function
	 * @return If the @c callback is empty and its return type is void, does nothing, otherwise return @c nullopt, if the @c callback is not empty, execute the function and return its result
	 * @remark This is the non @c const version of the operator
	 */
	template<typename ... Args>
	auto operator()(Args&&... args)
	{
		if constexpr(std::is_void_v<ReturnType>)
		{
			if(valid_ != invalid)
			get()->apply(std::forward<Args>(args)...);
		}
		else
		{
			if(valid_ != invalid)
			return std::optional<ReturnType>
			{	get()->apply(std::forward<Args>(args)...)};
			else
			return std::optional<ReturnType>
			{	std::nullopt};
		}
	}

	/**
	 * Checks if the @c callback is valid (contains an actual function)
	 * @return @c true if the @c callback contains a function, @c false if it is empty
	 */
	constexpr operator bool() const
	{
		return valid_ != invalid;
	}

	~callback()
	{
	if (valid_ == valid_destructor)
		std::destroy_at(get());
	}

	private:

	class i_callback
	{
	public:
		virtual ReturnType apply(Arguments&&... arguments) const = 0;
		virtual ReturnType apply(Arguments&&... arguments) = 0;
		virtual ~i_callback() = default;
	};

	static constexpr std::size_t FullSize = sizeof(i_callback) + StorageSize * sizeof(void*);
	using storage = struct alignas(std::alignment_of_v<i_callback>) { std::byte data[FullSize]; };

	template<typename Function>
	class callback_impl: i_callback
	{

	public:

		constexpr explicit callback_impl(Function&& func) :
				function_(std::forward<Function>(func))
		{

		}

		ReturnType apply(Arguments&&... args) const final
		{
			return function_(std::forward<Arguments>(args)...);
		}

		ReturnType apply(Arguments&&... args) final
		{
			return function_(std::forward<Arguments>(args)...);
		}

		~callback_impl() final = default;

	private:
		Function function_;
	};

	constexpr inline i_callback* get()
	{
		return reinterpret_cast<i_callback*>(&storage_);
	}

	constexpr inline const i_callback* get() const
	{
		return reinterpret_cast<const i_callback*>(&storage_);
	}

	callback_validity_t valid_{ invalid };
	storage storage_{ };
};
}
