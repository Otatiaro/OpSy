/**
 ******************************************************************************
 * @file    ellipsoid_fit.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    26-April-2026
 * @brief   Online ellipsoid least-squares fit for magnetometer calibration
 *
 *          Feed raw magnetometer samples one at a time; the accumulator
 *          updates a 9x9 normal-equations matrix and a 9-vector of sums.
 *          @ref ellipsoid_fit::fit solves the system, extracts the
 *          ellipsoid centre (hard iron offset) and the eigen-decomposition
 *          of the quadric (soft iron transform), and packs the result in a
 *          @ref magnetometer_calibration that the caller can apply to
 *          subsequent samples or persist (e.g. to EEPROM).
 *
 *          The accumulators are rescaled in place when any element crosses
 *          a saturation threshold. The least-squares solution
 *          @c v @c = @c inverse(DᵀD) @c * @c sums is invariant under a
 *          common rescale of @c DᵀD and @c sums , so this preserves the
 *          fit while keeping the magnitudes bounded for any plausible
 *          accumulation length.
 *
 * @tparam T Scalar type (default @c float ). @c double is supported for
 *           host-side validation; on-target, @c float matches the
 *           hardware FPU and is the intended choice.
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
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

#include <utility/matrix.hpp>
#include <utility/vector.hpp>

namespace opsy::algorithms
{

/**
 * @brief Magnetometer calibration parameters: hard iron bias + soft iron
 *        transform.
 *
 *        @c correct(raw) returns the calibrated reading @c soft_iron @c *
 *        @c (raw @c - @c hard_iron) which, when the calibration is
 *        accurate, lies on the unit sphere centred at the origin.
 *
 *        Trivially copyable (the @c static_assert below pins this) so the
 *        struct can be flushed straight to non-volatile storage with
 *        @c memcpy and restored at boot the same way.
 *
 * @tparam T Scalar type (default @c float ).
 */
template<typename T = float>
struct magnetometer_calibration
{
	utility::vector<3, T>     hard_iron{};   ///< bias subtracted from each raw sample
	utility::matrix<3, 3, T>  soft_iron{};   ///< transform that maps the bias-corrected sample back onto a sphere

	/** Apply the calibration to a raw magnetometer reading. */
	constexpr utility::vector<3, T> correct(const utility::vector<3, T>& raw) const
	{
		return soft_iron * (raw - hard_iron);
	}
};

/**
 * @brief Online least-squares fit of an ellipsoid to a stream of 3D
 *        samples, intended for magnetometer hard/soft iron calibration.
 *
 *        @ref feed accumulates a single sample's contribution to the
 *        9x9 @c DᵀD normal-equations matrix and the 9-vector of sums,
 *        in O(81) flops. After enough well-spread samples have been fed,
 *        @ref fit solves the system and returns a usable
 *        @ref magnetometer_calibration .
 *
 *        The default template argument @c T = @c float matches the
 *        Cortex-M FPU; @c double is supported for host-side validation
 *        but does not match any single-precision FPU and falls back to
 *        software emulation on most targets.
 *
 * @tparam T Scalar type (default @c float ).
 */
template<typename T = float>
class ellipsoid_fit
{
public:

	/**
	 * @brief Add one sample to the running accumulators.
	 *
	 *        Cost: 9 + 81 = 90 multiplies/adds for the symmetric outer
	 *        product, plus an O(N) overflow check on the resulting
	 *        accumulator. The rescale path almost never fires for
	 *        nominal magnetometer-calibration inputs (a few kHz over
	 *        ~10 s of µT-range readings) but bounds growth on
	 *        pathological inputs (raw int16 counts, runaway sample
	 *        counts).
	 */
	void feed(const utility::vector<3, T>& sample)
	{
		const T x = sample.x();
		const T y = sample.y();
		const T z = sample.z();

		// Quadric design row: each sample contributes one row to the
		// design matrix D, and we accumulate DᵀD and Dᵀ·1 directly.
		const std::array<T, 9> coefs{
			x * x, y * y, z * z,
			T{2} * x * y, T{2} * x * z, T{2} * y * z,
			T{2} * x,     T{2} * y,     T{2} * z
		};

		// Symmetric outer product accumulation. Diagonal first, then the
		// strict upper triangle mirrored across — saves one multiply per
		// off-diagonal pair while keeping the matrix symmetric exactly.
		for (std::size_t i = 0; i < 9; ++i)
		{
			sums_[i]     += coefs[i];
			dtd_(i, i)   += coefs[i] * coefs[i];
		}
		for (std::size_t i = 0; i < 9; ++i)
			for (std::size_t j = 0; j < i; ++j)
			{
				const T value = coefs[i] * coefs[j];
				dtd_(i, j) += value;
				dtd_(j, i) += value;
			}

		rescale_if_saturating();
	}

	/**
	 * @brief Solve the accumulated normal equations and return the
	 *        calibration.
	 *
	 *        Steps:
	 *          1. @c v @c = @c inverse(DᵀD) @c * @c sums solves the
	 *             least-squares system for the nine ellipsoid coefficients.
	 *          2. The coefficients are reshaped into the 4×4 quadric
	 *             algebraic matrix.
	 *          3. The ellipsoid centre is the offset that makes the linear
	 *             part of the quadric vanish — this is the @c hard_iron .
	 *          4. The quadric is translated to the centre, the upper-left
	 *             3×3 (the pure quadratic part) is normalised, and its
	 *             eigen-decomposition gives the principal axes (eigenvectors)
	 *             and squared semi-axes (eigenvalues).
	 *          5. @c soft_iron @c = @c V @c * @c diag(1/sqrt(λ)) @c * @c Vᵀ
	 *             is the unique symmetric matrix that maps the ellipsoid
	 *             back onto the unit sphere.
	 *
	 *        Requires at least nine well-spread samples; degenerate inputs
	 *        (all collinear, all on a plane, ...) yield a singular
	 *        @c DᵀD and the result is undefined.
	 */
	magnetometer_calibration<T> fit() const
	{
		// 1. Solve normal equations: v = inverse(DᵀD) * sums.
		const utility::vector<9, T> v = dtd_.inverse() * sums_;

		// 2. Reshape v into the 4x4 algebraic representation of the quadric:
		//
		//        | A  D  E  G |
		//    Q = | D  B  F  H |   with v = (A, B, C, D, E, F, G, H, I)
		//        | E  F  C  I |
		//        | G  H  I  -1|
		utility::matrix<4, 4, T> algebraic{
			v[0], v[3], v[4], v[6],
			v[3], v[1], v[5], v[7],
			v[4], v[5], v[2], v[8],
			v[6], v[7], v[8], T{-1}
		};

		// 3. Find the ellipsoid centre: solve A·c = -b where A is the
		//    upper-left 3x3 of the quadric and b is the linear part
		//    (column 3 of the first three rows).
		const utility::matrix<3, 3, T> upper_3x3 = algebraic.template sub_matrix<3, 3>(0, 0);
		const utility::vector<3, T>    linear{ algebraic(0, 3), algebraic(1, 3), algebraic(2, 3) };
		const utility::vector<3, T>    hard_iron = -(upper_3x3.inverse() * linear);

		// 4. Translate quadric so the centre sits at the origin:
		//    Q' = T · Q · Tᵀ  with  T(3, i) = hard_iron(i) for i ∈ 0..2 .
		utility::matrix<4, 4, T> translation{
			T{1}, T{0}, T{0}, T{0},
			T{0}, T{1}, T{0}, T{0},
			T{0}, T{0}, T{1}, T{0},
			hard_iron.x(), hard_iron.y(), hard_iron.z(), T{1}
		};
		const utility::matrix<4, 4, T> centred = translation * algebraic * translation.transposed();

		// 5. Normalise the upper-left 3x3 of the centred quadric by the
		//    constant term so that its eigenvalues are 1 / (semi-axis)² .
		const T ratio = -centred(3, 3);
		utility::matrix<3, 3, T> normalised = centred.template sub_matrix<3, 3>(0, 0);
		normalised /= ratio;

		// 6. Eigen-decompose: V·diag(λ)·Vᵀ where columns of V are the
		//    principal axes and λ are the squared inverse semi-axes.
		const auto eigen = normalised.symmetric_eigen_decomposition();

		// 7. Build soft_iron = V · diag(1/√λ) · Vᵀ , the unique symmetric
		//    matrix that maps the ellipsoid back onto the unit sphere.
		utility::vector<3, T> inv_sqrt_lambda{
			T{1} / std::sqrt(eigen.values[0]),
			T{1} / std::sqrt(eigen.values[1]),
			T{1} / std::sqrt(eigen.values[2])
		};
		utility::matrix<3, 3, T> soft_iron;
		for (std::size_t i = 0; i < 3; ++i)
			for (std::size_t j = 0; j < 3; ++j)
			{
				T sum = T{0};
				for (std::size_t k = 0; k < 3; ++k)
					sum += eigen.vectors(i, k) * eigen.vectors(j, k) * inv_sqrt_lambda[k];
				soft_iron(i, j) = sum;
			}

		return { hard_iron, soft_iron };
	}

	/**
	 * @brief Reset the accumulators to the empty state.
	 *
	 *        Useful when starting a fresh calibration session without
	 *        re-allocating the object.
	 */
	void reset()
	{
		dtd_  = utility::matrix<9, 9, T>{};
		sums_ = utility::vector<9, T>{};
	}

private:

	/**
	 * @brief Rescale @c dtd_ and @c sums_ in place when any element
	 *        crosses the saturation threshold.
	 *
	 *        The least-squares solution is invariant under a common
	 *        rescale of @c DᵀD and @c sums (both sides of the normal
	 *        equation get the same factor), so we can bring magnitudes
	 *        back to ~1 without affecting the fit. The rescale fires
	 *        rarely — only when accumulators have grown across many
	 *        orders of magnitude — so the cost is amortised.
	 */
	void rescale_if_saturating()
	{
		T max_abs = T{0};
		for (std::size_t i = 0; i < 9; ++i)
		{
			const T a = sums_[i] < T{0} ? -sums_[i] : sums_[i];
			if (a > max_abs) max_abs = a;
		}
		for (std::size_t i = 0; i < 9; ++i)
			for (std::size_t j = 0; j < 9; ++j)
			{
				const T a = dtd_(i, j) < T{0} ? -dtd_(i, j) : dtd_(i, j);
				if (a > max_abs) max_abs = a;
			}

		if (max_abs > rescale_threshold)
		{
			const T factor = T{1} / max_abs;
			dtd_  *= factor;
			sums_ *= factor;
		}
	}

	// Trigger rescaling when accumulators exceed (max / 1e8). Leaves
	// eight orders of magnitude of headroom for the next sample's outer
	// product before float overflow could occur.
	static constexpr T rescale_threshold = std::numeric_limits<T>::max() / static_cast<T>(1e8);

	utility::matrix<9, 9, T> dtd_{};
	utility::vector<9, T>    sums_{};
};

// Magnetometer calibration must round-trip through EEPROM via memcpy, so
// it must be trivially copyable. A change to vector / matrix / the struct
// itself that breaks this property would fail compilation here, which is
// exactly the regression we want to catch.
static_assert(std::is_trivially_copyable_v<magnetometer_calibration<float>>);
static_assert(std::is_trivially_copyable_v<magnetometer_calibration<double>>);

}
