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

#include <concepts>
#include <type_traits>
#include <cstddef>
#include <utility>
#include <new>
#include <optional>
#include <memory>

#include "opsy_assert.hpp"

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

namespace detail
{

/**
 * @brief What a @c callback does with the callable it holds, without knowing its type
 *
 *        One constant instance per callable type, in flash; a @c callback
 *        points at the one for what it holds, and at none when empty. It
 *        depends on the signature only, not on the storage size, so a
 *        callable moves between callbacks of different sizes unchanged.
 */
template<typename ReturnType, typename ... Arguments>
struct callback_operations
{
	ReturnType (*invoke)(const void* storage, Arguments&&... arguments);
	void (*relocate)(void* from, void* to); ///< move-constructs at @p to, then destroys at @p from
	void (*destroy)(void* storage);         ///< @c nullptr when the callable is trivially destructible
};

/**
 * @brief The operations for one callable type
 * @remark Each one finds the callable through @c std::launder at the address
 *         it was placement-new'd at, which is what makes the access defined:
 *         an object of exactly that type lives there.
 */
template<typename Function, typename ReturnType, typename ... Arguments>
struct callback_operations_for
{
	[[gnu::always_inline]] static Function& get(void* storage)
	{
		return *std::launder(static_cast<Function*>(storage));
	}

	[[gnu::always_inline]] static const Function& get(const void* storage)
	{
		return *std::launder(static_cast<const Function*>(storage));
	}

	static ReturnType invoke(const void* storage, Arguments&&... arguments)
	{
		return get(storage)(std::forward<Arguments>(arguments)...);
	}

	// static_cast and a destructor call rather than std::move and
	// std::destroy_at: the same thing, without one more function per callable
	// type in a build that does not inline.
	static void relocate(void* from, void* to)
	{
		Function& source = get(from);
		::new (to) Function(static_cast<Function&&>(source));
		source.~Function();
	}

	static void destroy(void* storage)
	{
		get(storage).~Function();
	}

	static constexpr callback_operations<ReturnType, Arguments...> table{
		&invoke, &relocate,
		std::is_trivially_destructible_v<Function> ? nullptr : &destroy };
};

}

/**
 * @brief A callback.
 * A callback is used as a way to defer code execution. For example, when you ask a peripheral to do something, you may want
 * the peripheral to execute code when it has finished, calling you back (hence the name) so that you can e.g. set a flag.
 * @tparam StorageSize The size available to store the callback data (in @c void* increment)
 * @tparam ReturnType The type of the return value (if none, use @c void)
 * @tparam Arguments The arguments of the callback (id none, use @c void)
 * @remark It is a replacement for @c std::function . The callable lives in
 *         the callback's own storage, constructed there by placement new:
 *         one that does not fit, or needs more alignment than a pointer, is
 *         a compile-time error, never a heap allocation.
 */
template<std::size_t StorageSize, typename ReturnType, typename ... Arguments>
class callback<ReturnType(Arguments...), StorageSize>
{
	template<typename, std::size_t>
	friend class callback;

	using operations = detail::callback_operations<ReturnType, Arguments...>;

public:

	/**
	 * Creates an empty @c callback.
	 */
	constexpr callback() = default;

	/**
	 * Creates a @c callback executing a function
	 * @param function The function to execute when the @c callback is called
	 * @tparam Function Any type whose @c const instances are invocable with
	 *         @c Arguments... (and not the
	 *         @c callback type itself, which would otherwise satisfy the
	 *         @c std::invocable constraint via @c operator()).
	 */
	template<typename Function>
		requires std::invocable<const std::decay_t<Function>&, Arguments...>
		      && (!std::same_as<std::remove_cvref_t<Function>, callback>)
	callback(Function&& function)
	{
		emplace(std::forward<Function>(function));
	}

	callback(const callback&) = delete;
	callback& operator=(const callback&) = delete;

	/**
	 * Creates a @c callback by moving the callable out of another @c callback
	 * @param from The @c callback to move from, left empty
	 */
	template<std::size_t FromSize>
	callback(callback<ReturnType(Arguments...), FromSize>&& from) noexcept
	{
		static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");
		take(from);
	}

	/**
	 * Assigns @c callback from another @c callback by moving its callable
	 * @param from The @c callback to move from, left empty
	 */
	template<std::size_t FromSize>
	callback& operator=(callback<ReturnType(Arguments...), FromSize>&& from) noexcept
	{
		static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");

		if (static_cast<const void*>(&from) != static_cast<const void*>(this))
		{
			reset();
			take(from);
		}
		return *this;
	}

	/**
	 * Assigns @c callback from a function
	 * @param function The function to encapsulate
	 * @tparam Function Any type whose @c const instances are invocable with
	 *         @c Arguments... (and not the
	 *         @c callback type itself, which would otherwise satisfy the
	 *         @c std::invocable constraint via @c operator()).
	 */
	template<typename Function>
		requires std::invocable<const std::decay_t<Function>&, Arguments...>
		      && (!std::same_as<std::remove_cvref_t<Function>, callback>)
	callback& operator=(Function&& function)
	{
		reset();
		emplace(std::forward<Function>(function));
		return *this;
	}

	/**
	 * Calls the @c callback function if it contains one
	 * @tparam The arguments types
	 * @param args The arguments to pass to the function
	 * @return If the @c callback is empty and its return type is void, does nothing, otherwise return @c nullopt, if the @c callback is not empty, execute the function and return its result
	 * @remark The callable is invoked as @c const , whether or not the
	 *         callback is: a mutable lambda is refused at construction.
	 */
	template<typename ... Args>
	auto operator()(Args&&... args) const
	{
		if constexpr(std::is_void_v<ReturnType>)
		{
			if (operations_ != nullptr)
				operations_->invoke(storage_, std::forward<Args>(args)...);
		}
		else
		{
			if (operations_ != nullptr)
				return std::optional<ReturnType>{ operations_->invoke(storage_, std::forward<Args>(args)...) };
			else
				return std::optional<ReturnType>{ std::nullopt };
		}
	}

	/**
	 * Checks if the @c callback is valid (contains an actual function)
	 * @return @c true if the @c callback contains a function, @c false if it is empty
	 */
	// explicit, like std::function's: without it a callback converts silently
	// to bool, so `int n = cb;` and `cb + 1` compile. Contextual conversion --
	// if (cb), !cb, cb && x -- still works.
	[[nodiscard]] constexpr explicit operator bool() const
	{
		return operations_ != nullptr;
	}

	~callback()
	{
		reset();
	}

private:

	template<typename Function>
	void emplace(Function&& function)
	{
		using stored = std::decay_t<Function>;
		static_assert(sizeof(stored) <= sizeof(storage_), "Cannot store the invokable in the callback");
		static_assert(alignof(stored) <= alignof(void*), "The invokable needs more alignment than the callback storage provides");
		static_assert(std::is_nothrow_move_constructible_v<stored> && std::is_nothrow_destructible_v<stored>,
			"The invokable is moved and destroyed by a callback's noexcept move and destructor");

		::new (static_cast<void*>(storage_)) stored(std::forward<Function>(function));
		operations_ = &detail::callback_operations_for<stored, ReturnType, Arguments...>::table;
	}

	template<std::size_t FromSize>
	void take(callback<ReturnType(Arguments...), FromSize>& from)
	{
		if (from.operations_ == nullptr)
			return;

		from.operations_->relocate(from.storage_, storage_);
		operations_ = from.operations_;
		from.operations_ = nullptr;
	}

	void reset()
	{
		if (operations_ != nullptr && operations_->destroy != nullptr)
			operations_->destroy(storage_);
		operations_ = nullptr;
	}

	const operations* operations_ = nullptr;
	alignas(void*) std::byte storage_[StorageSize * sizeof(void*)]{};
};
}
