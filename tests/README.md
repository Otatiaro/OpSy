# OpSy compile-only sanity tests

Compile-only sanity build that exercises the entire OpSy public API and
the `utility` / `algorithms` headers on every Cortex-M target supported
by OpSy. No executable is produced — the result is a static library, so
no startup file or linker script is needed. The point is to keep the
template surface honest under a strict warning set, not to run code on
hardware.

## What gets built

`scheduler.cpp` plus the two translation units in this directory are
compiled into a single static library, `opsy_sanity`:

| File | What it covers |
|---|---|
| `sanity.cpp` | One instance of every public OpSy primitive (`task`, `idle_task`, `mutex`, `condition_variable`, `scheduler`, `cortex_m`, `isr_priority`, `callback`, `sleep_for` / `sleep_until`). The functions are kept alive with `[[gnu::used]]` so `-ffunction-sections` / `--gc-sections` does not eliminate them before the compiler has typechecked the call sites. |
| `utility_sanity.cpp` | Instantiates every `utility/*.hpp` and `algorithms/*.hpp` template with valid configurations to fire the in-class `static_assert` checks, plus extra `static_assert` blocks on the `constexpr` surface (sizes, accessors, `at`, `append`, `prepend`, `sub`, arithmetic operators, …). Runtime-only paths (`std::sqrt`, `std::tan`, eigen-decomposition, `slerp`, `from_axis_angle`, …) are kept alive with `[[gnu::used]]` so they go through the front end too. |
| `scheduler.cpp` | The single OpSy translation unit, pulled in from `..` so the `PendSV` / `SVC` / `SysTick` handlers and the inline assembly are exercised on each `-mcpu`. |

The strict warning set used by both files matches what production OpSy
projects run with: `-Wall -Werror -Wpedantic -Wextra -Wshadow
-Wcast-align -Wcast-qual -Wnull-dereference -Wconversion
-Wsign-conversion -Wdouble-promotion`, with `-fno-exceptions -fno-rtti`,
plus the GCC-only `-Wlogical-op -Wduplicated-cond -Wduplicated-branches`
when the compiler is GCC.

## Building locally

```sh
cmake -S tests -B tests/build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc \
      -DOPSY_TARGET=m33
cmake --build tests/build --parallel
```

`OPSY_TARGET` selects the Cortex-M variant (`m3`, `m4`, `m7`, `m33`);
the matching `-mcpu` / `-mfpu` / `-mfloat-abi` flags are added by
`CMakeLists.txt`. `OPSY_COMPILER` selects the toolchain family (`gcc`
or `clang`); both expect the corresponding `arm-none-eabi-gcc` or
`clang` (with LLVM Embedded Toolchain for Arm) to be on `PATH`. The
in-tree `build-m3` / `build-m4` / `build-m7` / `build-m33` /
`build-O2` / `build-asm` directories are scratch build trees used
during local iteration — not consumed by anything else.

## CI

Every push and pull request to `master` builds the full
`{compiler} × {target}` matrix (`gcc` / `clang` × `m3` / `m4` / `m7` /
`m33`) on `ubuntu-latest`. See [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).
The workflow is the canonical spec — local builds should match its
configure line.
