# OpSy algorithms

Higher-level numerical algorithms built on top of the
[`utility`](../utility) primitives. These headers depend on
`utility/vector.hpp` and `utility/matrix.hpp` for their data types but
have no dependency on the scheduler — they are compile-time-sized,
allocate nothing on the heap, throw no exceptions, and build for a
hosted platform (handy for offline validation against reference
implementations).

One thing a host build needs: `utility/matrix.hpp` pulls in
`opsy_assert.hpp`, which *declares* `opsy::trap()` but leaves the
default definition in `config.hpp` — a header this chain never
includes. So link a definition of your own, or compile with `-DNDEBUG`,
or the build fails with an undefined reference to `opsy::trap()`. The
host test suite does the former; see
[`tests/host/host_test.cpp`](../tests/host/host_test.cpp).

Note also that including any OpSy header replaces the standard `assert`
macro with one that routes to `opsy::trap()`, for the rest of the
translation unit.

All identifiers live in `namespace opsy::algorithms`.

## Files

| File | Purpose |
|---|---|
| `ellipsoid_fit.hpp` | `ellipsoid_fit<T>` — online least-squares fit of an ellipsoid to a stream of 3D samples, intended for magnetometer hard/soft iron calibration. `feed(sample)` accumulates the sample's contribution to the 9×9 normal-equations matrix and the 9-vector of sums in 90 multiply-adds, then scans all 90 accumulator elements to decide whether they need rescaling — that scan runs on *every* call, so budget ~180 operations per sample, not 81. Rescaling itself is rare, and leaves the least-squares solution unchanged. `fit()` solves the system, extracts the ellipsoid centre (hard iron offset) and the eigen-decomposition of the quadric (soft iron transform), and packs the result into a `magnetometer_calibration<T>` whose `correct(raw)` applies the calibration. It is fallible — see the example below. `count()` reports how many samples have been fed, and `minimum_samples` is the floor below which `fit()` refuses outright. `magnetometer_calibration<T>` is trivially copyable so it can be flushed to non-volatile storage with `memcpy`. |

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

bool recalibrate(opsy::algorithms::magnetometer_calibration<float>& out)
{
    // fit() is fallible: it returns nullopt when the accumulator cannot
    // produce a meaningful ellipsoid — fewer than `minimum_samples` fed, a
    // singular system (samples collinear or coplanar), or a fit that landed
    // on a hyperboloid, which is what a partial sweep usually gives. Storing
    // an unchecked result would persist NaNs to non-volatile memory and
    // silently corrupt every later reading.
    const auto cal = fitter.fit();
    if (!cal)
        return false;

    out = *cal;  // cal->hard_iron, cal->soft_iron, cal->correct(raw)
    return true;
}
```

## Conventions

Same as OpSy core: `snake_case` identifiers, trailing `_` on members,
short STL-style template parameter names (`T`, `N`, `I`, `Is`).

## CI coverage

`tests/utility_sanity.cpp` instantiates every algorithm under the same
strict warning set as the scheduler (`-Wshadow`, `-Wcast-align`,
`-Wconversion`, `-Wsign-conversion`, `-Wdouble-promotion`, `-Werror`,
…) on every Cortex-M target in the matrix. That build never runs, so
numerical behaviour is covered separately by
[`tests/host/test_ellipsoid_fit.cpp`](../tests/host/test_ellipsoid_fit.cpp),
which fits a known ellipsoid and checks that every sample maps back
onto the unit sphere.
