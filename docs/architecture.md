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

**Sized for a few dozen tasks.** Several structures are derived by
walking a list rather than stored as one, which keeps the state minimal
and impossible to desynchronise, at the price of walks that are linear
in the number of tasks. That is the right trade for a Cortex-M
application with a handful of tasks; it is the wrong one for hundreds.
See [what that costs](#what-that-costs-and-the-system-size-it-implies).

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

A task blocked on a `mutex` is in `all_tasks_` and nothing else — it
carries `blocked_on_` instead of being linked anywhere.

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
        │  cv.waiting_list_    │   sleep_for / cv.wait
        │                      │
        │  or blocked_on_ a    │◄──── mutex.lock() on a
        │  mutex (in no list)  │      mutex someone holds
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
| `wait` | Same, plus links the task into the condition variable's list and releases the lock the caller recorded — dropping `BASEPRI` for an `isr_lock`, handing ownership to a waiter for a `mutex`. A negative duration means "no timeout". |
| `context_switch` | Just re-runs `do_switch()` — used after a priority change. |
| `mutex_lock` | Links the caller as a waiter on a held `mutex`, applies priority inheritance, and switches away. Returns once the caller owns it. |
| `mutex_unlock` | Releases the mutex, hands it straight to the highest-priority waiter, and recomputes the releaser's priority. |
| `terminate` | Unlinks from every list, releases every mutex the task held, clears `current_task_` if it was running, fires the `task_terminated` hook. |

The duration for `sleep` and `wait` is passed as a 64-bit count split
across `r1`/`r2`. **A negative count is the sentinel for "no timeout"** —
which is why `wait_for` and `sleep_for` reject negative durations before
issuing the call rather than passing them through.

## Exclusion: three mechanisms, one per problem

Which one to reach for follows from *who* you are excluding.

**`opsy::mutex`** — between tasks. A real mutex: it has an owner, and a
task that cannot take it is suspended and resumes owning it. Releases in
any order, several held at once, `try_lock`, priority inheritance. Use
it for state shared between tasks.

**`opsy::isr_lock`** — with an interrupt handler. Masking, because an
ISR cannot be suspended: `lock()` raises `BASEPRI` to the lock's
priority, so every interrupt at or numerically above it is held off.
Since `PendSV` sits at 255, *any* non-zero `BASEPRI` masks it too, and
task switching stops for the duration. No owner, no blocking, no
waiting: exclusion comes from nothing else running.

**The scheduler's critical section** (`critical_section_`) — internal.
It masks nothing. It makes `do_switch()` decline to mutate the lists,
recording that a switch is due. It matters when `SysTick` is *not*
masked: with an `isr_lock` at `0x80`, `SysTick` at 127 still runs and
would otherwise reorder `ready_` under the holder's feet.

So `BASEPRI` stops the switch, the critical section freezes the
scheduler's own state, and `mutex` is the only one of the three that
actually makes a task wait.

## How the mutex stores what it knows

Every relation is stored exactly once. Nothing is duplicated, so nothing
can disagree:

| Question | Answer |
|---|---|
| Who owns `m`? | `m.owner_` |
| What is `t` blocked on? | `t.blocked_on_` |
| Who is waiting on `m`? | **derived** — the tasks in `all_tasks_` whose `blocked_on_` is `&m` |
| What does `t` hold? | **derived** — the mutexes in `locked_mutexes_` owned by `t` |

The two derived rows are the ones a textbook implementation would store
as lists: a wait queue in each mutex, and a held-mutex list in each
task. Both were deliberately not kept, because a bidirectional graph
maintained by hand is two structures that can drift apart — which is the
shape of most of the list-corruption bugs this scheduler has had.

A consequence worth noting: a task blocked on a mutex is in **no list at
all**. Its `task_lists::waiting` node stays free, which is why `ready_`
and `condition_variable::waiting_list_` remain that node's only two
users.

### What that costs, and the system size it implies

Deriving instead of storing means walking a list where a direct
implementation would follow a pointer:

| Operation | Cost | When |
|---|---|---|
| `lock()`, uncontended | O(1) | the common path |
| `try_lock()` | O(1) | — |
| priority inheritance | O(1), O(depth) if transitive | on contention |
| `unlock()` with waiters | O(tasks) — elect the highest-priority waiter | every contended release |
| recompute priority after `unlock()` | O(held mutexes x tasks) | every release by a task that held several |
| `kill()` | O(locked mutexes) | rare |

**This is a deliberate trade, and it sets a ceiling on system size.**
OpSy is built for a few dozen tasks at most — the usual shape of a
Cortex-M application, where a handful of tasks each own a peripheral or
an activity. At that scale the walks are a few dozen iterations of a
pointer chase, far below the cost of the context switch that follows,
and the simplicity is worth more than the cycles.

It does **not** scale to hundreds of tasks. `unlock()` under contention
is linear in the total number of tasks, not in the number of waiters, and
the priority recomputation is a product of two counts. With 500 tasks and
50 mutexes, a contended release would walk tens of thousands of entries
inside a critical section — where a wait queue per mutex would have
walked one. If you need that many tasks, this is the design decision to
revisit first, and the fix is mechanical: give each mutex its own wait
list and each task its own held list, at the cost of keeping them in step.

### Waking with a lock held

A task that waited holding a lock has to get it back before it may run,
and the two kinds are re-acquired at different moments:

- an **`isr_lock`** is a `BASEPRI` level, which belongs to the context —
  it is restored inside `PendSV`, as part of the switch;
- a **`mutex`** is ownership, which is not per-context — it is taken back
  when the task is woken. And taking it back can fail, if someone else
  holds it by then, in which case the wake turns straight into blocking
  on the mutex.

That second case is why `resume_waiter()` decides about the mutex
*before* inserting into `ready_`, never after. A task woken to a mutex it
cannot have goes into no list at all, carrying `blocked_on_`. Inserting
first and correcting afterwards is what once put a task in two lists at
the same time.

Which lock to release is recorded on the task before the service call, as
a `std::variant<std::monostate, mutex*, isr_lock*>` — 8 bytes on
Cortex-M, and no `bad_variant_access` path under `-fno-exceptions`. There
was no register left in the service call frame to carry a discriminant,
and a bare pointer could not say which kind it pointed at.

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
