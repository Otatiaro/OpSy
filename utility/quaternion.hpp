/**
 ******************************************************************************
 * @file    quaternion.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   Quaternion operations on top of @ref vector
 *
 *          Quaternions are stored as @c vector<4, T> with the convention
 *          @c x() / @c y() / @c z() = imaginary components ( @c i / @c j / @c k )
 *          and @c w() = real (scalar) component.
 *
 *          The header adds the operations specific to unit quaternions —
 *          composition, conjugate / inverse, axis-angle factory, rotation of
 *          a 3D vector, and slerp — as free functions in @c opsy::utility .
 *          It deliberately does NOT introduce an @c operator*( @c vector<4>,
 *          @c vector<4> ) for the Hamilton product: the element-wise reading
 *          would silently disagree with the quaternion meaning. Writing
 *          @c q1 @c * @c q2 is a compile error, which is the cheap reminder
 *          that you wanted @ref hamilton_product instead. Element-wise
 *          @c +, @c -, scalar @c * / @c / and @c norm / @c length /
 *          @c normalize from @ref vector keep working and are used
 *          internally by @ref slerp.
 *
 *          A note on the double-cover: any unit quaternion @c q and its
 *          negation @c -q represent the same rotation, so @c q @c == @c -q
 *          returns @c false on a vector-level equality even though the two
 *          rotations are identical. Compare on the actual rotation (e.g.
 *          @c |dot_product(q1, q2)| close to 1) when this matters.
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

#include <cmath>

#include "vector.hpp"

namespace opsy::utility
{

/**
 * @brief Type alias for a quaternion stored as a 4D vector.
 *        Pure documentation — the underlying type is still @c vector<4, T>.
 */
template<typename T = float>
using quaternion = vector<4, T>;

/**
 * @brief The identity rotation: @c (0, 0, 0, 1).
 */
template<typename T = float>
constexpr quaternion<T> identity_quaternion()
{
	return { T{}, T{}, T{}, T{1} };
}

/**
 * @brief Hamilton product @c q1 * @c q2.
 *
 *        Composes two rotations: applying @c hamilton_product(q1, q2) to a
 *        vector first rotates by @c q2, then by @c q1.
 */
template<typename T>
constexpr quaternion<T> hamilton_product(const quaternion<T>& q1, const quaternion<T>& q2)
{
	return {
		q1.w() * q2.x() + q1.x() * q2.w() + q1.y() * q2.z() - q1.z() * q2.y(),
		q1.w() * q2.y() - q1.x() * q2.z() + q1.y() * q2.w() + q1.z() * q2.x(),
		q1.w() * q2.z() + q1.x() * q2.y() - q1.y() * q2.x() + q1.z() * q2.w(),
		q1.w() * q2.w() - q1.x() * q2.x() - q1.y() * q2.y() - q1.z() * q2.z()
	};
}

/**
 * @brief Conjugate: @c (-x, -y, -z, w) .
 *        Equal to the inverse for a unit quaternion.
 */
template<typename T>
constexpr quaternion<T> conjugate(const quaternion<T>& q)
{
	return { -q.x(), -q.y(), -q.z(), q.w() };
}

/**
 * @brief Inverse: @c conjugate(q) / norm(q) .
 *        For a unit quaternion this is equal to @c conjugate(q) , so prefer
 *        @ref conjugate when you know the input is unit-length to skip the
 *        four divisions.
 */
template<typename T>
constexpr quaternion<T> inverse(const quaternion<T>& q)
{
	const auto n = q.norm();
	return { -q.x() / n, -q.y() / n, -q.z() / n, q.w() / n };
}

/**
 * @brief Builds the unit quaternion that represents a rotation of @p angle
 *        radians around @p axis.
 * @param axis  Unit-length axis of rotation.
 * @param angle Rotation angle in radians.
 * @pre   @p axis must be unit-length; the result is unit-length only when
 *        this precondition holds.
 */
template<typename T>
quaternion<T> from_axis_angle(const vector<3, T>& axis, T angle)
{
	const auto half = angle / T{2};
	const auto s = std::sin(half);
	return { axis.x() * s, axis.y() * s, axis.z() * s, std::cos(half) };
}

/**
 * @brief Rotates a 3D vector by a unit quaternion.
 *
 *        Optimised form of @c v' = q * v * q⁻¹ that avoids two full Hamilton
 *        products by decomposing into cross products: about 12 mul / 12 add
 *        / 6 sub instead of ~28 mul / 16 add for the naive formulation.
 *
 * @param v Vector to rotate.
 * @param q Unit quaternion describing the rotation.
 * @pre   @p q must be unit-length.
 */
template<typename T>
constexpr vector<3, T> rotate(const vector<3, T>& v, const quaternion<T>& q)
{
	const vector<3, T> qv{ q.x(), q.y(), q.z() };
	const auto t = T{2} * cross_product(qv, v);
	return v + q.w() * t + cross_product(qv, t);
}

/**
 * @brief Spherical linear interpolation between two unit quaternions.
 *
 *        Walks along the great-circle arc on the unit hypersphere, so the
 *        intermediate quaternions remain unit-length and the angular speed
 *        stays constant. Picks the shortest of the two equivalent paths
 *        (since @c q and @c -q represent the same rotation) by negating
 *        @p q2 when the two endpoints are more than 90° apart on the
 *        4D dot product. Falls back to a normalised linear interpolation
 *        when the two endpoints are within ~1.8° of each other to avoid a
 *        division by an almost-zero @c sin(theta) .
 *
 * @param q1 First unit quaternion.
 * @param q2 Second unit quaternion.
 * @param t  Interpolation factor in @c [0, 1] .
 * @pre   @p q1 and @p q2 must be unit-length.
 */
template<typename T>
quaternion<T> slerp(const quaternion<T>& q1, const quaternion<T>& q2, T t)
{
	auto cos_theta = dot_product(q1, q2);
	auto q2_adj = q2;
	if (cos_theta < T{0})           // shortest-path: pick the closer of q2 and -q2
	{
		q2_adj = -q2;
		cos_theta = -cos_theta;
	}

	if (cos_theta > static_cast<T>(0.9995))   // nearly aligned — fall back to normalised lerp
	{
		auto result = q1 * (T{1} - t) + q2_adj * t;
		result.normalize();
		return result;
	}

	const auto theta = std::acos(cos_theta);
	const auto inv_sin_theta = T{1} / std::sin(theta);
	const auto a = std::sin((T{1} - t) * theta) * inv_sin_theta;
	const auto b = std::sin(t * theta)         * inv_sin_theta;
	return q1 * a + q2_adj * b;
}

}
