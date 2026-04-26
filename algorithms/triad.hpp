/**
 ******************************************************************************
 * @file    triad.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   Attitude from two orthogonal body-frame reference directions
 *
 *          @ref attitude_from_down_and_east takes the body-frame
 *          measurements of the world's @c +Z (Down) and @c +Y (East)
 *          axes and returns the unique unit quaternion @c q that maps
 *          one onto the other:
 *
 *              q · (+Z) = down
 *              q · (+Y) = east
 *
 *          Typical IMU / AHRS use in the NED reference frame:
 *
 *              down = accel.normalized();          // +Z in NED
 *              east = cross_product(accel, mag).normalized();
 *                                                  // +Y in NED — the
 *                                                  // cross product is
 *                                                  // perpendicular to
 *                                                  // gravity by
 *                                                  // construction, so
 *                                                  // we don't care
 *                                                  // that the magnetic
 *                                                  // field has a
 *                                                  // vertical (dip)
 *                                                  // component.
 *
 *          Why unique: a 3D rotation has 3 DoF. Fixing the image of one
 *          unit vector pins 2 DoF (the direction it lands on). The
 *          second image fixes the remaining DoF (the rotation around
 *          that direction). Orthogonality of @p down and @p east is the
 *          consistency condition — rotations preserve angles, and
 *          @c +Z ⊥ @c +Y at @c 90° , so the targets must be at @c 90°
 *          too.
 *
 * @tparam T Scalar type (default @c float ).
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

#include <utility/quaternion.hpp>
#include <utility/vector.hpp>

namespace opsy::algorithms
{

/**
 * @brief Build the unique unit quaternion that maps the world @c +Z and
 *        @c +Y axes to the supplied body-frame directions.
 *
 *        Computed as a composition of two shortest-arc rotations:
 *          1. @c q1 takes @c +Z onto @p down . This pins where the
 *             body's "vertical" axis points but leaves a free rotation
 *             around it.
 *          2. After @c q1 , the world @c +Y axis has landed on
 *             @c q1·(+Y) , somewhere in the plane perpendicular to
 *             @p down (because rotations preserve angles, and @c +Z ⊥
 *             @c +Y ). @p east lies in the same plane (because we
 *             require @p down ⊥ @p east ). The shortest arc from
 *             @c q1·(+Y) to @p east therefore tours around @p down
 *             itself — exactly the residual DoF we need to fix.
 *          3. Compose: @c q @c = @c q2 @c * @c q1 .
 *
 *        Edge cases of the underlying shortest-arc constructor (input
 *        antiparallel to its target) are absorbed at composition time:
 *        whatever perpendicular axis @c q1 picks for the @c +Z → @p
 *        down 180° case, @c q2 corrects so the final @c q is the
 *        unique rotation satisfying both constraints.
 *
 * @pre   @p down and @p east must be unit-length and mutually
 *        perpendicular.
 */
template<typename T = float>
utility::quaternion<T> attitude_from_down_and_east(const utility::vector<3, T>& down, const utility::vector<3, T>& east)
{
	const utility::vector<3, T> pos_z{ T{0}, T{0}, T{1} };
	const utility::vector<3, T> pos_y{ T{0}, T{1}, T{0} };

	const utility::quaternion<T> q1{ pos_z, down };
	const auto rotated_pos_y = q1 * pos_y;
	const utility::quaternion<T> q2{ rotated_pos_y, east };

	return q2 * q1;
}

}
