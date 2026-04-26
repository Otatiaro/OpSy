# OpSy utility headers

A small set of self-contained C++23 headers shipped alongside OpSy. They
have no dependency on the scheduler — feel free to use them on a hosted
platform too.

All identifiers live in `namespace opsy::utility`. The headers are
header-only, allocate nothing on the heap, throw no exceptions, and
exercise their `static_assert` / `constexpr` checks in CI alongside the
core scheduler.

## Files

| File | Purpose |
|---|---|
| `allocator.hpp` | `allocator<N, UseDummy, Dummy>` — fixed-size, in-band-tagged allocator. Slots are `int` (negative = allocated, positive = free); free space is coalesced on `deallocate`. Optional sentinel fill helps catch use-after-free. |
| `biquad.hpp` | `biquad<T, Coef>` — second-order IIR filter (direct form II transposed). `filter_type::{low_pass, high_pass, notch, band_pass}`, Butterworth Q default, value and coefficient types are independent template parameters. |
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
