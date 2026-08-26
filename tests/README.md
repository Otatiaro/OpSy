# OpSy tests

Two suites, answering two different questions.

| Suite | Question | Runs code? |
|---|---|---|
| [Cortex-M sanity](#cortex-m-sanity-build) (this directory) | does the whole API still compile, on every target, under a strict warning set? | no |
| [Host tests](#host-tests) (`host/`) | does the portable half of OpSy still *behave*? | yes |

The cross build cannot check behaviour: it produces a static library
that is never linked or run. The host suite covers what that leaves
open, for the part of OpSy that is plain C++ — the containers, the
allocator, the numerics.

## Cortex-M sanity build

Compile-only build that exercises the entire OpSy public API and the
`utility` / `algorithms` headers on every Cortex-M target supported by
OpSy. No executable is produced, so no startup file or linker script is
needed. The point is to keep the template surface honest under a strict
warning set, not to run code on hardware.

### What gets built

`scheduler.cpp` plus the two translation units in this directory are
compiled into a single static library, `opsy_sanity`:

| File | What it covers |
|---|---|
| `sanity.cpp` | One instance of every public OpSy primitive (`task`, `idle_task`, `mutex`, `condition_variable`, `scheduler`, `cortex_m`, `isr_priority`, `callback`, `sleep_for` / `sleep_until`). The functions are kept alive with `[[gnu::used]]` so `-ffunction-sections` / `--gc-sections` does not eliminate them before the compiler has typechecked the call sites. |
| `utility_sanity.cpp` | Instantiates the `utility/*.hpp` and `algorithms/*.hpp` templates with valid configurations to fire the in-class `static_assert` checks, plus extra `static_assert` blocks on the `constexpr` surface (sizes, accessors, `at`, `append`, `prepend`, `sub`, arithmetic operators, …). Runtime-only paths (`std::sqrt`, `std::tan`, eigen-decomposition, `slerp`, `from_axis_angle`, …) are kept alive with `[[gnu::used]]` so they go through the front end too. Every header in those two directories is included here — if you add one, add it to the include list, or nothing in the repository will ever compile it. |
| `scheduler.cpp` | The single OpSy translation unit, pulled in from `..` so the `PendSV` / `SVC` / `SysTick` handlers and the inline assembly are exercised on each `-mcpu`. |

The strict warning set matches what production OpSy projects run with:
`-Wall -Werror -Wpedantic -Wextra -Wshadow -Wcast-align -Wcast-qual
-Wnull-dereference -Wconversion -Wsign-conversion -Wdouble-promotion`,
with `-fno-exceptions -fno-rtti`, plus the GCC-only `-Wlogical-op
-Wduplicated-cond -Wduplicated-branches` when the compiler is GCC.

### Building locally

```sh
cmake -S tests -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake" \
      -DOPSY_COMPILER=gcc \
      -DOPSY_TARGET=m33
cmake --build build --parallel
```

`CMAKE_TOOLCHAIN_FILE` must be absolute (or relative to the *source*
tree, i.e. just `cortex-m-toolchain.cmake`): CMake resolves a relative
value against the build tree and then the source tree, never the
directory you invoked it from.

`OPSY_TARGET` selects the Cortex-M variant (`m3`, `m4`, `m7`, `m33`);
the matching `-mcpu` / `-mfpu` / `-mfloat-abi` flags are added by
`CMakeLists.txt`. `OPSY_COMPILER` selects the toolchain family (`gcc`
or `clang`); both expect the corresponding `arm-none-eabi-gcc` or
`clang` (with LLVM Embedded Toolchain for Arm) to be on `PATH`.

## Host tests

Behavioural suite in [`host/`](host), built and run natively.

```sh
cmake -S tests/host -B build-host -G Ninja
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

Each case pins behaviour that the cross build cannot see, and the ones
marked as regressions fail on the code that preceded their fix — which
is the property that makes them worth keeping.

| File | What it covers |
|---|---|
| `test_allocator.cpp` | Chunk merging and arena coherence, including the max-sized allocation whose split leaves a zero-payload trailing chunk, an exhaustive single-allocation size sweep, and deterministic LIFO traffic across 64 seeds. |
| `test_embedded_list.cpp` | `erase` from every position, erasing an item that heads a *different* list, `clear` leaving nodes reusable, const iteration through `begin() const` / `cbegin()` / range-for, and move construction. |
| `test_callback.cpp` | That assignment destroys the functor it replaces, for both the functor and the `callback` overloads, and that a held functor is released at scope exit. |
| `test_ellipsoid_fit.cpp` | That a known ellipsoid is mapped back onto the unit sphere (the direction of the `soft_iron` transform), and that degenerate accumulators — empty, too few samples, coplanar, collinear — are rejected rather than yielding NaN. |
| `test_biquad.cpp` | DC gain per filter type, `reset` reaching its requested output, and that static and automatic storage agree. |

The harness is [`host_test.hpp`](host/host_test.hpp) / `host_test.cpp`:
about a hundred lines, no dependency. Tests register themselves at
static-init time, so adding a case is `OPSY_TEST(name) { ... }` and
adding the file to `host/CMakeLists.txt`. Use `CHECK` / `CHECK_NEAR`,
never `assert` — OpSy installs its own `assert` macro, which routes to
`opsy::trap` and aborts the run.

`host_test.cpp` also defines `opsy::trap`. `opsy_assert.hpp` only
declares it; the default definition lives in `config.hpp`, which a host
build including a single `utility` header never pulls in.

The suite is built 32-bit. `utility/allocator.hpp` requires
`sizeof(int) == sizeof(void*)`, true on every Cortex-M target and on no
64-bit host, and a 32-bit build also keeps the pointer arithmetic under
test at target width. Pass `-DOPSY_HOST_TESTS_M32=OFF` to build native
width instead; the allocator cases are skipped in that configuration.

## CI

Every push and pull request to `master` runs both suites on
`ubuntu-latest`: the full `{gcc, clang} × {m3, m4, m7, m33}` cross
matrix, and the host tests under `gcc` and `clang`. See
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) — the
workflow is the canonical spec, and local builds should match its
configure lines.

## What is not covered

The scheduler's concurrency paths — context switching, the service
calls, priority inheritance, timeout expiry — are exercised for
compilation only. Nothing here runs them, so bugs that need two tasks
and an interrupt to manifest are found by review and by reading the
disassembly, not by this suite. Closing that gap needs an emulator
(QEMU `-machine mps2-an385` or similar) running a real image.
