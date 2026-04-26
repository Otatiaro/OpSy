/**
 ******************************************************************************
 * @file    vector.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   A fixed-size mathematical vector
 *
 * 			A compile-time-sized vector with the usual arithmetic operators
 * 			(@c +, @c -, @c *, @c /, in-place variants), norm, length and
 * 			normalization, plus typed accessors @c x() / @c y() / @c z() /
 * 			@c w() constrained by size. Helpers @c append, @c prepend and
 * 			@c sub return new vectors of the appropriate size, and free
 * 			functions @c rotate, @c cross_product and @c dot_product cover
 * 			the most common geometric operations.
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
#include <cstddef>
#include <utility>
#include <math.h>

namespace opsy::utility
{

template<std::size_t Size, typename ItemType = float>
class vector
{
	static_assert(Size != 0, "Cannot make a zero sized vector");
	static_assert(Size != 1, "A one sized vector is a value");
	template<std::size_t S, typename I> friend class vector; // vectors of all sizes are friend between them

public:

	/**
	 * Creates a vector from an initialization list
	 * @param value The first value in the list (there is at least two)
	 * @param values The remaining values in the list
	 */
	template<typename ... Args>
	constexpr vector(ItemType value, Args&&... values) : values_{{value, values...}}
	{
		static_assert(sizeof...(Args) + 1 == Size, "Wrong number of values for vector initialization");
	}

	/**
	 * Creates a @c vector from an array of values
	 * @param items The array containing the @c vector values
	 */
	constexpr vector(const std::array<ItemType, Size>& items) : values_{items}
	{

	}

	/**
	 * Creates a @c vector from another @c vector
	 * @param other The vector to copy
	 */
	constexpr vector(const vector& other) : values_{other.values_}
	{

	}

	/**
	 * Creates a @c vector with a specified value at each rank
	 * @param value
	 */
	constexpr vector(const ItemType& value) : values_{construct([&](std::size_t) {return value;})}
	{

	}

	/**
	 * Creates a default @c vector initialized with @c ItemType default value
	 */
	constexpr vector() : vector(ItemType())
	{

	}

	/**
	 * Copy assignment operator
	 * @param other The vector to copy over this instance
	 * @return This instance reference
	 */
	constexpr vector& operator=(const vector& other)
	{
		values_ = other.values_;
		return *this;
	}

	/**
	 * Constant bracket operator
	 * @param index The index of the value to get by const reference
	 * @return The n-th value of the @c vector
	 */
	constexpr inline const ItemType& operator[](std::size_t index) const
	{
		return values_[index];
	}

	/**
	 * Bracket operator
	 * @param index The index of the value to get by reference
	 * @return The n-th value of the @c vector
	 */
	constexpr inline ItemType& operator[](std::size_t index)
	{
		return values_[index];
	}

	/**
	 * Gets the size of the @c vector
	 * @return The size of the @c vector
	 */
	constexpr inline std::size_t size() const
	{
		return Size;
	}

	/**
	 * Gets the norm of the @c vector (squared length)
	 * @return The norm of the @c vector
	 */
	constexpr ItemType norm() const
	{
		// should use return std::transform_reduce here but not yet available in the library

		auto result = ItemType();

		for (auto i = 0U; i < Size; ++i)
			result += this->operator[](i) * this->operator[](i);

		return result;
	}

	/**
	 * Gets the length of the @c vector (square root of the norm)
	 * @return The length of the @c vector
	 */
	constexpr ItemType length() const
	{
		return std::sqrt(norm());
	}

	/**
	 * Normalizes the current @c vector (divide all items by its length)
	 */
	constexpr void normalize()
	{
		*this /= length();
	}

	/**
	 * Gets the current @c vector normalized as a copy
	 * @return The current @c vector normalized
	 */
	constexpr vector normalized() const
	{
		return *this / length();
	}

	/**
	 * Gets the item at rank @c Index by constant reference
	 * @tparam Index The index of the item to get
	 * @return A const reference to the n-th item
	 */
	template<std::size_t Index>
	constexpr const ItemType& at() const
	{
		static_assert(Index < Size, "Index out of bounds");
		return values_[Index];
	}

	/**
	 * Gets the item at rank @c Index by reference
	 * @tparam Index The index of the item to get
	 * @return A reference to the n-th item
	 */
	template<std::size_t Index>
	constexpr ItemType& at()
	{
		static_assert(Index < Size, "Index out of bounds");
		return values_[Index];
	}

#if __cplusplus >= 201709L

	/**
	 * Gets the @c x (first) value in the @c vector
	 * @return The @c x value
	 */
	constexpr const ItemType& x() const requires (Size == 2 || Size == 3 || Size == 4)
	{
		return values_[0];
	}

	/**
	 * Gets the @c x (first) value in the @c vector
	 * @return The @c x value
	 */
	constexpr ItemType& x() requires (Size == 2 || Size == 3 || Size == 4)
	{
		return values_[0];
	}

	/**
	 * Gets the @c y (second) value in the @c vector
	 * @return The @c y value
	 */
	constexpr const ItemType& y() const requires (Size == 2 || Size == 3 || Size == 4)
	{
		return values_[1];
	}

	/**
	 * Gets the @c y (second) value in the @c vector
	 * @return The @c y value
	 */
	constexpr ItemType& y() requires (Size == 2 || Size == 3 || Size == 4)
	{
		return values_[1];
	}

	/**
	 * Gets the @c z (third) value in the @c vector
	 * @return The @c z value
	 */
	constexpr const ItemType& z() const requires (Size == 3 || Size == 4)
	{
		return values_[2];
	}

	/**
	 * Gets the @c z (third) value in the @c vector
	 * @return The @c z value
	 */
	constexpr ItemType& z() requires (Size == 3 || Size == 4)
	{
		return values_[2];
	}

	/**
	 * Gets the @c w (fourth) value in the @c vector
	 * @return The @c w value
	 */
	constexpr const ItemType& w() const requires (Size == 4)
	{
		return values_[3];
	}

	/**
	 * Gets the @c w (fourth) value in the @c vector
	 * @return The @c w value
	 */
	constexpr ItemType& w() requires (Size == 4)
	{
		return values_[3];
	}

#else

	/**
	 * Gets the @c x (first) value in the @c vector
	 * @return The @c x value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 2 || S == 3 || S == 4>>
	constexpr const ItemType& x() const
	{
		return values_[0];
	}

	/**
	 * Gets the @c x (first) value in the @c vector
	 * @return The @c x value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 2 || S == 3 || S == 4>>
	constexpr ItemType& x()
	{
		return values_[0];
	}

	/**
	 * Gets the @c y (second) value in the @c vector
	 * @return The @c y value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 2 || S == 3 || S == 4>>
	constexpr const ItemType& y() const
	{
		return values_[1];
	}

	/**
	 * Gets the @c y (second) value in the @c vector
	 * @return The @c y value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 2 || S == 3 || S == 4>>
	constexpr ItemType& y()
	{
		return values_[1];
	}

	/**
	 * Gets the @c z (third) value in the @c vector
	 * @return The @c z value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 3 || S == 4>>
	constexpr const ItemType& z() const
	{
		return values_[2];
	}

	/**
	 * Gets the @c z (third) value in the @c vector
	 * @return The @c z value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 3 || S == 4>>
	constexpr ItemType& z()
	{
		return values_[2];
	}

	/**
	 * Gets the @c w (fourth) value in the @c vector
	 * @return The @c w value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 4>>
	constexpr const ItemType& w() const
	{
		return values_[3];
	}

	/**
	 * Gets the @c w (fourth) value in the @c vector
	 * @return The @c w value
	 */
	template<std::size_t S = Size, typename = std::enable_if_t<S == 4>>
	constexpr ItemType& w()
	{
		return values_[3];
	}

#endif

	/**
	 * Appends a new value to the current @c vector
	 * @param value The value to append to the current @c vector
	 * @return A new @c vector as a concatenation of the current @c vector and the specified value
	 */
	constexpr vector<Size + 1, ItemType> append(const ItemType& value) const
	{
		return append_impl(value, values_, std::make_index_sequence<Size>());
	}

	/**
	 * Prepends a new value to the current @c vector
	 * @param value The value to prepend to the current @c vector
	 * @return A new @c vector as a concatenation of the specified value and the current @c vector
	 */
	constexpr vector<Size + 1, ItemType> prepend(const ItemType& value) const
	{
		return prepend_impl(value, values_, std::make_index_sequence<Size>());
	}

	/**
	 * Extracts a sub-vector from the current vector
	 * @tparam Length The length of the sub-vector
	 * @tparam Offset The offset of the start of the sub-vector
	 * @return The sub-vector starting at @c Offset and with length @c Length
	 */
	template<std::size_t Length, std::size_t Offset = 0>
	constexpr vector<Length, ItemType> sub() const
	{
		static_assert(Length + Offset <= Size, "Sub vector out of range");
		return extract_impl<Length, Offset>(values_, std::make_index_sequence<Length>());
	}

	/**
	 * Multiplies the current @c vector by a factor
	 * @param factor The multiplying factor
	 * @return A reference to the current @c vector
	 */
	constexpr vector& operator*=(const ItemType& factor)
	{
		for (auto i = 0U; i < Size; ++i)
			values_[i] *= factor;

		return *this;
	}

	/**
	 * Divides the current @c vector by a factor
	 * @param factor The dividing factor
	 * @return A reference to the current @c vector
	 */
	constexpr vector& operator/=(const ItemType& factor)
	{
		for (auto i = 0U; i < Size; ++i)
			values_[i] /= factor;

		return *this;
	}

	/**
	 * Adds another @c vector to the current @c vector
	 * @param right The @c vector to add
	 * @return A reference to the current @c vector
	 */
	constexpr vector& operator+=(const vector& right)
	{
		for (auto i = 0U; i < Size; ++i)
			values_[i] += right[i];

		return *this;
	}

	/**
	 * Subtracts another @c vector to the current @c vector
	 * @param right The @c vector to subtracts
	 * @return A reference to the current @c vector
	 */
	constexpr vector& operator-=(const vector& right)
	{
		for (auto i = 0U; i < Size; ++i)
			values_[i] -= right[i];

		return *this;
	}

	/**
	 * Constructs the opposite of the current @c vector (all values negated)
	 * @return A copy of the current @c vector with all values negated
	 */
	constexpr vector operator-() const
	{
		return construct([](const ItemType& l, const ItemType& r) {return l * r;}, values_, static_cast<ItemType>(-1));
	}

	/**
	 * Adds another @c vector to the current one
	 * @param right The @c vector to add to the current one
	 * @return The @c vector containing the sum of the two vectors
	 */
	constexpr vector operator+(const vector& right) const
	{
		return construct([](const ItemType& l, const ItemType& r) {return l + r;}, values_, right.values_);
	}

	/**
	 * Subtracts another @c vector to the current one
	 * @param right The @c vector to subtracts to the current one
	 * @return The @c vector containing the subtraction of the two vectors
	 */
	constexpr vector operator-(const vector& right) const
	{
		return construct([](const ItemType& l, const ItemType& r) {return l - r;}, values_, right.values_);
	}


	/**
	 * Multiplies the current @c vector by a factor
	 * @param factor The multiplying factor
	 * @return The current @c vector multiplied by the factor
	 */
	constexpr vector operator*(const ItemType& factor) const
	{
		return construct([](const ItemType& l, const ItemType& r) {return l * r;}, values_, factor);
	}

	/**
	 * Divides the current @c vector by a factor
	 * @param factor The dividing factor
	 * @return The current @c vector divided by the factor
	 */
	constexpr vector operator/(const ItemType& factor) const
	{
		return construct([](const ItemType& l, const ItemType& r) {return l / r;}, values_, factor);
	}

	/**
	 * Checks if two vectors are identical term by term
	 * @param right The @c vector to compare to
	 * @return @c true if both vectors are identical, @c false otherwise
	 */
	constexpr bool operator==(const vector& right) const
	{
		return values_ == right.values_;
	}

private:

	std::array<ItemType, Size> values_;

	template<typename F, std::size_t... Is>
	static constexpr std::array<ItemType, Size> construct_impl(F func, std::index_sequence<Is...>)
	{
		return {func(Is)...};
	}

	template<typename F, std::size_t... Is>
	static constexpr std::array<ItemType, Size> construct_impl(F func, const std::array<ItemType, Size>& values, const ItemType& param, std::index_sequence<Is...>)
	{
		return {func(values[Is], param)...};
	}

	template<typename F, std::size_t... Is>
	static constexpr std::array<ItemType, Size> construct_impl(F func, const std::array<ItemType, Size>& values, const std::array<ItemType, Size>& param, std::index_sequence<Is...>)
	{
		return {func(values[Is], param[Is])...};
	}

	template<std::size_t... Is>
	static constexpr std::array<ItemType, Size + 1> append_impl(const ItemType& value, const std::array<ItemType, Size>& values, std::index_sequence<Is...>)
	{
		return {values[Is]..., value};
	}

	template<std::size_t... Is>
	static constexpr std::array<ItemType, Size + 1> prepend_impl(const ItemType& value, const std::array<ItemType, Size>& values, std::index_sequence<Is...>)
	{
		return {value, values[Is]...};
	}

	template <std::size_t Length, std::size_t Offset, std::size_t... Is>
	static constexpr vector<Length, ItemType> extract_impl(const std::array<ItemType, Size>& values, std::index_sequence<Is...>)
	{
		return { values[Is + Offset]... };
	}

	template<typename F>
	static constexpr std::array<ItemType, Size> construct(F func)
	{
		return construct_impl(func, std::make_index_sequence<Size>());
	}

	template<typename F>
	static constexpr std::array<ItemType, Size> construct(F func, const std::array<ItemType, Size>& values, const ItemType& value)
	{
		return construct_impl(func, values, value, std::make_index_sequence<Size>());
	}

	template<typename F>
	static constexpr std::array<ItemType, Size> construct(F func, const std::array<ItemType, Size>& values, const std::array<ItemType, Size>& param)
	{
		return construct_impl(func, values, param, std::make_index_sequence<Size>());
	}

};


template<std::size_t Size, typename ItemType>
constexpr auto operator*(const ItemType& factor, const vector<Size, ItemType>& v)
{
	return v * factor;
}

/**
 * Creates the image of the @c vector by the rotation of a specified angle
 * @param v The @c vector to rotate
 * @param angle The angle of rotation
 * @return The image of v by the rotation of angle radian
 */
template<typename ItemType>
auto rotate(const vector<2, ItemType>& v, ItemType angle) -> vector<2, ItemType>
{
	auto sin = std::sin(angle);
	auto cos = std::cos(angle);

	return vector<2>{v.x() * cos - v.y() * sin, v.y() * cos + v.x() * sin};
}

/**
 * Calculates the cross product of two vectors
 * @param left The first vector
 * @param right The second vector
 * @return The cross product of left and right
 */
template<typename ItemType>
auto cross_product(const vector<3, ItemType>& left, const vector<3, ItemType>& right) -> vector<3, ItemType>
{
	return vector<3, ItemType>{
		left.y() * right.z() - left.z() * right.y(),
		left.z() * right.x() - left.x() * right.z(),
		left.x() * right.y() - left.y() * right.x()
	};
}

/**
 * Calculates the dot product of two vectors
 * @param left The first vector
 * @param right The second vector
 * @return The dot product of left and right
 */
template<std::size_t Size, typename ItemType>
auto dot_product(const vector<Size, ItemType>& left, const vector<Size, ItemType>& right) -> ItemType
{
	ItemType result = ItemType();

	for (auto i = 0U; i < Size; ++i)
		result += left[i] * right[i];

	return result;
}

}
