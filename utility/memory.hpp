/**
 ******************************************************************************
 * @file    memory.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   Building blocks for typed, atomic-aware memory-mapped register layouts
 *
 *          Generated peripheral header packs (RCC, GPIO, USART, ...) declare
 *          their register layout as a struct of these helper types, so that
 *          accessing a register field becomes:
 *
 *          @code
 *          rcc.rcc_ahb1enr |= rcc_p::rcc_ahb1enr_r::gpdma1en_f() | atomic;
 *          @endcode
 *
 *          and the @c |= / @c <<= operators do the right read-modify-write,
 *          atomic or not.
 *
 *          What this header offers:
 *           - @ref clear_set – a (clear-mask, set-mask) pair, the atomic-friendly
 *             form of "clear these bits, then set those bits".
 *           - @ref atomic_t – a tag wrapper that turns a normal masked-or
 *             into an atomic @c fetch_or / @c compare_exchange operation.
 *             Created via the @c "value | atomic" / @c "clear_set || atomic"
 *             postfix syntax.
 *           - @ref atomic – the singleton tag used in that postfix syntax.
 *           - @ref memory – read-write storage for a single memory-mapped
 *             word. Holds a @c volatile @c std::atomic of the underlying
 *             integer type and exposes the bitwise / clear-set operators.
 *           - @ref read_only_memory – same idea, but read-only.
 *           - @ref write_only_memory – same idea, but write-only.
 *           - @ref padding – fixed-size byte filler used to align successive
 *             registers in a struct to the offsets the hardware expects.
 *
 *          Note that this is the storage-side helper: it sits in the actual
 *          peripheral struct that lives at the peripheral's base address.
 *          For one-off accesses to a known address (e.g. SCB / NVIC), use
 *          @c opsy::detail::memory_register (in @c cortex_m.hpp ), which holds an address rather
 *          than a value.
 *
 * @par What @c atomic protects against
 *
 *          An atomic update is a load-exclusive / store-exclusive loop: the
 *          store only happens if nothing broke the core's exclusive monitor
 *          since the load, and is retried otherwise. Exception entry and exit
 *          clear the monitor, so an interrupt handler or a task switch landing
 *          in between makes the store fail and the update start again. The
 *          update is therefore atomic against every other task and handler on
 *          the core, and lock-free: one that keeps retrying holds up only
 *          itself, never masks anything.
 *
 *          It is not atomic against another bus master. A DMA write to the
 *          same register between the load and the store does not touch the
 *          core's monitor; the store succeeds and the DMA write is lost. Nor
 *          against the peripheral itself, which updates its own bits whenever
 *          it does.
 *
 * @par Where the architecture leaves it to the device
 *
 *          Peripheral registers are Device memory. Armv7-M asks for exclusive
 *          accesses to target Normal memory only (DDI 0403E.e, A3.4.5).
 *          Armv8-M treats Device memory as always shareable (DDI 0553B.h,
 *          RYMTK), so a store-exclusive to it also goes through the global
 *          monitor, whose existence and coverage are left to the system
 *          (B8.3); where there is none, the architecture allows a fault, a
 *          no-op or an UNKNOWN status. Whether @c atomic works on a given
 *          device is therefore a property of that device, to be checked on it.
 *
 *          Checked on STM32H563 (Cortex-M33), on APB1, AHB2 and AHB3
 *          registers, debug and release: no fault, the store-exclusive fails
 *          every time an interrupt is taken between load and store, and an
 *          interrupt toggling another bit of the register at about 450 kHz
 *          loses none of its updates to an atomic read-modify-write running
 *          against it, where a plain one loses thousands.
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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace opsy::utility
{

/**
 * @brief A pair of (clear, set) masks for read-modify-write register updates.
 *        @c clear_ marks the bits to zero before @c set_ marks the bits to one.
 *        Combine multiple operations with @c operator||.
 */
template<typename TReg>
class clear_set
{
public:

	constexpr clear_set(TReg clear, TReg set) : clear_(clear), set_(set) {}

	constexpr auto operator||(clear_set other) const -> clear_set
	{
		return clear_set(clear_ | other.clear_, set_ | other.set_);
	}

	constexpr auto clear() const -> TReg { return clear_; }
	constexpr auto set() const -> TReg { return set_; }

private:
	TReg clear_;
	TReg set_;
};

/**
 * @brief Tag wrapper that promotes a register update to an atomic operation.
 *        Created via the @c value|atomic / @c clear_set||atomic postfix
 *        notation; consumed by @c memory's atomic-aware overloads.
 */
template<typename T>
struct atomic_t
{
	T value;
	constexpr auto operator~() { return atomic_t{ ~value }; }
};

/**
 * @brief Tag type for the @c atomic singleton — never instantiated directly.
 */
struct atomic_tag {};

/**
 * @brief Tag value used as the right-hand operand of @c | and @c || to mark a
 *        register update as atomic. Lives in @c opsy::utility — pull it into
 *        scope at peripheral-header level with @c using @c opsy::utility::atomic.
 */
constexpr atomic_tag atomic;

template<typename T>
constexpr auto operator|(T t, [[maybe_unused]] atomic_tag ato)
{
	return atomic_t<decltype(t | t)>{ t };
}

template<typename T>
constexpr auto operator||(clear_set<T> cs, [[maybe_unused]] atomic_tag ato)
{
	return atomic_t<clear_set<T>>{ cs };
}

template<typename T>
constexpr auto operator||(T value, [[maybe_unused]] atomic_tag ato)
{
	return atomic_t<decltype(value || value)>{ decltype(value || value){ T::mask, value } };
}

/**
 * @brief Read-write memory-mapped register storage.
 *        @tparam TInt The underlying integer type (uint8_t / uint16_t / uint32_t / uint64_t).
 *        @tparam TReg The strongly-typed register wrapper that converts to / from @c TInt.
 */
template<typename TInt, typename TReg>
class memory
{
	static_assert(std::is_integral_v<TInt>);
	static_assert(sizeof(TReg) == sizeof(TInt));
	static_assert(std::is_convertible_v<TInt, TReg>);
	static_assert(std::atomic<TInt>::is_always_lock_free, "TInt is not lock-free on this target: every access would go through libatomic, and a register must be accessed in one instruction");

public:

	auto operator=(TReg value) -> memory&
	{
		value_.store(value.value(), std::memory_order_relaxed);
		return *this;
	}

	operator TReg() const
	{
		return load();
	}

	[[nodiscard]] auto load() const -> TReg
	{
		return value_.load(std::memory_order_relaxed);
	}

	auto operator|=(atomic_t<TReg> value) -> memory&
	{
		value_.fetch_or(value.value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator|=(TReg value) -> memory&
	{
		value_.store(value_.load(std::memory_order_relaxed) | value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator&=(atomic_t<TReg> value) -> memory&
	{
		value_.fetch_and(value.value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator&=(TReg value) -> memory&
	{
		value_.store(value_.load(std::memory_order_relaxed) & value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator^=(atomic_t<TReg> value) -> memory&
	{
		value_.fetch_xor(value.value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator^=(TReg value) -> memory&
	{
		value_.store(value_.load(std::memory_order_relaxed) ^ value.value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator<<=(clear_set<TReg> value) -> memory&
	{
		value_.store((value_.load(std::memory_order_relaxed) & ~value.clear().value()) | value.set().value(), std::memory_order_relaxed);
		return *this;
	}

	auto operator<<=(atomic_t<clear_set<TReg>> value) -> memory&
	{
		auto previous = value_.load(std::memory_order_relaxed);
		while (!value_.compare_exchange_strong(previous, (previous & ~value.value.clear().value()) | value.value.set().value(), std::memory_order_relaxed));
		return *this;
	}

private:
	volatile std::atomic<TInt> value_;
};

/**
 * @brief Read-only memory-mapped register storage.
 */
template<typename TInt, typename TReg>
class read_only_memory
{
	static_assert(std::is_integral_v<TInt>);
	static_assert(sizeof(TReg) == sizeof(TInt));
	static_assert(std::is_convertible_v<TInt, TReg>);
	static_assert(std::atomic<TInt>::is_always_lock_free, "TInt is not lock-free on this target: every access would go through libatomic, and a register must be accessed in one instruction");

public:

	operator TReg() const
	{
		return load();
	}

	[[nodiscard]] auto load() const -> TReg
	{
		return value_.load(std::memory_order_relaxed);
	}

private:
	volatile std::atomic<TInt> value_;
};

/**
 * @brief Write-only memory-mapped register storage.
 */
template<typename TInt, typename TReg>
class write_only_memory
{
	static_assert(std::is_integral_v<TInt>);
	static_assert(sizeof(TReg) == sizeof(TInt));
	static_assert(std::is_convertible_v<TInt, TReg>);
	static_assert(std::atomic<TInt>::is_always_lock_free, "TInt is not lock-free on this target: every access would go through libatomic, and a register must be accessed in one instruction");

public:

	auto operator=(TReg value) -> write_only_memory&
	{
		value_.store(value.value(), std::memory_order_relaxed);
		return *this;
	}

private:
	volatile std::atomic<TInt> value_;
};

/**
 * @brief Fixed byte-size padding inserted between registers in a peripheral
 *        struct so that successive registers land at the offsets the hardware
 *        expects.
 */
template<std::size_t Size>
class __attribute__((packed)) padding
{
	static_assert(Size != 0);
	std::array<uint8_t, Size> reserved;
};

} // namespace opsy::utility
