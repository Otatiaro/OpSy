# GDB scenarios

The other suites can only observe what OpSy does on its own. These drive
the emulator through a debugger: they stop the CPU at a chosen
instruction, write state the hardware would take weeks to reach, create
the exact interleaving a race needs, and read structures no running code
can see.

That is what makes them worth having. A race window two instructions
wide is never hit by chance under a deterministic emulator, so a test
that merely runs the code proves nothing about it — it passes just as
readily against the broken version. Each scenario here reproduces the
situation deliberately.

They run against the same image as the [QEMU suite](../qemu), so there
is nothing extra to compile.

## Running them

They are ctest tests, registered alongside the QEMU suite:

```
cmake -S tests/qemu -B build-qemu -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/tests/cortex-m-toolchain.cmake \
      -DOPSY_COMPILER=gcc -DOPSY_TARGET=m4
cmake --build build-qemu --parallel
ctest --test-dir build-qemu --output-on-failure
```

Needs `arm-none-eabi-gdb` and a Python interpreter on `PATH`, on top of
the `qemu-system-arm` the QEMU suite already needs. Without them the
scenarios are skipped with a message, and the QEMU suite still runs:
they are an addition to it, not a prerequisite.

To run one on its own:

```
ctest --test-dir build-qemu -R tick_wrap --output-on-failure
```

## The scenarios

| Scenario | What it establishes |
|---|---|
| `tick_wrap.gdb` | `scheduler::ticks_` stays monotonic across the wrap of its low 32 bits — about 49.7 days of uptime at 1 ms a tick, which is why no suite that just runs will ever reach it. Arms the counter two ticks short of the boundary and watches it cross. |
| `take_mutex_masking.gdb` | `scheduler::take_mutex` is never entered with `SysTick` unmasked. Both a task and the tick handler can reach it, and if the two overlapped the mutex would end up with two owners. Reads `BASEPRI` inside the function and forces a tick to confirm it is held off. |
| `add_task_atomicity.gdb` | Starting a task links it into two lists, and `SysTick` must not run while they disagree — it would walk a half-stitched list. Single-steps through `add_task` and checks at every instruction that the two lists agree, or `SysTick` is masked. |
| `critical_section_race.gdb` | Two tasks never both hold the scheduler's critical section. Stops the CPU at the instant the flag is read and writes it as a competing task would, which is the only interleaving that tells an atomic claim apart from a read followed by a write. |
| `pendsv_window.gdb` | The task elected for a switch is accounted for at every point: no task is claimed to be running when `PendSV` is entered, and the handler installs the elected task and closes the election window. A task lost in that window would simply never run again, silently. |
| `list_integrity.gdb` | The scheduler's task lists stay coherent tick after tick: counts match the chains, back-links agree with forward links, no chain is a cycle, and the running task is not also queued as waiting. Intrusive lists do not fail loudly when mis-stitched, so this looks for the damage directly. |

## How a scenario reports

A scenario prints its own verdict, `SCENARIO: PASS` or `SCENARIO: FAIL`,
and `run_scenario.py` looks for it. A session that dies, hangs or is cut
short prints no verdict, and that is treated as a failure — the same
rule the QEMU suite applies to its `RESULT:` line, and for the same
reason: silence must never read as success.

The same principle applies inside the scenarios. Several check that they
actually observed the situation they exist to observe — that `add_task`'s
window was seen open at least once, that not every switch went to the
idle task, that the list link fields were identified — and fail if they
did not. A scenario that quietly checked nothing is worse than one that
fails, because it looks like coverage.

## What has been shown to catch a real defect

A scenario that passes proves nothing until it has been shown to fail on
the defect it describes. Each of these was checked by reintroducing that
defect in the source, rebuilding and confirming the scenario turns red:

| Scenario | Defect reintroduced | Caught |
|---|---|---|
| `add_task_atomicity.gdb` | `add_task` no longer raises `BASEPRI` around its two insertions | yes |
| `critical_section_race.gdb` | the atomic claim replaced by a load followed by a store | yes |
| `take_mutex_masking.gdb` | `mutex::lock` no longer raises `BASEPRI` | yes |
| `list_integrity.gdb` | `pop_front` leaves a back-link pointing at a removed element | yes |
| `pendsv_window.gdb` | `PendSV` does not clear `next_task_` | yes |
| `pendsv_window.gdb` | `PendSV` installs a task other than the elected one | yes |

Two things are deliberately not in that table.

`pendsv_window.gdb` also checks that no task is claimed to be running
when `PendSV` is entered. No local edit was found that violates it while
still producing a switch to observe — the ordering that guarantees it is
structural. It is kept as a guard against a future reworking of the
switch, not as a regression test for a defect anyone has written.

`tick_wrap.gdb` has no such entry either. Its value is not that it
detects an edit; it is that it reaches a point in time no test run will
otherwise reach, and shows the counter behaves there.

## Writing another one

A scenario is a plain GDB batch script. `run_scenario.py` starts QEMU
with the CPU frozen at reset (`-S`) and its GDB stub listening, runs the
script against it, and turns the output into an exit status.

Conventions the existing ones follow:

- open with a comment saying what the scenario establishes, why running
  the code cannot establish it, and what a reader needs to know to
  follow the script — the register, the hardware rule, the memory layout
  it relies on. Assume no other context.
- print `INFO` for what was set up or observed, `CHECK` for each thing
  decided, with the expected value alongside. Those are the lines the
  harness keeps; everything else is GDB's own noise.
- end on exactly one `SCENARIO: PASS` or `SCENARIO: FAIL`, then `kill`.
- fail when the scenario did not get to check anything, rather than
  passing by default.
- run `break opsy_test_main` and `continue` first, unless the scenario
  is specifically about startup: past that point the scheduler is
  running and the image is doing the work the scenario wants to watch.

Two traps worth knowing about, both of which cost time here:

- **GDB convenience variables share a namespace with the CPU
  registers.** `set $r2 = 0` writes register r2 and corrupts the
  program; so do `$sp`, `$lr`, `$pc`, `$s0`. Name variables after what
  they hold and the collision does not arise.
- **The scheduler's static members carry no type information** in the
  image, so they are read through explicit casts —
  `*(unsigned long *)&'opsy::scheduler::current_task_'`. Anything read
  by walking pointers out of them should be range-checked before being
  followed: GDB stops the whole scenario with an error when it is asked
  to read an address that is not memory.
