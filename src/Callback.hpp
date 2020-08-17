/**
 ******************************************************************************
 * @file    Callback.hpp
 * @author  EZNOV SAS
 * @version V0.1
 * @date    01-March-2019
 * @brief   Replacement for @c std::function
 *
 * 		 Compared to @c std::function, @c Callback does not:
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
#include <cstddef>
#include <utility>
#include <new>
#include <optional>

namespace opsy
{
/**
 * Defines the default storage size for callback data
 */
static constexpr std::size_t kDefaultCallbackStorageSize = 3;

/**
 * @brief The base definition of a @c Callback, which will be specialized by @c Callback<ReturnType(Arguments...), StorageSize> for concrete implementation
 */
template<typename, std::size_t StorageSize = kDefaultCallbackStorageSize>
class Callback;

enum callback_validity_t
{
	Invalid = 0, ValidNoDestructor, ValidDestructor,
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
class Callback<ReturnType(Arguments...), StorageSize>
{
    template<typename, std::size_t>
    friend class Callback;

public:

	/**
	 * Creates an empty @c Callback.
	 */
	constexpr Callback() = default;

	/**
	 * Creates a @c Callback executing a function
	 * @param function The function to execute when the @c Callback is called
	 * @tparam Function The type of the function
	 */
	template<typename Function>
	Callback(Function&& function) :
			m_valid(std::is_destructible<Function>::value && !std::is_trivially_destructible<Function>::value ? ValidDestructor : ValidNoDestructor)
	{
		static_assert(sizeof(CallbackImpl<Function>) < FullSize, "Cannot store the invokable in the callback");
		static_assert(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<Function>>, Callback>, "Do not wrap Callback in a Callback, you probably meant to move it");

		new (&m_storage) CallbackImpl<Function>(std::forward<Function>(function));
	}

	Callback(const Callback&) = delete;
	Callback& operator=(const Callback&) = delete;

	/**
	 * Creates a @c Callback by moving data from another @c Callback
	 * @param from The @c Callback to move data from
	 */
    template<std::size_t FromSize>
	constexpr Callback(Callback<ReturnType(Arguments...), FromSize>&& from) :
			m_valid(from.m_valid)
	{
        static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");        
        *reinterpret_cast<typename Callback<ReturnType(Arguments...), FromSize>::Storage*>(&m_storage) = from.m_storage; // copy only necessary data
		from.m_valid = Invalid;
	}

	/**
	 * Assigns @c Callback from another @c Callback by moving data
	 * @param from The @c Callback to move from
	 */
    template<std::size_t FromSize>
	constexpr Callback& operator=(Callback<ReturnType(Arguments...), FromSize>&& from)
	{
        static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");

		if (m_valid == ValidDestructor)
			delete get();

		m_valid = from.m_valid;
		if (m_valid != Invalid)
			*reinterpret_cast<typename Callback<ReturnType(Arguments...), FromSize>::Storage*>(&m_storage) = from.m_storage; // copy only necessary data
		from.m_valid = Invalid;
		return *this;
	}

	/**
	 * Assigns @c Callback from a function
	 * @param function The function to encapsulate
	 */
	template<typename Function>
	constexpr Callback& operator=(Function&& function)
	{
		static_assert(sizeof(CallbackImpl<Function>) < FullSize, "Cannot store the invokable in the callback");
		static_assert(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<Function>>, Callback>, "Do not wrap Callback in a Callback, you probably meant to move it");

		m_valid = (std::is_destructible<Function>::value && !std::is_trivially_destructible<Function>::value) ? ValidDestructor : ValidNoDestructor;
		new (&m_storage) CallbackImpl<Function>(std::forward<Function>(function));
		return *this;
	}

	/**
	 * Calls the @c Callback function if it contains one
	 * @tparam The arguments types
	 * @param args The arguments to pass to the function
	 * @return If the @c Callback is empty and its return type is void, does nothing, otherwise return @c nullopt, if the @c Callback is not empty, execute the function and return its result
	 * @remark This is the @c const version of the operator
	 */
	template<typename ... Args>
	auto operator()(Args&&... args) const
	{
		if constexpr(std::is_void_v<ReturnType>)
		{
			if(m_valid != Invalid)
			get()->apply(std::forward<Arguments>(args)...);
		}
		else
		{
			if(m_valid != Invalid)
			return std::optional<ReturnType>
			{	get()->apply(std::forward<Arguments>(args)...)};
			else
			return std::optional<ReturnType>
			{	std::nullopt};
		}
	}

	/**
	 * Calls the @c Callback function if it contains one
	 * @tparam The arguments types
	 * @param args The arguments to pass to the function
	 * @return If the @c Callback is empty and its return type is void, does nothing, otherwise return @c nullopt, if the @c Callback is not empty, execute the function and return its result
	 * @remark This is the non @c const version of the operator
	 */
	template<typename ... Args>
	auto operator()(Args&&... args)
	{
		if constexpr(std::is_void_v<ReturnType>)
		{
			if(m_valid != Invalid)
			get()->apply(std::forward<Arguments>(args)...);
		}
		else
		{
			if(m_valid != Invalid)
			return std::optional<ReturnType>
			{	get()->apply(std::forward<Arguments>(args)...)};
			else
			return std::optional<ReturnType>
			{	std::nullopt};
		}
	}

	/**
	 * Checks if the @c Callback is valid (contains an actual function)
	 * @return @c true if the @c Callback contains a function, @c false if it is empty
	 */
	constexpr operator bool() const
	{
		return m_valid != Invalid;
	}

	~Callback()
	{
	if (m_valid == ValidDestructor)
		delete get();
	}

	private:

	class ICallback
	{
	public:
		virtual ReturnType apply(Arguments&&... arguments) const = 0;
		virtual ReturnType apply(Arguments&&... arguments) = 0;
		virtual ~ICallback() = default;
	};

	static constexpr std::size_t FullSize = sizeof(ICallback) + StorageSize * sizeof(void*);
	using Storage = typename std::aligned_storage<FullSize, std::alignment_of<ICallback>::value>::type;

	template<typename Function>
	class CallbackImpl: ICallback
	{

	public:

		constexpr explicit CallbackImpl(Function&& func) :
				m_function(std::forward<Function>(func))
		{

		}

		ReturnType apply(Arguments&&... args) const final
		{
			return m_function(std::forward<Arguments>(args)...);
		}

		ReturnType apply(Arguments&&... args) final
		{
			return m_function(std::forward<Arguments>(args)...);
		}

		~CallbackImpl() final = default;

	private:
		Function m_function;
	};

	constexpr inline ICallback* get()
	{
		return reinterpret_cast<ICallback*>(&m_storage);
	}

	constexpr inline const ICallback* get() const
	{
		return reinterpret_cast<const ICallback*>(&m_storage);
	}

	callback_validity_t m_valid{ Invalid };
	Storage m_storage{ };
};
}
