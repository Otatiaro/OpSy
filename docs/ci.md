# Continuous integration

What the CI checks, why it is arranged this way, and — the part that
matters if you are changing OpSy — where your new file has to be
mentioned so that something actually compiles and runs it.

One workflow, [`.github/workflows/ci.yml`](../.github/workflows/ci.yml),
triggered on pushes to `master` and on pull requests targeting it. It is
not triggered on pushes to other branches, so **work on a branch is not
checked until you open the pull request**.

36 jobs, all running in parallel on `ubuntu-latest`. A full run takes
about a minute and a half. `fail-fast` is off everywhere: one broken
axis does not cancel the others, so a single run tells you whether a
failure is specific to one compiler or target, or general.

## The five groups of jobs

| Job group | Axes | Count | What a green run means |
|---|---|---|---|
| **Build** | `{gcc, clang}` × `{m3, m4, m7, m33}` × `{c++23, c++26}` | 16 | The whole public API and every `utility/` and `algorithms/` header compiles, on both compilers, all four cores, both standards, under a strict warning set with `-Werror`. Nothing is linked or run. |
| **Host tests** | `{gcc, clang}` × `{c++23, c++26}` | 4 | The portable half of OpSy — containers, allocator, callback, numerics — behaves. Built and run natively, 32-bit. |
| **QEMU and GDB tests** | `{m3, m4, m7, m33}` | 4 | The scheduler behaves on an emulated Cortex-M: 81 on-target cases, 6 debugger-driven scenarios, and the no-heap check on the linked image. Built with no `-O`. |
| **QEMU tests, optimised** | `{m3, m4, m7, m33}` × `{-Os, -O3 + LTO}` | 8 | The same on-target cases and no-heap check, once the optimiser is allowed to reorder, cache, eliminate and inline across translation units. |
| **Codegen checks** | `{m3, m4, m7, m33}` | 4 | The optimiser left the memory-mapped accesses and the stack alignment alone. Built at `-O2` and read out of the disassembly. |

[`tests/README.md`](../tests/README.md) describes what each suite covers
and how to run it locally. This document is about the CI around them.

### Why these axes

**Two compilers.** gcc and clang disagree about what they accept in
inline assembly, about naked functions, and about which parts of the
freestanding standard library exist at all. Each of those has broken a
build here that the other compiler was happy with.

**Four cores.** `m3` has no FPU, `m4` and `m7` have different ones,
`m33` is ARMv8-M. The PendSV handler compiles a different register save
for each, and the QEMU boards differ (`mps2-an385`, `-an386`, `-an500`,
`-an505`).

**Two standards.** OpSy targets C++23. C++26 is built alongside so that
a construct the newer standard changes or removes is caught here rather
than by a user on a newer toolchain.

**Two optimisation levels, and LTO.** A whole class of defect exists
only once the optimiser is turned on: a memory-mapped register read
without `volatile` gets cached or folded, inline assembly without a
`"memory"` clobber lets the compiler keep a stale global in a register
across a service call, a naked function is assumed to have a frame. None
of that shows in an unoptimised build. `-Os` and `-O3` sit at opposite
ends of what the optimiser will do, and LTO adds the cross-translation-
unit dimension neither has alone, so `-O2` in the middle is bracketed by
the two.

The GDB scenarios do not run in those jobs, and take themselves out of
the test list rather than failing: they stop the CPU at functions by
name, and an optimised build has inlined those functions into their
callers. There is no breakpoint to set. The on-target cases have no such
limit, which is why they are what covers the optimised configuration.

**Only gcc runs code.** The clang axis is compile-only. Running the
suites under clang as well would be worth having; the linker scripts and
startup are set up for the GNU toolchain, and nobody has done that work.

## What is pinned, and why

**GCC ARM toolchain, 14.3.Rel1**, via
`carlosperate/arm-none-eabi-gcc-action`. Chosen to match what
STM32CubeIDE 2.2.0 ships, so the compiler that builds the CI is the one
most users will build with.

**LLVM Embedded Toolchain for Arm, 19.1.5**, installed by downloading
the upstream release tarball directly rather than through an action.
Two reasons, both learned the hard way: the action that used to do it
`apt`-installs `libtinfo5`, which Ubuntu 24.04 no longer ships, so every
clang job died before compiling anything; and it capped the version at
19.1.1. The step also symlinks `libtinfo.so.6` to `.so.5` if — and only
if — `clang --version` fails without it.

**g++-14 explicitly, on the host C++26 axis.** The runner's default GNU
compiler does not know `c++26` and fails at configure time with "the
current compiler GNU does not support this". The clang on the runner is
recent enough as-is.

**32-bit host tests.** `utility/allocator.hpp` requires `sizeof(int) ==
sizeof(void*)`, true on every Cortex-M target and on no 64-bit host, and
a 32-bit build keeps the pointer arithmetic under test at target width.
Hence `gcc-multilib` / `g++-multilib`. `-DOPSY_HOST_TESTS_M32=OFF`
builds native width and drops the allocator cases.

**`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`** is set workflow-wide because
some actions still ship a Node.js 20 manifest, and Node.js 20 is
deprecated on GitHub Actions.

## Changing OpSy: where your change has to be mentioned

Nothing here discovers files on its own — with one exception, noted
below. A file nobody lists is a file nothing compiles, and the CI stays
green while covering none of it. That has happened: `utility/`'s
interrupt vector header was included by no translation unit in the
repository, so a change breaking it passed every job.

| You added | You must also |
|---|---|
| a header in `utility/` or `algorithms/` | include it in [`tests/utility_sanity.cpp`](../tests/utility_sanity.cpp), and instantiate the templates it declares. Nothing else in the repository includes those headers. |
| a public class or function to OpSy | use it in [`tests/sanity.cpp`](../tests/sanity.cpp), so its every call site goes through both front ends on all four cores |
| a `.cpp` to OpSy itself | add it to the source lists in `tests/CMakeLists.txt`, `tests/qemu/CMakeLists.txt` and `tests/codegen/CMakeLists.txt`, each of which names `scheduler.cpp` today |
| a host test file | add it to `OPSY_HOST_TEST_SOURCES` in `tests/host/CMakeLists.txt` |
| a host case in an existing file | nothing — `OPSY_TEST(name)` registers itself |
| an on-target test file | add it to `add_executable(opsy_qemu_tests ...)` in `tests/qemu/CMakeLists.txt` |
| an on-target case in an existing file | nothing — `OPSY_QEMU_TEST(name)` registers itself |
| a GDB scenario | drop the `.gdb` file in `tests/gdb/` — nothing else; the glob that picks it up is `CONFIGURE_DEPENDS`, so a plain `cmake --build` re-configures and finds it. It must work on an unoptimised build, which is the only configuration it will be registered in |
| a codegen check | add the probe function to `tests/codegen/codegen_probe.cpp` and the assertion about its disassembly to `tests/codegen/check_codegen.py` |

Adding a *target* or a *compiler* means editing the matrices in
`ci.yml`, and also `tests/cortex-m-toolchain.cmake` and the
`OPSY_QEMU_MACHINE` mapping in `tests/qemu/CMakeLists.txt`.

### Constraints your new code has to satisfy

These are properties the CI enforces that are not obvious from the
source, and that have each cost someone a red run:

**No allocation.** `opsy_no_heap` runs `nm` over the linked test image
and fails if an allocator is in it. Reaching the global `operator new`
or `delete` — a `std::function`, a container, a stream, or a polymorphic
class whose deleting destructor has no class-level `operator delete` to
call — pulls in `malloc`, `free` and `_sbrk`, silently, since none of
that fails to build. See `tests/qemu/check_no_heap.cmake`.

**No exceptions, and no unwinder.** Everything is built with
`-fno-exceptions`, and the linker scripts discard the exception index
tables. Some standard library code reaches the ARM unwinder even so —
`std::lock` and `std::unique_lock::unlock` both do — and an image
containing a call to one of them fails to link with `undefined reference
to __exidx_start`.

**Freestanding standard library, and not the same one twice.** What
`<mutex>` provides differs between the two toolchains: the GNU one has
`std::lock` but not `std::scoped_lock`, and the LLVM one has neither.
`std::lock_guard` is in both. A check written against a facility only
one of them ships passes locally and breaks half the CI. Prefer
asserting the requirement — a concept, a `static_assert` — over
instantiating a library utility to prove the same thing.

**The strict warning set is `-Werror`.** `-Wall -Wextra -Wpedantic
-Wshadow -Wcast-align -Wcast-qual -Wnull-dereference -Wconversion
-Wsign-conversion -Wdouble-promotion`, plus `-Wlogical-op
-Wduplicated-cond -Wduplicated-branches` on gcc. Deprecations count:
`++` on a `volatile` is an error under C++23.

**On-target cases run below the task that runs them.** The suite runner
sits at `task_priority::high`, and helpers start at
`task_priority::lowest`, so a freshly started helper cannot run until
the runner blocks. A helper that must preempt the runner has to be
raised explicitly — and a helper that never yields must stay *below* the
runner's priority, including any priority inheritance may raise it to,
or the runner never gets another turn and the image hangs.

## Reproducing a CI failure locally

Each job is a `cmake` configure, a build, and for three of them a
`ctest`. Run the same commands:

```bash
# Build job (compile-only, no executable produced)
cmake -S tests -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4 -DOPSY_CXX_STANDARD=23
cmake --build build --parallel

# Host tests
cmake -S tests/host -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=g++-14 -DOPSY_CXX_STANDARD=23
cmake --build build-host --parallel && ctest --test-dir build-host --output-on-failure

# QEMU and GDB tests
cmake -S tests/qemu -B build-qemu -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4
cmake --build build-qemu --parallel && ctest --test-dir build-qemu --output-on-failure

# QEMU tests, optimised (the GDB scenarios take themselves out here)
cmake -S tests/qemu -B build-opt -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4 \
      -DOPSY_OPTIMISATION=-O3 -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-opt --parallel && ctest --test-dir build-opt --output-on-failure

# Codegen checks
cmake -S tests/codegen -B build-codegen -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4
cmake --build build-codegen --parallel && ctest --test-dir build-codegen --output-on-failure
```

Needs `arm-none-eabi-gcc`, `qemu-system-arm`, `ninja`, and — for the GDB
scenarios — `arm-none-eabi-gdb` and a Python interpreter. Without the
last two the scenarios are skipped with a message rather than failing,
and the rest of the QEMU suite still runs; a local run that skips them
silently is a local run that has not checked them.

Two differences from the CI worth knowing when a failure will not
reproduce:

- Optimisation is per job. The Build, Host and QEMU-and-GDB jobs use no
  `-O` at all; the optimised QEMU jobs use `-Os` or `-O3` with LTO; the
  codegen checks are fixed at `-O2` with `NDEBUG` and `-fno-lto`,
  because that is the configuration their assertions are about.
  `-DOPSY_OPTIMISATION=<flags>` works on `tests` and `tests/qemu`; LTO
  is `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` on top.
- The runners are slower and differently timed than a desktop. A case
  that depends on *how long* something takes rather than on ordering
  will eventually disagree between the two; write it against an event
  or a shared deadline instead.

## When to touch the workflow itself

Rarely. Adding tests, cases or scenarios does not require it — the lists
above are all in CMake. Edit `ci.yml` for:

- a new target, compiler or optimisation configuration (the matrices),
- a toolchain version bump (the pinned versions above),
- a new suite that needs its own configure command,
- a new system dependency to `apt`-install.

If you bump a toolchain, expect the strict warning set to find something
new. That is the intent.
