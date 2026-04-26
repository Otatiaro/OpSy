# OpSy utility headers

A small set of self-contained C++23 headers shipped alongside OpSy. Most
have no dependency on the scheduler — feel free to use them on a hosted
platform too. The only exception is `interrupt_vector.hpp`, which is
ARM Cortex-M specific by design (it builds the architectural vector
table).

All identifiers live in `namespace opsy::utility`. The headers are
header-only, allocate nothing on the heap, throw no exceptions, and
exercise their `static_assert` / `constexpr` checks in CI alongside the
core scheduler.

## Files

| File | Purpose |
|---|---|
| `allocator.hpp` | `allocator<N, UseDummy, Dummy>` — fixed-size, in-band-tagged allocator. Slots are `int` (negative = allocated, positive = free); free space is coalesced on `deallocate`. Optional sentinel fill helps catch use-after-free. |
| `biquad.hpp` | `biquad<T, Coef>` — second-order IIR filter (direct form II transposed). `filter_type::{low_pass, high_pass, notch, band_pass}`, Butterworth Q default, value and coefficient types are independent template parameters. |
| `interrupt_vector.hpp` | `interrupt_vector<PeripheralIrqs>` — compile-time builder for the ARM Cortex-M vector table. System exception slots are passed by name to the constructor; peripheral ISRs are added by chaining `.with_handler<IRQ, fn>()`. The whole expression is constant-initialisable so the table lives in `.isr_vector` in flash. **Cortex-M only.** |
| `matrix.hpp` | `matrix<Rows, Cols, T>` — fixed-size full matrix targeted at small dimensions (rotations, small Kalman blocks, ellipsoid fits up to 9×9, …). Row-major storage, element access via `m(i, j)` (Eigen-style), compile-time `m.at<I, J>()`, plus `m.row(i)` / `m.col(j)` returning the matching `vector<>`. Basic algebra: `==`, `+`, `-`, unary `-`, scalar `*` / `/` and compound forms, `transposed()`, `operator*(matrix, matrix)`, `operator*(matrix, vector)`. Row primitives `swap_rows`, `scale_row`, `add_scaled_row` for Gauss-style algorithms, plus `sub_matrix<R, C>(top, left)` and in-place `apply(f)`. `determinant()` and `inverse()` for square matrices: closed-form (1×1…4×4) or Gauss-Jordan with partial pivoting (≥ 5×5). For real symmetric square matrices, `eigenvalues()` and `symmetric_eigen_decomposition()` (Householder + QL with implicit Wilkinson shifts; eigenvalues sorted ascending). Free function `identity_matrix<N, T>()`. Multiplications and inverse are `[[gnu::always_inline]]` so they stay inlined down to `-Og`. |
| `memory.hpp` | Building blocks for typed, atomic-aware memory-mapped register layouts: `memory<TInt, TReg>` / `read_only_memory<TInt, TReg>` / `write_only_memory<TInt, TReg>` for register storage, `clear_set<TReg>` for read-modify-write masks, the `atomic` tag and `\| atomic` / `\|\| atomic` postfix syntax that promote a write to a `fetch_or` / `compare_exchange`, plus `padding<Size>` to align successive registers in a struct to the offsets the hardware expects. Used by generated peripheral header packs (RCC, GPIO, USART, ...). |
| `quaternion.hpp` | `quaternion<T>` — strong-typed unit quaternion built as a thin composition wrapper around `vector<4, T>` (xyz = i/j/k, w = scalar). Distinct from a plain `vector<4>` so `q1 * q2` (Hamilton product) and `q * v3` (rotation) are unambiguous and a `vector<4>` holding RGBA / homogeneous coordinates / etc. is not misinterpreted. Constructors from raw components, from a rotation vector (axis × angle, the form gyroscope integrators produce — zero-length input maps to identity), and shortest-arc from two unit vectors (with antiparallel handling). Includes `identity_quaternion`, `hamilton_product`, `conjugate`, `inverse`, `from_axis_angle`, `rotate`, `slerp`, Tait-Bryan ZYX extraction `roll()` / `pitch()` / `yaw()` (pitch clamped at the gimbal-lock singularity), plus the usual element-wise `+`, `-`, scalar `*`/`/`, `norm`/`length`/`normalize` (used internally by `slerp`). The wrapper costs zero at any optimisation level: `[[gnu::always_inline]]` on the hot ops keeps them inlined down to `-Og` — verified byte-for-byte against a hand-written direct-on-`vector<4>` version. |
| `slope.hpp` | `slope<N, T, Coef>` — FIR numerical derivative based on a least-squares linear fit over `N` samples. Coefficients are computed at compile time, so `value()` is a fixed-size dot product. Mean delay is `N/2` samples. |
| `vector.hpp` | `vector<N, T>` — compile-time-sized math vector with the usual operators (`+`, `-`, `*`, `/`), `norm()` / `length()` / `normalized()`, typed `x()` / `y()` / `z()` / `w()` accessors constrained by size, plus `append` / `prepend` / `sub`, `dot_product`, `cross_product`, `rotate`. |

## Use

If the OpSy repository root is on your include path (as suggested in the
top-level README), reach the headers via `<utility/...>`:

```cpp
#include <utility/biquad.hpp>
#include <utility/vector.hpp>

opsy::utility::biquad<float> filter{
    48000.0f, 1000.0f, opsy::utility::filter_type::low_pass};

constexpr opsy::utility::vector<3> v{1.0f, 2.0f, 3.0f};
static_assert(v.norm() == 14.0f);
```

## Conventions

Same as OpSy core: `snake_case` identifiers, trailing `_` on members,
short STL-style template parameter names (`T`, `N`, `Coef`, `I`, `Is`).

## CI coverage

`tests/utility_sanity.cpp` instantiates every template under the same
strict warning set as the scheduler (`-Wshadow`, `-Wcast-align`,
`-Wconversion`, `-Wsign-conversion`, `-Wdouble-promotion`, `-Werror`,
…) on every Cortex-M target in the matrix.
