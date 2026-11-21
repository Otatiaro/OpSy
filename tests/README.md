# OpSy tests

Five suites, answering five different questions.

| Suite | Question | Runs code? |
|---|---|---|
| [Cortex-M sanity](#cortex-m-sanity-build) (this directory) | does the whole API still compile, on every target, under a strict warning set? | no |
| [Host tests](#host-tests) (`host/`) | does the portable half of OpSy still *behave*? | yes, natively |
| [QEMU tests](#qemu-tests) (`qemu/`) | does the *scheduler* behave, on a real Cortex-M? | yes, emulated |
| [Codegen checks](#codegen-checks) (`codegen/`) | does the optimiser leave the memory-mapped accesses alone? | no — it reads the disassembly |
| [GDB scenarios](gdb/README.md) (`gdb/`) | does OpSy hold up in the states a running test never reaches? | yes, emulated and driven through a debugger |

The QEMU suite also carries one check that runs no code: `opsy_no_heap`
inspects the symbols of the linked image and fails if an allocator is in
it. OpSy never allocates, but nothing in the source says so, and one
construct reaching the global `operator new` or `delete` is enough for
the linker to pull in `malloc`, `free` and `_sbrk` — silently, since
none of that fails to build. See `qemu/check_no_heap.cmake`.

[`../docs/ci.md`](../docs/ci.md) describes how these suites are wired
into the CI, which axes each one runs on, and what you have to add so a
new file of yours is actually built and run.

The cross build cannot check behaviour: it produces a static library
that is never linked or run. The host suite covers the part of OpSy
that is plain C++ — the containers, the allocator, the numerics. The
QEMU suite covers the rest: task switching, service calls, condition
variable timeouts, all of which only exist once the scheduler owns a
CPU.

What none of those can reach is the state a system only gets into
rarely: a race window a couple of instructions wide, a counter 49.7 days
from wrapping, a list caught halfway through being re-stitched. The
emulator is deterministic, so waiting for such a moment to happen by
itself never works. The GDB scenarios create it instead — they stop the
CPU where the situation begins and build it by hand.

## Cortex-M sanity build

Compile-only build that exercises the entire OpSy public API and the
`utility` / `algorithms` headers on every Cortex-M target supported by
OpSy. No executable is produced, so no startup file or linker script is
needed. The point is to keep the template surface honest under a strict
warning set, not to run code on hardware.

### What gets built

`scheduler.cpp` plus the translation units in this directory are compiled
into one static library per configuration (`opsy_sanity_O0`, `_Os`, `_O2`,
and the two with `-DNDEBUG`), and once more as `opsy_sanity_examples`,
with `examples/` first on the include path: that directory holds the
override files the README shows (`opsy_config.hpp`, `opsy_hooks.hpp`),
which `__has_include` then picks up exactly as in a project. OpSy is built
with three preemption bits and the cycle-counting hooks there, and with
its defaults everywhere else; `readme_examples.cpp` asserts under
`OPSY_SANITY_EXAMPLES` that the overrides were the ones seen.

| File | What it covers |
|---|---|
| `sanity.cpp` | One instance of every public OpSy primitive (`task`, `idle_task`, `mutex`, `condition_variable`, `scheduler`, `cortex_m`, `isr_priority`, `callback`, `sleep_for` / `sleep_until`). The functions are kept alive with `[[gnu::used]]` so `-ffunction-sections` / `--gc-sections` does not eliminate them before the compiler has typechecked the call sites. Also a stand-in for a generated vector table, built at compile time with `scheduler::isrs` and `hooked_isr`: `static_assert`s check the three handlers land in slots 11, 14 and 15, a device interrupt goes through `hooks::decorate_isr`, and a table missing an exception does not satisfy `opsy::vector_table`. No dependency on SvdGenerator. |
| `utility_sanity.cpp` | Instantiates the `utility/*.hpp` and `algorithms/*.hpp` templates with valid configurations to fire the in-class `static_assert` checks, plus extra `static_assert` blocks on the `constexpr` surface (sizes, accessors, `at`, `append`, `prepend`, `sub`, arithmetic operators, …). Runtime-only paths (`std::sqrt`, `std::tan`, eigen-decomposition, `slerp`, `from_axis_angle`, …) are kept alive with `[[gnu::used]]` so they go through the front end too. Every header in those two directories is included here — if you add one, add it to the include list, or nothing in the repository will ever compile it. |
| `scheduler.cpp` | The single OpSy translation unit, pulled in from `..` so the `PendSV` / `SVC` / `SysTick` handlers and the inline assembly are exercised on each `-mcpu`. |

The strict warning set matches what production OpSy projects run with:
`-Wall -Werror -Wpedantic -Wextra -Wshadow -Wcast-align -Wcast-qual
-Wconversion -Wsign-conversion -Wdouble-promotion` (not
`-Wnull-dereference`: `warnings.cmake` says why),
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
`CMakeLists.txt`. `OPSY_COMPILER` selects the toolchain family (`gcc`,
what the CI runs, or `clang`); both expect the corresponding
`arm-none-eabi-gcc` or `clang` (with LLVM Embedded Toolchain for Arm) to
be on `PATH`.

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
| `test_callback.cpp` | That assignment destroys the functor it replaces, for both the functor and the `callback` overloads, and that a held functor is released at scope exit. That a function passed by name (a reference, never null) builds under `-Werror`, that a null function or member pointer gives an empty callback, and that a non-null member pointer is held. |
| `test_ellipsoid_fit.cpp` | That a known ellipsoid is mapped back onto the unit sphere (the direction of the `soft_iron` transform), and that degenerate accumulators — empty, too few samples, coplanar, collinear — are rejected rather than yielding NaN. |
| `test_biquad.cpp` | DC gain per filter type, `reset` reaching its requested output, and that static and automatic storage agree. |

The harness is [`host_test.hpp`](host/host_test.hpp) / `host_test.cpp`:
about a hundred lines, no dependency. Tests register themselves at
static-init time, so adding a case is `OPSY_TEST(name) { ... }` and
adding the file to `host/CMakeLists.txt`. Use `CHECK` / `CHECK_NEAR`,
never `assert`, which aborts the run on the first failure.

`host_test.cpp` also defines `handle_contract_violation`, which a failed
check of an OpSy contract calls (the suite is built under the `observe`
semantic), and `opsy::trap`, whose default definition lives in
`config.hpp`, which a host build including a single `utility` header
never pulls in.

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

Time is instruction-counted (`-icount shift=5`, 32 ns an instruction,
close to the 25 MHz the image declares), not taken from the host clock.
Several cases race a task against a deadline, and with wall-clock time
how much a task got done per tick depended on how busy the machine
running QEMU was: a slow CI runner failed them, a fast workstation never
did. Counted, a run is the same every time, on every machine. The suite
also runs at `shift=9`, sixteen times slower (`opsy_qemu_tests_slow_*`): a
case that passes at one speed and not the other depends on how much work a
task gets through per tick, not on what the scheduler decides, and has to
be written against an event instead.

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

[`bench/bench.cpp`](qemu/bench/bench.cpp) is a fourth image,
`opsy_qemu_bench`: the paths through OpSy that cost, each between two
marker functions, `bench::begin<S>()` and `bench::end<S>()`. A task woken
from an interrupt handler through a semaphore, a condition variable and a
queue; a task-to-task switch; a mutex with and without contention; a sleep
ended by the tick; an `isr_lock` round trip; a release that wakes a lower
task without switching, alone and with eight ready tasks to walk past. Two
instruments read the same
image. Under QEMU, which does not model cycles,
[`bench/profile.py`](qemu/bench/profile.py) runs it with
one instruction per translation block (`-accel tcg,one-insn-per-tb=on -d exec,nochain`), counts the instructions executed between the
markers and attributes them to functions: exact, repeatable, and what the
CI holds against `bench/baseline-<target>.json`. On a board the markers
read the DWT cycle counter and the image prints cycles, which is where
exception entry and return, barriers and the memory system show up;
`-DOPSY_BENCH_BOARD=stm32h563zi` builds it for a NUCLEO-H563ZI, clock
brought to 250 MHz in the image, and `probe-rs run` flashes it and prints
its semihosting output. The image's own `opsy_hooks.hpp`, on its include
path alone, timestamps the points along a wake-up that only OpSy sees.

```sh
cmake -S tests/qemu -B build-bench -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake" \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m33 "-DOPSY_OPTIMISATION=-Os;-DNDEBUG"
cmake --build build-bench --target opsy_qemu_bench
python3 tests/qemu/bench/profile.py --image build-bench/opsy_qemu_bench.elf \
      --machine mps2-an505 --baseline tests/qemu/bench/baseline-m33.json
```

Two notes on the plumbing, both learned the hard way:

- On AArch32, plain `SYS_EXIT` takes the reason code *in r1*, not a
  pointer — the `{reason, code}` block is `SYS_EXIT_EXTENDED`. Getting
  that wrong makes QEMU exit 1 on a fully passing run.
- QEMU writes semihosting output to *its own stderr*, and collapses
  every non-zero application exit code to a process status of 1. So
  `run_qemu.cmake` looks for a `RESULT:` line across both streams and
  treats its absence as a failure — otherwise an image that hangs or
  faults halfway would read as a pass.

`test_start_check.cpp` is a second image, `opsy_qemu_start_check`: it
must stop, so it cannot share the suite's. It shows `scheduler::start`
refuses a vector table that does not take its handlers. The MPU makes the
table read-only and MemManage steps over the refused store, so a write to
it goes nowhere, as to flash. The table lacks `SVC_Handler`; passing means
reaching HardFault from a trap instruction while still on the main stack,
with the store refused and the entry unchanged. A task running means start
went on, and fails. Every QEMU job runs it, the `NDEBUG` ones included:
the check is not an `assert`.

`test_faults.cpp` is a third image, `opsy_qemu_faults`: every fault
through `opsy::fault_isrs`, decoded by `opsy::fault_status` and stepped
over. The table is built the way a user builds it, `with(scheduler::isrs)`
then `with(fault_isrs<on_fault>)`, and the faults are raised from a task.
An undefined instruction with the configurable faults still disabled, so
it escalates to HardFault; the same once enabled; a divide by zero; a store
the MPU refuses, with its address; a load from an address nothing answers,
a precise BusFault with its address. On `mps2-an505` the last case takes
the process stack pointer below `PSPLIM`: the handler sees `STKOF` and
ends the run from there, as the stack pointer is not to be trusted after
it. The `mpu.hpp` and `thumb.hpp` helpers (a read-only region, the length
of a Thumb instruction) are shared with the start check.

`test_before_start.cpp` is a fourth image, `opsy_qemu_before_start`: what
a program does to its tasks between their `start()` and
`scheduler::start()`. Two tasks are started at the default priority, one
is then raised and the other lowered and restored, and the scheduler
starts: the raised task has to run first and both have to report the
priority they were given. Its own image because it is about the time
before start, which the suite's image is past. It fell with "fault:
unhandled exception" before `update_priority` learnt not to ask for a
switch the scheduler cannot make yet.

`mps2-an505` needs its own linker script for a second reason beyond
TrustZone addressing: its SRAM at `0x30000000` sits behind a Memory
Protection Controller that comes up blocking, so the very first stack
push faults before any instruction of the image runs. Rather than
programming the SSE-200 MPCs, the image puts both code and data in the
Secure ZBT SRAM at `0x10000000`.

## CI

Every push and pull request to `master` runs all four suites on
`ubuntu-latest`, with GCC 16: the build on `{m3, m4, m7, m33}`, the
host tests, and the QEMU and codegen checks on all four targets. See [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) —
the workflow is the canonical spec, and local builds should match its
configure lines.

## Codegen checks

Probes in [`codegen/`](codegen), built at `-O2` and inspected with
`objdump`.

```sh
cmake -S tests/codegen -B build-codegen -G Ninja       -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/cortex-m-toolchain.cmake"       -DOPSY_COMPILER=gcc       -DOPSY_TARGET=m4
cmake --build build-codegen --parallel
ctest --test-dir build-codegen --output-on-failure
```

A whole class of bug here only exists once the optimiser is allowed to
work: a register read through a non-volatile pointer can be cached or
folded with an identical read, and a store to one can be dropped as
dead. The other three suites are blind to it — the cross build and the
QEMU images are compiled without `-O`, and emulation would only show it
if the compiler happened to take the dangerous transformation. So here
the disassembly itself is the assertion.

Four checks: two reads of the same register stay two loads, a spin on a
register keeps its load inside the loop, `enable_systick`'s four stores
all survive, and `SVC_Handler` pushes an even number of registers so MSP
stays 8-byte aligned across its `bl`.

They were validated by putting the bugs back. With `volatile` removed
from `memory_register` and `push {R4, LR}` reverted to `push {LR}`, all
four fail, and the numbers say exactly what went wrong:

```
FAIL probe_two_reads_stay_two: 1 load(s), expected >= 2
FAIL probe_spin_on_register: 0 load(s), 1 branch(es)
FAIL probe_stores_are_not_eliminated: 2 store(s), expected >= 4
FAIL SVC_Handler: pushes 1 register(s)
```

Zero loads inside the spin is the interesting one: that loop would never
have terminated on hardware.

## What is still not covered

Narrow races. `try_critical_section`'s test-then-set window and
`add_task`'s unmasked list mutation are a few instructions wide; QEMU
runs deterministically and will not hit them on its own. Reproducing
them means driving the interleaving by hand — pending SysTick through
`ICSR` at a chosen instruction — which mostly proves the test was
written correctly.

The `ticks_` torn read needs the 32-bit low word to wrap: ~49.7 days of
simulated time at 1 ms, or an API to seed the counter near the boundary.

The missing `"memory"` clobber on `set_basepri` has no probe: unlike the
MMIO accesses, there is no single instruction pattern that proves the
compiler kept an ordering it was free to break. It is covered by review
and by the fact that the clobber is now there.
