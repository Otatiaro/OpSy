# OpSy algorithms

Higher-level numerical algorithms built on top of the
[`utility`](../utility) primitives. These headers depend on
`utility/vector.hpp` / `utility/matrix.hpp` / `utility/quaternion.hpp` for
their data types but have no dependency on the scheduler — they are
compile-time-sized, allocate nothing on the heap, throw no exceptions,
and run unchanged on a hosted platform (handy for offline validation
against reference implementations).

All identifiers live in `namespace opsy::algorithms`.

## Files

| File | Purpose |
|---|---|
| `ellipsoid_fit.hpp` | `ellipsoid_fit<T>` — online least-squares fit of an ellipsoid to a stream of 3D samples, intended for magnetometer hard/soft iron calibration. `feed(sample)` accumulates the sample's contribution to the 9×9 normal-equations matrix and the 9-vector of sums in O(81) flops, with an in-band rescale path that bounds the magnitudes for any plausible accumulation length without changing the least-squares solution. `fit()` solves the system, extracts the ellipsoid centre (hard iron offset) and the eigen-decomposition of the quadric (soft iron transform), and packs the result into a `magnetometer_calibration<T>` whose `correct(raw)` applies the calibration. `magnetometer_calibration<T>` is trivially copyable so it can be flushed to non-volatile storage with `memcpy`. |

## Use

If the OpSy repository root is on your include path, reach the headers
via `<algorithms/...>`:

```cpp
#include <algorithms/ellipsoid_fit.hpp>
#include <utility/vector.hpp>

opsy::algorithms::ellipsoid_fit<float> fitter;

void on_mag_sample(const opsy::utility::vector<3, float>& raw)
{
    fitter.feed(raw);
}

void recalibrate()
{
    const auto cal = fitter.fit();
    // cal.hard_iron, cal.soft_iron, cal.correct(raw)
}
```

## Conventions

Same as OpSy core: `snake_case` identifiers, trailing `_` on members,
short STL-style template parameter names (`T`, `N`, `I`, `Is`).

## CI coverage

`tests/utility_sanity.cpp` instantiates every algorithm under the same
strict warning set as the scheduler (`-Wshadow`, `-Wcast-align`,
`-Wconversion`, `-Wsign-conversion`, `-Wdouble-promotion`, `-Werror`,
…) on every Cortex-M target in the matrix.
