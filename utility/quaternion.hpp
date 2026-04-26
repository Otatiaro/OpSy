/**
 ******************************************************************************
 * @file    quaternion.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   Strong-typed unit quaternion built on top of @ref vector
 *
 *          @c quaternion<T> is a thin composition wrapper around
 *          @c vector<4, T> with the convention
 *          @c x() / @c y() / @c z() = imaginary components ( @c i / @c j /
 *          @c k ) and @c w() = real (scalar) component. The wrapper exists
 *          only to give the type a different identity than a plain 4D
 *          vector — it carries no extra data and at @c -Og or higher the
 *          compiler erases it entirely (verified against @c objdump).
 *
 *          Because @c quaternion<T> is its own type:
 *           - @c q1 @c * @c q2 means the Hamilton product (composition of
 *             rotations). It does not silently interpret a @c vector<4>
 *             holding RGBA, complex pairs, etc. as a quaternion.
 *           - @c q @c * @c v3 rotates the 3D vector by the unit quaternion.
 *           - Element-wise @c +, @c -, scalar @c * / @c / on quaternions
 *             still work — they delegate to the underlying @c vector<4> —
 *             and are used internally by @ref slerp.
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
#include <numbers>

#include "vector.hpp"

namespace opsy::utility
{

/**
 * @brief Strong-typed unit quaternion.
 *
 *        Stored as a @c vector<4, T> with @c x()/@c y()/@c z() the
 *        imaginary components ( @c i, @c j, @c k ) and @c w() the real
 *        component. Distinct from a plain @c vector<4> at the type level
 *        so that @c operator* unambiguously means "compose rotations" or
 *        "rotate a 3D vector".
 *
 * @tparam T Scalar type (default @c float ).
 */
template<typename T = float>
class quaternion
{
public:

	using value_type = T;

	/** Identity rotation: @c (0, 0, 0, 1) . */
	constexpr quaternion() : v_{ T{}, T{}, T{}, T{1} } {}

	/** From components: @c x, @c y, @c z imaginary, @c w real. */
	constexpr quaternion(T x, T y, T z, T w) : v_{ x, y, z, w } {}

	/**
	 * @brief Wrap an existing 4-tuple of components as a quaternion.
	 * @remark Explicit on purpose: a @c vector<4> can hold anything (RGBA,
	 *         a homogeneous coordinate, ...) and treating it as a
	 *         quaternion is an opt-in, not a free conversion.
	 */
	explicit constexpr quaternion(const vector<4, T>& v) : v_{ v } {}

	/**
	 * @brief Build a unit quaternion from a rotation vector.
	 *
	 *        The rotation vector encodes a rotation as @c axis @c * @c
	 *        angle : direction = unit rotation axis, length = angle in
	 *        radians. A zero-length input maps to the identity rotation
	 *        (no NaN). This is the continuous form used by gyroscope
	 *        integrators.
	 *
	 * @remark Explicit on purpose: a @c vector<3> can hold anything (a
	 *         position, a velocity, ...) and treating it as a rotation is
	 *         an opt-in, not a free conversion.
	 */
	explicit quaternion(const vector<3, T>& rotation_vector)
	{
		const auto angle = rotation_vector.length();
		if (angle == T{0})
		{
			v_ = vector<4, T>{ T{}, T{}, T{}, T{1} };  // identity, no division by zero
			return;
		}
		const auto half = angle / T{2};
		const auto s    = std::sin(half) / angle;      // sin(angle/2) / angle scales the rotation vector to the imaginary part
		v_ = vector<4, T>{
			rotation_vector.x() * s,
			rotation_vector.y() * s,
			rotation_vector.z() * s,
			std::cos(half)
		};
	}

	/**
	 * @brief Build the shortest-arc rotation that takes @p from to @p to.
	 *
	 *        Both inputs must be unit-length. The result rotates @p from
	 *        onto @p to along the great-circle arc between them. The
	 *        antiparallel case ( @p from @c == @c -@p to , so the rotation
	 *        is 180° but the axis is undetermined) is handled by picking
	 *        any axis perpendicular to @p from .
	 *
	 *        Math: with @c r @c = @c 1 @c + @c dot(from, @c to) and @c v
	 *        @c = @c cross(from, @c to), the unnormalised quaternion @c
	 *        (v.x, @c v.y, @c v.z, @c r) is the half-angle form. After
	 *        normalisation, @c xyz @c = @c sin(theta/2) @c * @c axis and
	 *        @c w @c = @c cos(theta/2) .
	 *
	 * @pre   @p from and @p to must be unit-length.
	 */
	quaternion(const vector<3, T>& from, const vector<3, T>& to)
	{
		const T r = dot_product(from, to) + T{1};
		if (r < static_cast<T>(1e-6))  // antiparallel: pick any axis perpendicular to from
		{
			vector<3, T> perp = (std::abs(from.x()) > std::abs(from.z()))
				? vector<3, T>{ -from.y(),  from.x(), T{0}     }
				: vector<3, T>{  T{0},     -from.z(), from.y() };
			perp.normalize();
			v_ = vector<4, T>{ perp.x(), perp.y(), perp.z(), T{0} };  // 180° rotation, already unit length
			return;
		}
		const auto v = cross_product(from, to);
		v_ = vector<4, T>{ v.x(), v.y(), v.z(), r };
		v_.normalize();
	}

	// ── Component accessors ──────────────────────────────────────────────
	constexpr const T& x() const { return v_.x(); }
	constexpr       T& x()       { return v_.x(); }
	constexpr const T& y() const { return v_.y(); }
	constexpr       T& y()       { return v_.y(); }
	constexpr const T& z() const { return v_.z(); }
	constexpr       T& z()       { return v_.z(); }
	constexpr const T& w() const { return v_.w(); }
	constexpr       T& w()       { return v_.w(); }

	/** Raw 4-tuple view, useful for serialisation or interop. */
	constexpr const vector<4, T>& as_vector() const { return v_; }

	// ── Norm / length / normalize (delegate to vector) ───────────────────
	constexpr T          norm()       const { return v_.norm(); }
	constexpr T          length()     const { return v_.length(); }
	constexpr quaternion normalized() const { return quaternion(v_.normalized()); }
	constexpr void       normalize()        { v_.normalize(); }

	// ── Euler angle extraction (Tait-Bryan ZYX) ──────────────────────────
	//
	// Roll = rotation around X (last applied), pitch = around Y, yaw =
	// around Z (first applied) in an aerospace ZYX convention. The
	// inverse, building a quaternion from Euler angles, is intentionally
	// not provided here: there are too many possible conventions and the
	// caller is better off composing two @ref from_axis_angle products.

	/** Roll (rotation around the X axis), in radians. */
	T roll() const
	{
		return std::atan2(T{2} * (w()*x() + y()*z()), T{1} - T{2} * (x()*x() + y()*y()));
	}

	/**
	 * Pitch (rotation around the Y axis), in radians.
	 * Clamped to ±π/2 at the singularity (gimbal lock) to avoid @c NaN
	 * when the input quaternion is slightly non-unit and @c sin_p falls
	 * just outside @c [-1, 1] .
	 */
	T pitch() const
	{
		const T sin_p = T{2} * (w()*y() - z()*x());
		if (sin_p >= T{ 1}) return  std::numbers::pi_v<T> / T{2};
		if (sin_p <= T{-1}) return -std::numbers::pi_v<T> / T{2};
		return std::asin(sin_p);
	}

	/** Yaw (rotation around the Z axis), in radians. */
	T yaw() const
	{
		return std::atan2(T{2} * (w()*z() + x()*y()), T{1} - T{2} * (y()*y() + z()*z()));
	}

	// ── Element-wise ops on the underlying vector ────────────────────────
	constexpr quaternion operator+(const quaternion& other) const { return quaternion(v_ + other.v_); }
	constexpr quaternion operator-(const quaternion& other) const { return quaternion(v_ - other.v_); }
	constexpr quaternion operator-()                        const { return quaternion(-v_); }
	constexpr quaternion operator*(const T& factor)         const { return quaternion(v_ * factor); }
	constexpr quaternion operator/(const T& factor)         const { return quaternion(v_ / factor); }

	constexpr quaternion& operator+=(const quaternion& other) { v_ += other.v_; return *this; }
	constexpr quaternion& operator-=(const quaternion& other) { v_ -= other.v_; return *this; }
	constexpr quaternion& operator*=(const T& factor)         { v_ *= factor;   return *this; }
	constexpr quaternion& operator/=(const T& factor)         { v_ /= factor;   return *this; }

	constexpr bool operator==(const quaternion&) const = default;

private:
	vector<4, T> v_;
};

/** Scalar-on-the-left multiplication. */
template<typename T>
constexpr quaternion<T> operator*(const T& factor, const quaternion<T>& q)
{
	return q * factor;
}

/** The identity rotation, written as a free function for symmetry with the rest of the API. */
template<typename T = float>
constexpr quaternion<T> identity_quaternion()
{
	return {};
}

/**
 * @brief Hamilton product, i.e. composition of rotations.
 *
 *        Applying @c hamilton_product(q1, q2) to a vector first rotates by
 *        @c q2, then by @c q1.
 *
 * @remark Marked @c [[gnu::always_inline]] : the body is ~12 mul + 8 add/sub
 *         which is just above @c -Og 's "small function" inlining threshold.
 *         Without the hint, hot loops at @c -Og would pay a real call
 *         overhead per iteration. At @c -O2 the attribute is a no-op (the
 *         function would be inlined anyway).
 */
template<typename T>
[[gnu::always_inline]] constexpr quaternion<T> hamilton_product(const quaternion<T>& q1, const quaternion<T>& q2)
{
	return {
		q1.w() * q2.x() + q1.x() * q2.w() + q1.y() * q2.z() - q1.z() * q2.y(),
		q1.w() * q2.y() - q1.x() * q2.z() + q1.y() * q2.w() + q1.z() * q2.x(),
		q1.w() * q2.z() + q1.x() * q2.y() - q1.y() * q2.x() + q1.z() * q2.w(),
		q1.w() * q2.w() - q1.x() * q2.x() - q1.y() * q2.y() - q1.z() * q2.z()
	};
}

/** @c q1 @c * @c q2 is the Hamilton product. */
template<typename T>
[[gnu::always_inline]] constexpr quaternion<T> operator*(const quaternion<T>& q1, const quaternion<T>& q2)
{
	return hamilton_product(q1, q2);
}

/** Conjugate: @c (-x, -y, -z, w) . Equal to the inverse for a unit quaternion. */
template<typename T>
constexpr quaternion<T> conjugate(const quaternion<T>& q)
{
	return { -q.x(), -q.y(), -q.z(), q.w() };
}

/**
 * @brief Inverse: @c conjugate(q) / norm(q) .
 *        For a unit quaternion this is equal to @ref conjugate, so prefer
 *        that when you know the input is unit-length to skip four divisions.
 */
template<typename T>
constexpr quaternion<T> inverse(const quaternion<T>& q)
{
	const auto n = q.norm();
	return { -q.x() / n, -q.y() / n, -q.z() / n, q.w() / n };
}

/**
 * @brief Dot product of two quaternions, identical to the dot product of
 *        their underlying 4-vectors.
 */
template<typename T>
constexpr T dot_product(const quaternion<T>& q1, const quaternion<T>& q2)
{
	return dot_product(q1.as_vector(), q2.as_vector());
}

/**
 * @brief Build a unit quaternion that represents a rotation of @p angle
 *        radians around @p axis.
 * @pre   @p axis must be unit-length.
 */
template<typename T>
quaternion<T> from_axis_angle(const vector<3, T>& axis, T angle)
{
	const auto half = angle / T{2};
	const auto s = std::sin(half);
	return { axis.x() * s, axis.y() * s, axis.z() * s, std::cos(half) };
}

/**
 * @brief Rotate a 3D vector by a unit quaternion.
 *
 *        Optimised form of @c v' = q * v * q⁻¹ that avoids two full
 *        Hamilton products by decomposing into cross products: about
 *        12 mul / 12 add / 6 sub instead of ~28 mul / 16 add for the
 *        naive formulation.
 *
 * @pre   @p q must be unit-length.
 */
template<typename T>
[[gnu::always_inline]] constexpr vector<3, T> rotate(const vector<3, T>& v, const quaternion<T>& q)
{
	const vector<3, T> qv{ q.x(), q.y(), q.z() };
	const auto t = T{2} * cross_product(qv, v);
	return v + q.w() * t + cross_product(qv, t);
}

/** @c q @c * @c v3 rotates the 3D vector by the unit quaternion. */
template<typename T>
[[gnu::always_inline]] constexpr vector<3, T> operator*(const quaternion<T>& q, const vector<3, T>& v)
{
	return rotate(v, q);
}

/**
 * @brief Spherical linear interpolation between two unit quaternions.
 *
 *        Walks the great-circle arc on the unit hypersphere, so the
 *        intermediate quaternions stay unit-length and the angular
 *        speed stays constant. Picks the shortest of the two equivalent
 *        paths (since @c q and @c -q represent the same rotation) by
 *        negating @p q2 when the endpoints are more than 90° apart.
 *        Falls back to a normalised linear interpolation when the two
 *        endpoints are within ~1.8° of each other to avoid a division
 *        by an almost-zero @c sin(theta) .
 *
 * @pre   @p q1 and @p q2 must be unit-length.
 * @param t Interpolation factor in @c [0, 1] .
 */
template<typename T>
quaternion<T> slerp(const quaternion<T>& q1, const quaternion<T>& q2, T t)
{
	auto cos_theta = dot_product(q1, q2);
	auto q2_adj = q2;
	if (cos_theta < T{0})                       // shortest-path: pick the closer of q2 and -q2
	{
		q2_adj = -q2;
		cos_theta = -cos_theta;
	}

	if (cos_theta > static_cast<T>(0.9995))     // nearly aligned — fall back to normalised lerp
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
