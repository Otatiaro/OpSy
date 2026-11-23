/**
 ******************************************************************************
 * @file    callback.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Move-only callable wrapper with the interface of
 *          @c std::move_only_function , holding its target inline
 *
 * 		 What differs from @c std::move_only_function :
 * 		  - the target lives in the callback's own storage, whose size is
 * 		    a template parameter; one that does not fit, or needs more
 * 		    alignment than a pointer, does not compile -- nothing is ever
 * 		    allocated;
 * 		  - moving and destroying the target must not throw, so that a
 * 		    callback moves without throwing too.
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

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "opsy_assert.hpp"

namespace opsy
{
/**
 * Defines the default storage size for callback data
 */
static constexpr std::size_t default_callback_storage_size = 3;

/**
 * @brief A move-only callable wrapper, with the interface of @c std::move_only_function
 * @tparam Signature As for @c std::move_only_function : @c R(Args...) , with
 *         any of @c const , @c & or @c && , and @c noexcept . They say how the
 *         target is called -- @c R(Args...) const only accepts what can be
 *         called as @c const , a plain @c R(Args...) also takes a mutable lambda.
 * @tparam StorageSize The size available to store the target, in @c void* increments
 */
template<typename Signature, std::size_t StorageSize = default_callback_storage_size>
class callback;

namespace detail
{

/**
 * @brief What a @c callback does with the target it holds, without knowing its type
 *
 *        One constant instance per target type and signature, in flash; a
 *        @c callback points at the one for what it holds, and at none when
 *        empty. It depends on the signature only, not on the storage size,
 *        so a target moves between callbacks of different sizes unchanged.
 */
template<typename R, typename... A>
struct callback_operations
{
	R (*invoke)(void* storage, A&&... arguments);
	void (*relocate)(void* from, void* into) noexcept; ///< move-constructs at @p into, then destroys at @p from
	void (*destroy)(void* storage) noexcept;         ///< @c nullptr when the target is trivially destructible
};

/**
 * @brief How a signature calls its target: as what, whether that is
 *        possible for a given target type, and the call itself
 */
template<typename Signature>
struct callback_signature;

// One specialization per cv / ref combination; noexcept is deduced. INV is the
// reference the target is called through: an unqualified signature calls it as
// an lvalue, as std::move_only_function does. The six combinations can only be
// spelled out, which is what the standard library does too.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): see above
#define OPSY_CALLBACK_SIGNATURE(CV, REF, INV)                                              \
	template<typename R, typename... A, bool Noexcept>                                     \
	struct callback_signature<R(A...) CV REF noexcept(Noexcept)>                           \
	{                                                                                      \
		using operations = callback_operations<R, A...>;                                   \
		using function_pointer = R (*)(A...) noexcept(Noexcept);                           \
                                                                                           \
		template<typename F>                                                               \
		using invoked = F CV INV;                                                          \
                                                                                           \
		template<typename F>                                                               \
		static constexpr bool invocable = Noexcept                                         \
			? std::is_nothrow_invocable_r_v<R, invoked<F>, A...>                           \
			: std::is_invocable_r_v<R, invoked<F>, A...>;                                  \
                                                                                           \
		template<typename F>                                                               \
		static constexpr R invoke(void* storage, A&&... arguments) noexcept(Noexcept)      \
		{                                                                                  \
			return std::invoke_r<R>(                                                       \
				static_cast<invoked<F>>(*std::launder(static_cast<F*>(storage))),          \
				std::forward<A>(arguments)...);                                            \
		}                                                                                  \
                                                                                           \
		/* Calls a stateless target, built on the spot rather than stored. */              \
		template<typename F>                                                               \
		static constexpr R invoke_stateless(void*, A&&... arguments) noexcept(Noexcept)    \
		{                                                                                  \
			F target{};                                                                    \
			return std::invoke_r<R>(static_cast<invoked<F>>(target),                       \
				std::forward<A>(arguments)...);                                            \
		}                                                                                  \
	};

OPSY_CALLBACK_SIGNATURE(, , &)
OPSY_CALLBACK_SIGNATURE(, &, &)
OPSY_CALLBACK_SIGNATURE(, &&, &&)
OPSY_CALLBACK_SIGNATURE(const, , &)
OPSY_CALLBACK_SIGNATURE(const, &, &)
OPSY_CALLBACK_SIGNATURE(const, &&, &&)

#undef OPSY_CALLBACK_SIGNATURE

template<typename F, typename Signature>
struct callback_operations_for;

/**
 * @brief The operator() a signature gives a @c callback , with the signature's
 *        own qualifiers
 */
template<typename Derived, typename Signature>
struct callback_call;

// The analyzer follows a call on an empty callback, which is the precondition violation documented below.
// NOLINTBEGIN(clang-analyzer-core.NullDereference)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the six qualifier combinations, as above
#define OPSY_CALLBACK_CALL(CV, REF)                                                        \
	template<typename Derived, typename R, typename... A, bool Noexcept>                   \
	struct callback_call<Derived, R(A...) CV REF noexcept(Noexcept)>                       \
	{                                                                                      \
		/** @pre The callback holds a target: calling an empty one is a precondition      \
		 *       violation, as for @c std::move_only_function , asserted. */               \
		constexpr R operator()(A... arguments) CV REF noexcept(Noexcept)                   \
		{                                                                                  \
			const auto& self = static_cast<const Derived&>(*this);                         \
			assert(self.operations_ != nullptr);                                           \
			return self.operations_->invoke(self.target_address(),                         \
				std::forward<A>(arguments)...);                                            \
		}                                                                                  \
	};

OPSY_CALLBACK_CALL(, )
OPSY_CALLBACK_CALL(, &)
OPSY_CALLBACK_CALL(, &&)
OPSY_CALLBACK_CALL(const, )
OPSY_CALLBACK_CALL(const, &)
OPSY_CALLBACK_CALL(const, &&)

#undef OPSY_CALLBACK_CALL
// NOLINTEND(clang-analyzer-core.NullDereference)

/**
 * @brief The operations for one target type under one signature
 * @remark Each one finds the target through @c std::launder at the address it
 *         was placement-new'd at, which is what makes the access defined: an
 *         object of exactly that type lives there.
 */
template<typename F, typename Signature>
struct callback_operations_for
{
	static F& get(void* storage) noexcept
	{
		return *std::launder(static_cast<F*>(storage));
	}

	// static_cast and a destructor call rather than std::move and
	// std::destroy_at: the same thing, without one more function per target
	// type in a build that does not inline.
	static void relocate(void* from, void* into) noexcept
	{
		F& source = get(from);
		::new (into) F(static_cast<F&&>(source));
		source.~F();
	}

	static void destroy(void* storage) noexcept
	{
		get(storage).~F();
	}

	static constexpr callback_signature<Signature>::operations table{
		&callback_signature<Signature>::template invoke<F>,
		&relocate,
		std::is_trivially_destructible_v<F> ? nullptr : &destroy,
	};
};

/** @brief Relocates a stateless target: there is nothing to move */
constexpr void relocate_nothing(void* /*from*/, void* /*into*/) noexcept {}

/**
 * @brief The operations for a stateless target (a lambda without capture, a
 *        @c std::constant_wrapper ), which is built at each call instead of
 *        being stored
 */
template<typename F, typename Signature>
struct callback_stateless_operations_for
{
	static constexpr callback_signature<Signature>::operations table{
		&callback_signature<Signature>::template invoke_stateless<F>,
		&relocate_nothing,
		nullptr,
	};
};

template<typename T>
inline constexpr bool is_in_place_type = false;

template<typename T>
inline constexpr bool is_in_place_type<std::in_place_type_t<T>> = true;

}

/**
 * @brief A move-only callable wrapper, with the interface of @c std::move_only_function
 *
 *        A callback is used as a way to defer code execution. For example, when
 *        you ask a peripheral to do something, you may want the peripheral to
 *        execute code when it has finished, calling you back (hence the name).
 *
 *        The target lives in the callback's own storage, constructed there by
 *        placement new: one that does not fit, or needs more alignment than a
 *        pointer, is a compile-time error, never a heap allocation.
 *
 *        Built from a function or a stateless target, a callback is a constant
 *        expression, and can be called inside one:
 *        @code
 *        constinit callback<void()> on_receive = &handler; // no dynamic initialisation
 *        @endcode
 *
 * @remark Calling a function pointer target loads the pointer, then calls
 *         through it. A stateless target is called directly: for a slot on a
 *         hot path, write @c std::cw<&handler> or @c [] { handler(); } rather
 *         than @c &handler . Each such target has its own operations table.
 */
template<typename Signature, std::size_t StorageSize>
class callback : public detail::callback_call<callback<Signature, StorageSize>, Signature> // NOLINT(cppcoreguidelines-special-member-functions): its moves are the templates below, which also take a callback with less storage
{
	template<typename, std::size_t>
	friend class callback;
	friend struct detail::callback_call<callback, Signature>;

	using traits = detail::callback_signature<Signature>;
	using operations = traits::operations;
	using function_pointer = traits::function_pointer;

	template<typename F>
	static constexpr bool accepts = traits::template invocable<F>;

public:

	/** @brief Creates an empty callback */
	constexpr callback() noexcept = default;

	/** @brief Creates an empty callback */
	constexpr callback(std::nullptr_t) noexcept {} // NOLINT(misc-explicit-constructor,cppcoreguidelines-explicit-constructor): implicit, as std::move_only_function's

	/**
	 * @brief Creates a callback calling @p function
	 * @remark Empty if @p function is a null function or member pointer. A
	 *         function passed by name is a reference, not a pointer, and is
	 *         never null: it is stored without a check, which is also what
	 *         keeps @c -Wnonnull-compare quiet.
	 */
	template<typename F>
		requires (!std::same_as<std::remove_cvref_t<F>, callback>)
		      && (!detail::is_in_place_type<std::remove_cvref_t<F>>)
		      && std::is_constructible_v<std::decay_t<F>, F>
		      && accepts<std::decay_t<F>>
	constexpr callback(F&& function) // NOLINT(misc-explicit-constructor,cppcoreguidelines-explicit-constructor): implicit, as std::move_only_function's
	{
		using given = std::remove_cvref_t<F>;
		if constexpr (std::is_pointer_v<given> || std::is_member_pointer_v<given>)
			if (function == nullptr)
				return;
		// A constant expression cannot placement-new into the byte storage. At
		// compile time a stateless target is therefore not stored at all (its
		// operations build it at each call), and a function pointer goes into
		// the union's function pointer member: what makes
		// "constinit callback<void()> slot = &handler;" possible.
		if consteval
		{
			using target = std::decay_t<F>;
			if constexpr (std::is_empty_v<target> && std::is_trivially_default_constructible_v<target>
				&& std::is_trivially_destructible_v<target>)
			{
				operations_ = &detail::callback_stateless_operations_for<target, Signature>::table;
				return;
			}
			else if constexpr (std::is_convertible_v<F, function_pointer>)
			{
				storage_.function = function;
				operations_ = &detail::callback_operations_for<function_pointer, Signature>::table;
				return;
			}
		}
		emplace<std::decay_t<F>>(std::forward<F>(function));
	}

	/** @brief Creates a callback calling a @p T constructed in place from @p arguments */
	template<typename T, typename... Arguments>
		requires std::is_constructible_v<T, Arguments...> && accepts<T>
	explicit callback(std::in_place_type_t<T> /*type*/, Arguments&&... arguments)
	{
		emplace<T>(std::forward<Arguments>(arguments)...);
	}

	callback(const callback&) = delete("a callback owns what it holds and is move-only: move it");
	callback& operator=(const callback&) = delete("a callback owns what it holds and is move-only: move it");

	/**
	 * @brief Takes the target of @p from, which is left empty
	 * @remark Also from a callback with less storage, never more.
	 */
	template<std::size_t FromSize>
	callback(callback<Signature, FromSize>&& from) noexcept // NOLINT(misc-explicit-constructor,cppcoreguidelines-explicit-constructor,cppcoreguidelines-rvalue-reference-param-not-moved): a move, implicit as any; take() relocates the target out of it
	{
		static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");
		take(from);
	}

	/** @copydoc callback(callback<Signature, FromSize>&&) */
	template<std::size_t FromSize>
	callback& operator=(callback<Signature, FromSize>&& from) noexcept // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved): take() relocates the target out of it
	{
		static_assert(StorageSize >= FromSize, "You can only increase the storage size, not decrease it");

		if (static_cast<const void*>(&from) != static_cast<const void*>(this))
		{
			reset();
			take(from);
		}
		return *this;
	}

	/** @brief Empties the callback */
	constexpr callback& operator=(std::nullptr_t) noexcept
	{
		reset();
		return *this;
	}

	/** @brief Replaces the target with @p function */
	template<typename F>
		requires (!std::same_as<std::remove_cvref_t<F>, callback>)
		      && std::is_constructible_v<std::decay_t<F>, F>
		      && accepts<std::decay_t<F>>
	callback& operator=(F&& function)
	{
		callback replacement(std::forward<F>(function));
		reset();
		take(replacement);
		return *this;
	}

	constexpr ~callback()
	{
		reset();
	}

	/** @return @c true if it holds a target */
	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return operations_ != nullptr;
	}

	void swap(callback& other) noexcept
	{
		callback held(std::move(other));
		other = std::move(*this);
		*this = std::move(held);
	}

	friend void swap(callback& left, callback& right) noexcept
	{
		left.swap(right);
	}

	friend constexpr bool operator==(const callback& function, std::nullptr_t) noexcept
	{
		return !function;
	}

private:

	template<typename T, typename... Arguments>
	void emplace(Arguments&&... arguments)
	{
		static_assert(sizeof(T) <= capacity, "Cannot store the invokable in the callback");
		static_assert(alignof(T) <= alignof(void*), "The invokable needs more alignment than the callback storage provides");
		static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>,
			"The invokable is moved and destroyed by a callback's noexcept move and destructor");

		::new (target_address()) T(std::forward<Arguments>(arguments)...);
		operations_ = &detail::callback_operations_for<T, Signature>::table;
	}

	template<std::size_t FromSize>
	void take(callback<Signature, FromSize>& from) noexcept
	{
		if (from.operations_ == nullptr)
			return;

		from.operations_->relocate(from.target_address(), target_address());
		operations_ = from.operations_;
		from.operations_ = nullptr;
	}

	constexpr void reset() noexcept
	{
		if (operations_ != nullptr && operations_->destroy != nullptr)
			operations_->destroy(target_address());
		operations_ = nullptr;
	}

	/**
	 * @brief Where the operations find the target
	 * @remark A constant expression reads a function pointer target through the
	 *         union member that holds it: the only way to read it as itself.
	 *         Not const: the operations call through a const signature as const.
	 */
	[[nodiscard]] constexpr void* target_address() const noexcept
	{
		// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-const-cast): the storage is a union, and the operations restore the signature's const
		if consteval
		{
			if (operations_ == &detail::callback_operations_for<function_pointer, Signature>::table)
				return const_cast<void*>(static_cast<const void*>(&storage_.function));
		}
		return const_cast<std::byte*>(storage_.bytes.data());
		// NOLINTEND(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-const-cast)
	}

	static constexpr std::size_t capacity = StorageSize * sizeof(void*);

	const operations* operations_ = nullptr;
	union storage
	{
		alignas(void*) std::array<std::byte, capacity> bytes;
		function_pointer function; ///< only ever written at compile time, see the constructor
	};
	storage storage_{};
};
}
