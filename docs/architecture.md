# OpSy architecture

How the scheduler actually works: which exception does what, what the
three lists hold, and how a task moves between them.

This is the document to read before changing anything in `scheduler.cpp`,
`scheduler_inl.hpp` or `task.hpp`. The public API is covered by the
[top-level README](../README.md); this one is about the inside.

## The shape of it

OpSy is a **priority-based preemptive scheduler with a single ready
queue**. There is one hardware thread, so exactly one task runs at a
time, and the highest-priority runnable task is always the one running.
Tasks of equal priority are ordered by how long ago they last ran, so
they take turns rather than starving each other.

Everything the scheduler owns is static: there is no allocation, and
`scheduler.cpp` is the only translation unit precisely because it holds
that static state and the two naked ISRs.

## Three exceptions, three jobs

The scheduler runs entirely out of three ARM exceptions, each at a fixed
priority. The numbers below are the defaults, with `preemption_bits = 2`:

| Exception | Priority | Role |
|---|---|---|
| `SVC` | 64 (`0x40`) | **Decides.** Every blocking operation a task requests enters here. |
| `SysTick` | 127 (`0x7F`) | **Counts.** Advances the clock, wakes expired timeouts. |
| `PendSV` | 255 (`0xFF`) | **Switches.** Saves one context, restores the next. |

Lower number means higher priority, as everywhere on Cortex-M.

The ordering is the whole design:

- `SVC` outranks `SysTick`, so a scheduling decision cannot be
  interrupted by the clock halfway through mutating a list.
- `PendSV` sits at the **lowest possible priority**, so the actual
  context switch happens only once every pending interrupt has been
  serviced. A switch never delays an ISR.
- Anything numerically **below 64** outranks the whole scheduler. Those
  levels are deliberately left free for latency-critical handlers — and
  such a handler must not call into OpSy at all, since it can preempt
  the scheduler at an arbitrary instruction. `scheduler::is_os_callable()`
  asserts this on the entry points that do not go through `SVC`; the
  ones that do are protected by the hardware, since an `SVC` issued from
  a handler that outranks `service_call_priority` escalates to
  HardFault.

### Why a task never switches itself

A task asking to sleep does not touch the ready list. It issues an `SVC`,
which raises the execution priority to 64; the handler mutates the lists
with `SysTick` locked out, marks the switch needed, and returns. `PendSV`
— pending since the handler asked for it — then runs at priority 255,
after everything else, and does the register shuffling.

Decision and mechanism are separated on purpose: the decision needs
exclusion, the mechanism needs to be last.

## Three lists, three sets of links

`task_control_block` inherits **three separate node types**
(`task_lists::handle`, `task_lists::timeout`, `task_lists::waiting`), so
one task can sit in three lists at once without them interfering:

| List | Node used | Holds |
|---|---|---|
| `all_tasks_` | `handle` | Every started task. Enumeration only — `scheduler::all_tasks()`. |
| `timeouts_` | `timeout` | Tasks with a deadline, ordered by wake-up time. |
| `ready_` | `waiting` | Runnable tasks that are not currently running, ordered by priority. |

> **The `waiting` node is shared.** `condition_variable::waiting_list_`
> uses the *same* node pair as `ready_`. A task is therefore in one or
> the other, never both — and mixing them corrupts both lists. This is
> not a detail: several fixed bugs came from a task being linked into
> `ready_` while something still believed it was in a condition
> variable's list. When touching either, check which list owns the task
> first.

A task that sleeps with a timeout is in `all_tasks_` **and** `timeouts_`.
A task waiting on a condition variable with a timeout is in `all_tasks_`,
`timeouts_`, and the condition variable's list. A running task is in
`all_tasks_` only — it is *not* in `ready_`, which holds the ones waiting
their turn.

## Who is running: three pointers

- `current_task_` — the task that owns the CPU. `nullptr` while the
  scheduler is between tasks, or when idle.
- `next_task_` — chosen by `do_switch()`, consumed by `PendSV`.
- `previous_task_` — the one whose context `PendSV` must save.

`do_switch()` is the one function that picks a task. It pops the head of
`ready_`, puts the outgoing task back in, and pends `PendSV`. It refuses
to do anything while a critical section is held, recording
`may_need_switch_` instead so the switch is replayed on release.

## Life of a task

```
              start()                      priority() raises it
                 │                          above the running one
                 ▼                                   │
        ┌──────────────┐   do_switch picks it   ┌─────▼──────┐
        │   ready_     │───────────────────────►│  running   │
        │ (runnable,   │◄───────────────────────│ current_   │
        │  by priority)│   preempted, or its    │   task_    │
        └──────┬───────┘   time slice ends      └─────┬──────┘
               │                                      │
               │  notify / timeout expiry             │ sleep_for
               │                                      │ cv.wait
        ┌──────┴───────────────┐                      │
        │  timeouts_ and/or    │◄─────────────────────┘
        │  cv.waiting_list_    │
        └──────────────────────┘
                 │
                 │ kill(), or the entry callback returns
                 ▼
            terminated  ──► removed from all_tasks_, timeouts_,
                            the cv list, and ready_
```

Termination is the path that has to remove the task from **all four**
places. Forgetting `ready_` leaves the scheduler holding a task it has
already reported as terminated.

## The service calls

`SVC` carries one of four numbers (`scheduler::service_call_number`):

| Call | What the handler does |
|---|---|
| `sleep` | Sets `wait_until_`, inserts into `timeouts_`, clears `current_task_`, switches. |
| `wait` | Same, plus links the task into the condition variable's list and releases the mutex atomically. A negative duration means "no timeout". |
| `context_switch` | Just re-runs `do_switch()` — used after a priority change. |
| `terminate` | Unlinks from every list, clears `current_task_` if it was running, fires the `task_terminated` hook. |

The duration for `sleep` and `wait` is passed as a 64-bit count split
across `r1`/`r2`. **A negative count is the sentinel for "no timeout"** —
which is why `wait_for` and `sleep_for` reject negative durations before
issuing the call rather than passing them through.

## Exclusion: two different mechanisms

They are easy to confuse, and they do not do the same thing.

**`BASEPRI`** masks exceptions at or numerically above its value. Since
`PendSV` sits at 255, *any* non-zero `BASEPRI` masks it — so raising
`BASEPRI` at all stops context switching outright. That is how a
`priority_mutex` with an `isr_priority` excludes both tasks and
interrupts up to its level.

**The scheduler's critical section** (`critical_section_`) does not mask
anything. It makes `do_switch()` decline to mutate the lists, recording
that a switch is due. It matters when `SysTick` is *not* masked — with a
mutex at `0x80`, `SysTick` at 127 still runs and would otherwise reorder
`ready_` under the lock holder's feet.

So: `BASEPRI` stops the switch, the critical section freezes the
scheduler's own state. A `priority_mutex` in task context takes both.

> `opsy::mutex` is an alias for `priority_mutex`, and it is **not**
> `std::mutex`: no owner, no blocking, exclusion is global rather than
> per object, and releases must be strictly LIFO. See the warnings on
> `priority_mutex::lock`.

## The clock

`ticks_` is a 64-bit count incremented once per `SysTick`. Two
consequences worth knowing:

- Reading it from task context is a **two-instruction load** on
  Cortex-M, so `scheduler::now()` masks `SysTick` around the read to
  keep the halves consistent. `std::atomic<uint64_t>` is not an option —
  it is not lock-free on any supported target.
- The pending bit is a flag, not a counter. Masking above
  `systick_priority` for longer than a tick period **loses** ticks
  rather than delaying them, so the clock under-counts real elapsed
  time. Fine for scheduling; not a measurement instrument.

## Context switching in detail

`PendSV_Handler` is naked assembly. It:

1. locks to `service_call_priority` via `BASEPRI`, so the decision
   cannot change under it;
2. saves `R4-R11` of the outgoing task (and `S16-S31` if it used the
   FPU) onto its `PSP`;
3. writes the new `PSP` and restores the incoming task's registers
   (`PSPLIM` is set on ARMv8-M by `pend_sv_handler`, the C++ half it
   calls into);
4. returns through `EXC_RETURN`, which pops the rest.

`R0-R3`, `R12`, `LR`, `PC` and `xPSR` are stacked by the hardware on
exception entry, which is why only the callee-saved half appears here.

A task's initial stack is built by `start_impl` to look exactly like a
frame the hardware would have pushed, with `PC` at the task entry
trampoline and `LR` at the terminate path — so a task that returns from
its entry callback terminates cleanly, without any special case.

Both handlers keep `MSP` 8-byte aligned across their calls, as AAPCS
requires at a public interface: user hooks run from inside them.

## Where to look next

- [`tests/README.md`](../tests/README.md) — the four test suites, and
  explicitly what is *not* covered.
- [`config.hpp`](../config.hpp) — priorities, tick rate, preemption bits,
  and the `opsy_config.hpp` extension point.
- [`hooks.hpp`](../hooks.hpp) — the tracing points, all no-ops by default.
