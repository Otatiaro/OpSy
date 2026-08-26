# OpSy tests

Three suites, answering three different questions.

| Suite | Question | Runs code? |
|---|---|---|
| [Cortex-M sanity](#cortex-m-sanity-build) (this directory) | does the whole API still compile, on every target, under a strict warning set? | no |
| [Host tests](#host-tests) (`host/`) | does the portable half of OpSy still *behave*? | yes, natively |
| [QEMU tests](#qemu-tests) (`qemu/`) | does the *scheduler* behave, on a real Cortex-M? | yes, emulated |

The cross build cannot check behaviour: it produces a static library
that is never linked or run. The host suite covers the part of OpSy
that is plain C++ — the containers, the allocator, the numerics. The
QEMU suite covers the rest: task switching, service calls, condition
variable timeouts, all of which only exist once the scheduler owns a
CPU.

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

## QEMU tests

On-target suite in [`qemu/`](qemu), booted on an emulated Cortex-M.

```sh
cmake -S tests/qemu -B build-qemu -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake" \
      -DOPSY_COMPILER=gcc \
      -DOPSY_TARGET=m4
cmake --build build-qemu --parallel
ctest --test-dir build-qemu --output-on-failure
```

Needs `qemu-system-arm` on `PATH`. Each `OPSY_TARGET` maps to the board
QEMU models for it:

| Target | Machine | Linker script |
|---|---|---|
| `m3` | `mps2-an385` | `armv7m.ld` |
| `m4` | `mps2-an386` | `armv7m.ld` |
| `m7` | `mps2-an500` | `armv7m.ld` |
| `m33` | `mps2-an505` | `armv8m.ld` |

The image is a real one: `startup.cpp` brings up `.data`, `.bss` and the
static constructors, relocates the vector table into RAM and points VTOR
at it, then hands over to `scheduler::start`. Cases run from an OpSy
task, so they can block, sleep, and start or stop other tasks. I/O is
ARM semihosting — no UART, no C library.

Cases run back to back on one runner task, so each must leave the system
as it found it: no task left started, no mutex left locked. The runner
raises itself to `task_priority::high` once running; tasks start at
`lowest`, so a freshly started helper cannot run until the runner
blocks. That is what lets a case observe a helper *before* it has had
any chance to execute. A helper that must preempt the runner is raised
to `highest` explicitly.

`test_scheduler.cpp` covers scheduler liveness and priority ordering,
condition variable timeouts and notification, mutex serialisation,
`sleep_until`, task termination and reuse, and the four regressions that
needed a running scheduler to reproduce: `wait_until` with an elapsed
deadline, `sleep_until` with one, a terminated task left linked into
`ready_`, and a stale `waiting_` pointer after a wait timed out.

Two notes on the plumbing, both learned the hard way:

- On AArch32, plain `SYS_EXIT` takes the reason code *in r1*, not a
  pointer — the `{reason, code}` block is `SYS_EXIT_EXTENDED`. Getting
  that wrong makes QEMU exit 1 on a fully passing run.
- QEMU writes semihosting output to *its own stderr*, and collapses
  every non-zero application exit code to a process status of 1. So
  `run_qemu.cmake` looks for a `RESULT:` line across both streams and
  treats its absence as a failure — otherwise an image that hangs or
  faults halfway would read as a pass.

`mps2-an505` needs its own linker script for a second reason beyond
TrustZone addressing: its SRAM at `0x30000000` sits behind a Memory
Protection Controller that comes up blocking, so the very first stack
push faults before any instruction of the image runs. Rather than
programming the SSE-200 MPCs, the image puts both code and data in the
Secure ZBT SRAM at `0x10000000`.

## CI

Every push and pull request to `master` runs all three suites on
`ubuntu-latest`: the `{gcc, clang} × {m3, m4, m7, m33}` cross matrix,
the host tests under `gcc` and `clang`, and the QEMU tests on all four
targets. See [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) —
the workflow is the canonical spec, and local builds should match its
configure lines.

## What is still not covered

Narrow races. `try_critical_section`'s test-then-set window and
`add_task`'s unmasked list mutation are a few instructions wide; QEMU
runs deterministically and will not hit them on its own. Reproducing
them means driving the interleaving by hand — pending SysTick through
`ICSR` at a chosen instruction — which mostly proves the test was
written correctly.

The `ticks_` torn read needs the 32-bit low word to wrap: ~49.7 days of
simulated time at 1 ms, or an API to seed the counter near the boundary.

The non-`volatile` MMIO and the missing `"memory"` clobber are
optimisation bugs, not execution bugs. Emulation only shows them if the
compiler happens to take the dangerous transformation; the disassembly
is the tool for those.
