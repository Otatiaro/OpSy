# OpSy

OpSy is a small, header-mostly real-time operating system (RTOS) for ARMv7-M
and ARMv8-M Cortex-M microcontrollers. It is written in modern C++ (C++26)
and built around the standard library threading primitives (`std::mutex`,
`std::condition_variable`, `std::lock_guard`, `std::chrono`) so the API feels
familiar to anyone who has written multi-threaded C++ on a hosted platform.

OpSy does no heap allocation, throws no exceptions, and has no dependency
beyond a C++26-capable `arm-none-eabi-g++`.

Beyond the scheduler, it ships a set of standalone [`utility/`](utility)
primitives (fixed-size vector / matrix / quaternion, biquad filters, a
heap-free allocator) and
[`algorithms/`](algorithms) built on them — none of which depend on the
scheduler.

## Status

Alpha. The API is stable enough to use in side projects but has not yet
soaked through extensive production deployment in this iteration. Earlier
iterations of the same scheduler design have flown on hundreds of thousands
of hours in commercial flight controllers.

## Supported targets

- Cortex-M3 (ARMv7-M, no FPU)
- Cortex-M4, Cortex-M7 (ARMv7-M, FPU)
- Cortex-M33 (ARMv8-M Mainline, FPU, without the Security Extension)

The `PendSV` context-switch path saves and restores the floating-point
callee-saved registers (`S16-S31`) only when the toolchain reports a
hardware FPU (the predefined `__ARM_FP` macro, set by GCC for `-mfpu=…
-mfloat-abi=hard`). Builds without an FPU — Cortex-M3 in the official
build matrix, but also any soft-float build of M4/M7 — pay zero clock
cycle for the absent feature: the FP save/restore block is preprocessed
out, and the matching `if constexpr` branch in the service-call handler
disappears at compile time. `cortex_m::enable_fpu()` is a compile-time
error in those builds — the user project is responsible for calling it
only on FPU-equipped targets. The scheduler asserts at startup that the
running core is one of the above.

"Without the Security Extension" is the assumption on ARMv8-M, and it is
not the same as "Non-secure": a Cortex-M33 built without TrustZone, or
one whose vendor configuration leaves it off. The `EXC_RETURN` OpSy writes
into a fresh task's context is `0xFFFFFFFD`, and its fault handler reads
bit 2 of the one the core hands it; neither looks at the `S` and `ES` bits
TrustZone adds, which decide the security state a return goes to. Seen
from a core with the extension, `0xFFFFFFFD` is the Secure-state value: a
Non-secure OpSy would build another one, and a Secure OpSy would have to
own the SAU, the interrupt targeting and the banked registers. None of
that is here.

## Requirements

- C++26 compiler. CI builds every target with **GCC 16.2.0**, release
  `v16.2.0-1` of
  [Otatiaro/arm-none-eabi-toolchain](https://github.com/Otatiaro/arm-none-eabi-toolchain)
  (GCC, binutils, newlib; Linux and Windows x86_64), which is what OpSy is
  written for. GCC 14.3 (GNU Tools for STM32, the STM32CubeIDE default) built
  it until November 2026 and is not tested any more; clang (LLVM Embedded
  Toolchain for Arm) comes back to the CI when it has contracts.

  Older toolchains are not tested and are unlikely to work — the headers use
  C++26 throughout. If you need to build with the GCC 14 or 15 that CubeIDE
  ships, expect to have to check it yourself.

  GCC 16's libstdc++ checks its own preconditions in unoptimised builds
  (`_GLIBCXX_ASSERTIONS`, on by default at `-O0`). A failed check calls a
  handler in the library that is built with exceptions, so a debug image gets
  the C++ unwinder and `fprintf` linked in with it, and a linker script that
  discards `.ARM.exidx` fails on `__exidx_start`. Define
  `_GLIBCXX_NO_ASSERTIONS` to turn the checks off, as the QEMU images do.
- A linker script that places `.text`, `.bss`, `.data` etc. as usual on
  Cortex-M, plus `_estack`, `_sstack`, `_sidata`, `_sdata`, `_edata`,
  `_sbss`, `_ebss` symbols if you reuse the example startup code.
- An NVIC priority configuration that leaves at least 2 preemption bits
  (the OpSy default — overridable, see *Configuration* below).

## Repository layout

OpSy is intended to be added to a project as a git submodule. All public
headers live next to each other in this directory; the only translation
unit is `scheduler.cpp`, which holds the static state and the two ISRs
(`PendSV` and `SVC`) that the scheduler installs.

| File | Purpose |
|---|---|
| `opsy.hpp` | Umbrella header — pulls in everything below and exposes `sleep_for` / `sleep_until`. Include this from `application.cpp`. |
| `scheduler.hpp` | The `scheduler` class itself. Most users only call `scheduler::start()`, and `scheduler::isrs` to place OpSy's handlers in a vector table built at compile time (see *The vector table*). |
| `scheduler.cpp` | **Add this to your build** — it holds every static of the scheduler and provides the `SVC` / `PendSV` / `SysTick` handlers. |
| `task.hpp` | `task<StackSize>`, `task_control_block`, `idle_task<StackSize>`, `task_priority`. `stack_high_water()` reports the most stack a task, or the idle task, ever used: what to size its stack from. |
| `mutex.hpp` | `mutex` — blocking, owning mutual exclusion between tasks, with priority inheritance. |
| `isr_lock.hpp` | `isr_lock` — masking exclusion between a task and an interrupt handler. |
| `condition_variable.hpp` | `condition_variable`, plus an `opsy::cv_status` enum mirroring `std::cv_status` (the standard one lives in `<condition_variable>`, which is not available on bare-metal Cortex-M). |
| `semaphore.hpp` | `counting_semaphore<Max>` and `binary_semaphore`, with the interface of their `std` counterparts: a task blocks on `acquire`, a task or an interrupt handler `release`s. Lock-free while nobody has to wait. |
| `queue.hpp` | `queue<T, Capacity>`, with the interface of the proposed `std::bounded_queue` (P0260): `push`/`pop` wait, `try_push`/`try_pop` report a `conqueue_status`, `close()` ends it. One producer and one consumer, either of which may be an interrupt handler; nothing masked, nothing allocated. A full queue refuses and counts it (`dropped()`), and remembers how full it got (`high_water()`). |
| `critical_section.hpp` | RAII handle for task-only exclusion. |
| `cortex_m.hpp` | Thin wrappers around the Cortex-M system registers used by the scheduler (`BASEPRI`, `PRIMASK`, `MSP`/`PSP`, NVIC, `VTOR`, ...). Useful from your own code too. |
| `isr_priority.hpp` | `isr_priority` value type, with split between preemption and sub-priority bits. |
| `callback.hpp` | `callback<Signature, StorageSize>`, with the interface of `std::move_only_function` -- `const`, `&`, `&&` and `noexcept` in the signature say how the target is called -- holding its target inline: no heap, no exceptions. Built from a function or a stateless target (a lambda without capture, `std::cw<&f>`), it is a constant expression: `constinit` dispatch slots, calls inside a constant evaluation. Used by `task::start`. |
| `embedded_list.hpp` | Intrusive doubly-linked list used internally for the ready / waiting / timeout queues. |
| `hooks.hpp` | `hook`, the list of the points OpSy reports; each one calls the function of the same name in `opsy::hooks` when an `<opsy_hooks.hpp>` on the include path defines it, and is empty otherwise. Also `hooked_isr`, the decoration that passes a generated table's interrupt handlers through `hook::decorate_isr`. |
| `config.hpp` | Default configuration. Override by providing `<opsy_config.hpp>` somewhere on the include path. |

Two directories sit alongside the scheduler and are independent of it —
they allocate nothing, throw nothing, and have no dependency on the
scheduler, so they can be used on their own (including on a host, for
offline validation):

| Directory | Contents |
|---|---|
| [`utility/`](utility) | Fixed-size numeric and container primitives: [`vector`](utility/README.md) / `matrix` (with symmetric eigen-decomposition) / `quaternion`, `biquad` filters, `slope`, a heap-free `allocator`. |
| [`algorithms/`](algorithms) | Higher-level numerics built on `utility/`. Currently `ellipsoid_fit`, an online least-squares ellipsoid fit for magnetometer hard/soft iron calibration. |

Each has its own README with the per-file detail.

## How it works

[`docs/architecture.md`](docs/architecture.md) covers the inside: which
exception does what (`SVC` decides, `SysTick` counts, `PendSV` switches,
and why they sit at those priorities), what the three lists hold, how a
task moves between them, and the two different exclusion mechanisms that
are easy to confuse. Read it before changing anything in `scheduler.cpp`.

[`docs/ci.md`](docs/ci.md) covers what is checked, and — if you are
adding code — where your new file has to be mentioned for anything to
compile and run it. Nothing here discovers files on its own, so a
translation unit nobody lists is one the whole matrix passes without
ever having built.

## Naming convention

Identifiers follow the C++ standard library style: `snake_case` types and
methods, trailing `_` on members, no `k`/`m_`/`s_` prefixes.

## Quick start

Add OpSy to your project as a submodule:

```sh
git submodule add https://github.com/Otatiaro/OpSy.git src/OpSy
```

Then in your build system add `src/OpSy/scheduler.cpp` to your sources and
add `src/OpSy/` to your include path. Example with CMake:

```cmake
target_sources(my_app PRIVATE src/OpSy/scheduler.cpp)
target_include_directories(my_app PRIVATE src/OpSy)
```

Minimal `application.cpp`:

```cpp
#include <opsy.hpp>

[[noreturn]] void application()
{
    // Configure clocks, peripherals, ...
    opsy::scheduler::start(); // [[noreturn]]
}
```

`scheduler::start()` never returns — it switches the CPU to the lowest
priority task, or to the idle task (a `WFI` loop by default) if no task
has been registered. The system is now ready.

`application()` is what the startup code calls once the runtime is up,
in place of `main()`; [Starting up](#starting-up) says why, and what that
startup code does. A project that keeps the C runtime's start files and
`main()` writes the same body there.

## Blinky

```cpp
#include <opsy.hpp>

using namespace std::chrono_literals;

opsy::task<512> blinker; // 512 stack slots = 2 KiB on Cortex-M

[[noreturn]] void application()
{
    // ... clock and GPIO init ...

    // start() is [[nodiscard]] — it returns false if the task was already
    // running. Discard it explicitly, or check it.
    (void) blinker.start([] {
        while (true)
        {
            led_on();
            opsy::sleep_for(250ms);
            led_off();
            opsy::sleep_for(250ms);
        }
    });

    opsy::scheduler::start();
}
```

`opsy::task<N>` is a value type that contains its own `N`-word stack — no
dynamic allocation, no separate buffer to manage. Declare it as a global
or `static` local; the scheduler keeps a pointer to it for the lifetime of
the program.

`task::start()` takes any callable that fits in the default `callback`
storage (3 pointers worth — enough for any non-capturing lambda and most
small captures). It is `[[nodiscard]]`: it returns `false` if the task was
already started.

The lambda runs forever in this example (`while (true)`); if it ever
returns, the scheduler terminates the task cleanly.

Add a second `task<>` and you have two truly independent execution
contexts — preempted by the scheduler so each behaves as if it had its
own CPU.

## Synchronization primitives

OpSy mirrors the standard library API, so the synchronization patterns
you are used to from `<thread>` / `<mutex>` translate directly.

### `opsy::mutex` — between tasks

A real mutex, with the semantics of `std::mutex`: it has an owner, and a
task that cannot take it is **suspended** until the holder releases,
then resumes owning it.

```cpp
#include <mutex>
#include <opsy.hpp>

opsy::mutex m;

void from_task()
{
    std::lock_guard guard{m};
    // exclusive against other tasks; interrupts keep running
}
```

Locks may be released in any order, a task may hold several at once, and
`try_lock()` is available. A holder is raised to the priority of the
most important task waiting behind it, so a middle-priority task cannot
starve it — the classic priority inversion.

Not recursive, like `std::mutex`. Locking one you already hold, unlocking
one you do not own, and destroying a held one each trip an assert in
debug and cost nothing in release.

`lock()` blocks, so it must be called from a task — never from an
interrupt handler, which has nothing to suspend. For that, use:

### `opsy::isr_lock` — between a task and an ISR

An interrupt cannot be suspended, so exclusion against one can only be
masking. `lock()` raises `BASEPRI` to the lock's priority for the
duration.

```cpp
opsy::isr_lock l{opsy::isr_priority{0x80}};

void from_task()
{
    std::lock_guard guard{l};
    // masks every ISR at priority numerically >= 0x80,
    // and task switching along with it
}

void from_isr()
{
    std::lock_guard guard{l};   // same lock, from the handler side
}
```

`isr_lock` has no owner and never blocks: exclusion comes from nothing
else being allowed to run. Constructed without an `isr_priority` it only
holds off other tasks; with `isr_priority{0}` it is a full lock that
disables all maskable interrupts (`PRIMASK = 1`).

> Because it is a mask rather than a lock, releases across several
> `isr_lock`s must be strictly LIFO, and it must not be held across a
> sleep. `opsy::mutex` has neither restriction. See
> [`docs/architecture.md`](docs/architecture.md) for why.

### `opsy::condition_variable` — an interrupt handler waking a task

The usual real-time shape: a handler fills a buffer or notes what
happened, and a task is woken to deal with it. The state the task waits
for lives under an `isr_lock`; the handler changes it under the same lock
and notifies; the task tests it and waits in one step.

```cpp
opsy::isr_lock           lock{opsy::scheduler::os_callable_priority};
opsy::condition_variable events_changed{opsy::scheduler::os_callable_priority};
uint32_t                 events = 0;      // guarded by lock

constexpr uint32_t rx_done = 1u << 0;
constexpr uint32_t error   = 1u << 1;

void uart_isr()                           // strictly below OpSy's preemption level
{
    {
        std::lock_guard guard{lock};
        events |= rx_done;
    }
    events_changed.notify_one();
}

void uart_task()
{
    while (true)
    {
        uint32_t got;
        {
            std::lock_guard guard{lock};
            while ((events & (rx_done | error)) == 0)
                events_changed.wait(lock); // releases lock and sleeps in one step,
                                           // takes it back on wake
            got = std::exchange(events, 0);
        }
        // handle got, outside the lock
    }
}
```

The word of bits is what event flags are: several causes, set by the
handler, taken all at once by the task. Two things make it correct, and
both are easy to get wrong:

- **The state is tested under the lock, in a loop, before waiting.** A
  notification that comes before the task waits is not stored by the
  condition variable: it is the flag that remembers it. OpSy emits no
  spurious wakeups, but the loop is still what catches an event that came
  first.
- **The lock and the condition variable are at
  `scheduler::os_callable_priority`.** The lock has to mask the handler,
  or it could change the state between the test and the wait; and it must
  not mask OpSy's own level, where the wait's service call is taken. A
  handler using them has to run at that priority or below.

`condition_variable::wait` exists with an `isr_lock`, with an
`opsy::mutex` (between tasks), or with no lock at all, and each with a
timeout (`wait_for(duration)` / `wait_until(time_point)`). The timed
variants return an `opsy::cv_status`, mirroring `std::cv_status` from
`<condition_variable>`.

### `opsy::critical_section`

A scoped handle for task-only exclusion. Obtained via
`scheduler::try_critical_section()`. While held, the scheduler will not
preempt the current task. Useful when you need to protect against other
tasks but specifically *not* against ISRs (for example, when the data is
only ever produced by an ISR and consumed by a task).

### Sleeping

```cpp
opsy::sleep_for(500ms);
opsy::sleep_until(opsy::scheduler::now() + 1s);
```

Both must be called from a task, never from an ISR, and you must release
every `mutex` before calling them.

## The vector table

OpSy runs on three exceptions: `SVC`, `PendSV` and `SysTick`. Their
handlers are `SVC_Handler`, `PendSV_Handler` and `SysTick_Handler`, defined
in `scheduler.cpp`; the vector table has to hold them.

With a table built at compile time -- the `vectors.hpp`
[SvdGenerator](https://github.com/Otatiaro/SvdGenerator) generates for a
device, from v1.0.0 -- `opsy::scheduler::isrs` places the three, and `opsy::hooked_isr`
passes every device interrupt handler through `hook::decorate_isr`, so a
tracer wired there sees them all:

```cpp
#include <opsy.hpp>
#include "vectors.hpp" // generated by SvdGenerator for the device

using namespace STM32H563;

[[gnu::section(".isr_vector"), gnu::used]]
constexpr auto g_vectors = vector_table<Default_Handler, opsy::hooked_isr>(&_estack, Reset_Handler)
    .with(opsy::scheduler::isrs)
    .with<USART3_IRQn, uart_isr>();
```

`scheduler::isrs` takes any table that satisfies the `opsy::vector_table`
concept: an `exception_type` naming `svcall`, `pendsv` and `systick`, and
a `with<exception, handler>()` that returns the table with that slot
filled. OpSy does not depend on SvdGenerator.

A vendor startup file works too: its weak `SVC_Handler`, `PendSV_Handler`
and `SysTick_Handler` give way to OpSy's.

Either way, `scheduler::start()` writes the three handlers through `VTOR`
where they differ, then reads them back. If one is not there -- a table in
flash that holds another handler, a `SysTick_Handler` the vendor startup
does not route to OpSy's -- it stops on `opsy::trap()` rather than start a
scheduler whose first tick would land elsewhere. The check is not an
`assert`: it holds in a release build too.

### Migrating from `utility/interrupt_vector.hpp` and `utility/memory.hpp`

Both headers are gone; SvdGenerator generates what they did, for the
device at hand.

- `utility/interrupt_vector.hpp`: use the generated `vectors.hpp`, as
  above. `.with(opsy::scheduler::isrs)` replaces installing OpSy's three
  handlers by hand, and `opsy::hooked_isr` replaces the decoration through
  `hook::decorate_isr`.
- `utility/memory.hpp` (`opsy::utility::memory`, `clear_set`, `atomic`,
  `padding`): use SvdGenerator's `Support/mmio.hpp`, namespace `mmio`,
  which the generated peripheral headers are written on.

## Starting up

OpSy has no opinion on how the image comes up, only on what holds when
`scheduler::start()` runs: `.data` copied, `.bss` zeroed, the static
constructors run (a `task<N>` at file scope has one), `MSP` at the top of
the main stack and, on ARMv8-M, `MSPLIM` at its bottom. The C runtime's
start files do all of this and call `main()`. The examples here assume
the other way, a startup the project owns:

- the image links with `-nostartfiles`, so nothing refers to `main()` and
  the program has none;
- `Reset_Handler`, the one in the vector table, sets `MSP` and `MSPLIM`,
  enables the FPU, copies `.data`, zeroes `.bss`, then runs
  `.preinit_array` and `.init_array` in that order: the two arrays
  `__libc_init_array()` walks, without newlib's start code to walk them;
- it then calls `application()`, declared `[[noreturn]] void application();`
  and defined once per image. That is where the Quick start's body goes.

Between a task's `start()` and `scheduler::start()` the task exists and
is queued, and nothing runs yet. Its priority and its name can be set
there, and `scheduler::start()` picks the ready list as those calls left
it; what needs a running scheduler -- `kill()`, `sleep_for`, taking a lock
that may block -- waits for a task to call it from. `scheduler::is_started()`
says which side of the line a program is on, and `kill()` asserts on it in
a debug build: the service call it issues has no handler of OpSy's to land
in before start.

The reason is `main()` itself. C++ forbids calling it from the program and
giving it C linkage, so a C++ `Reset_Handler` cannot call it: the function
the startup calls has to be another one, and once it is, the start files
have nothing left to do. `[[noreturn]]` states what `scheduler::start()`
guarantees, and lets the compiler check that the image never falls out of
its entry point.

A complete one, with the no-heap traps and the `assert` replacement that
go with it, is Bornier's
[`startup.cpp`](https://github.com/bornier-dev/bornier/blob/2d561e5/firmware/boards/base/startup.cpp)
and [`application.hpp`](https://github.com/bornier-dev/bornier/blob/2d561e5/firmware/boards/base/application.hpp),
for an STM32H523. `cortex_m::set_msp()`, `set_msplim()` and `enable_fpu()`
are the pieces OpSy provides.

## Faults

Out of reset every fault is a HardFault, and a vendor table routes it to a
handler that loops. OpSy puts the four fault exceptions behind one function
of yours, with the cause decoded:

```cpp
[[noreturn]] void reset();

void on_fault(const opsy::fault_status& status, opsy::exception_frame& frame, uint32_t exc_return)
{
	if (status.stack_overflow())
		reset(); // the stack pointer is below its limit: nothing more can be pushed on it

	if (status.escalated())
	{
		// one of the three configurable faults, taken as HardFault
	}

	if (const auto address = status.memory_fault_address())
	{
		// the MPU refused an access to *address
	}

	(void) frame.pc;       // the faulting instruction, for a precise fault
	(void) exc_return;     // bit 2 set: the frame is on the process stack, a task faulted
	reset();
}

[[gnu::section(".isr_vector"), gnu::used]]
constexpr auto g_vectors = vector_table<Default_Handler, opsy::hooked_isr>(&_estack, Reset_Handler)
    .with(opsy::scheduler::isrs)
    .with(opsy::fault_isrs<on_fault>);

void application()
{
	opsy::cortex_m::enable_configurable_faults();
	opsy::cortex_m::enable_divide_by_zero_trap();
	opsy::cortex_m::set_priority(opsy::cortex_m::system_irq::usage_fault, opsy::isr_priority{0});
	opsy::scheduler::start();
}
```

`opsy::fault_status` is a copy of `CFSR`, `HFSR`, `MMFAR` and `BFAR`, taken
on entry and left in the registers; its predicates name the bits the
architecture defines (`undefined_instruction()`, `divide_by_zero()`,
`precise_bus_fault()`, `stack_overflow()`, ...). `opsy::exception_frame` is
the frame the core stacked, writable. Returning from the handler returns
from the exception, to `frame.pc`: a handler that steps over the faulting
instruction moves `pc` and calls `cortex_m::clear_fault_status()` first,
or the next fault reads as this one on top of its own.

`enable_configurable_faults()` gives MemManage, BusFault and UsageFault
their own exceptions; until then they reach the handler as HardFault, with
`escalated()` set. Each then has a priority of its own, set through
`cortex_m::set_priority(system_irq::usage_fault, ...)`; a fault in OpSy's
own handlers reaches yours only if it outranks them. The handler must not
call OpSy: it may be running on the main stack, in the middle of a context
switch.

On ARMv8-M, a task that runs out of stack is a `stack_overflow()` with
`exc_return` bit 2 set; the stack pointer is then below its limit, and the
handler cannot resume the task. What else watches a task's stack, and
what does on ARMv7-M, is in
[architecture.md](docs/architecture.md#three-nets-under-a-tasks-stack).

## Configuration

OpSy ships with sensible defaults in `config.hpp` and `hooks.hpp`. To
override either, add an `<opsy_config.hpp>` or `<opsy_hooks.hpp>` file
to any directory on your include path; OpSy detects them with
`__has_include` and uses them in place of the defaults.

`<opsy_config.hpp>` replaces the whole default block of `config.hpp`, and
`config.hpp` includes it inside `namespace opsy`: the file defines every
name the defaults define (`core_clock`, `priority_bits`, `preemption_bits`,
`opsy_preemption`, `duration`, `max_inheritance_depth`, `trap`), with no
namespace of its own. [One is shown below](#choosing-preemption_bits).

Typical reasons to provide an `<opsy_config.hpp>`:

- Change the time base (the default `duration` is `std::chrono::milliseconds`).
- Change the preemption-bit split or the OpSy preemption level.
- Replace the default `mutex` alias with a custom mutex (e.g. a multi-core
  semaphore on a multiprocessor system).
- Provide a different `core_clock()` than reading the linker-provided
  `SystemCoreClock` symbol.

Typical reasons to provide an `<opsy_hooks.hpp>`:

- Wire `task_started` / `task_stopped` / `enter_pend_sv` / etc. to a
  tracing tool such as SEGGER SystemView.
- Add custom asserts or logging on context switches.
- Measure where the cycles go, as below.

`hooks.hpp` includes `<opsy_hooks.hpp>` outside any namespace, and the file
defines `opsy::hooks` with the hooks it listens to -- the others are not
called, and cost nothing. [`opsy::hook`](hooks.hpp) is the list of what
it may define, each with its signature: a task made ready and why
(`task_ready(task, reason)`, with a `wake_reason` of `notified`,
`timed_out`, `sleep_elapsed` or `mutex_granted`), a task put to sleep and
until when, a mutex taken, handed over or blocked on, the entry and exit
of each of OpSy's handlers, the idle task's wait (`idle_wait`, a `WFI` by
default, which a tracer reading memory through the debug port replaces
with a `NOP`), and `decorate_isr`, which the vector table applies to every
device handler through `opsy::hooked_isr`. Where a hook notes a shorter
form, `task_ready(task)` for one, that form is accepted too. A name that
matches nothing in the list is a function OpSy never calls: a hook that
stays silent is checked against the list first.

### Measuring OpSy's handlers

What OpSy adds to the latency of a handler below its level is the time
its own handlers run; what a handler adds to everything below it is its
own. The hooks see both ends of each. This `opsy::hooks` keeps the
longest run of the service call, of SysTick, of PendSV's C++ half (up to
the choice of the next task; the register restore after it is assembly,
and not seen) and of any decorated interrupt handler, from the DWT cycle
counter, which `starting` turns on. It is
[`tests/examples/opsy_hooks.hpp`](tests/examples/opsy_hooks.hpp), which
the sanity build compiles OpSy against:

```cpp
/** @brief The longest run of each handler since the scheduler started, in core cycles */
struct latency
{
    static inline std::atomic<uint32_t> service_call = 0; ///< SVC_Handler, as a whole
    static inline std::atomic<uint32_t> systick      = 0; ///< SysTick_Handler, as a whole
    static inline std::atomic<uint32_t> pend_sv      = 0; ///< PendSV_Handler, up to the choice of the next task
    static inline std::atomic<uint32_t> isr          = 0; ///< the longest decorated interrupt handler

    /** @brief Raises longest to the cycles elapsed since entered when they are more: one atomic
     *         step, as decorated handlers of different priorities all record into isr */
    static void record(std::atomic<uint32_t>& longest, uint32_t entered)
    {
        longest.fetch_max(cortex_m::cycle_count() - entered, std::memory_order_relaxed);
    }
};

class hooks
{
public:
    /** @brief Starts the cycle counter: after a reset it does not run unless a debugger started it */
    static void starting(idle_task_control_block&, uint32_t, callback<void(callback<void(const task_control_block&)>)>)
    {
        (void) cortex_m::enable_cycle_count();
    }

    static void enter_service_call() { service_call_entered_ = cortex_m::cycle_count(); }
    static void exit_service_call(bool) { latency::record(latency::service_call, service_call_entered_); }
    static void enter_systick() { systick_entered_ = cortex_m::cycle_count(); }
    static void exit_systick(bool) { latency::record(latency::systick, systick_entered_); }
    static void enter_pend_sv() { pend_sv_entered_ = cortex_m::cycle_count(); }
    static void task_started(task_control_block&) { latency::record(latency::pend_sv, pend_sv_entered_); }
    static void enter_idle() { latency::record(latency::pend_sv, pend_sv_entered_); }

    /** @brief The handler the vector table holds in place of Routine: the same, timed */
    template<void (*Routine)()>
    static constexpr auto decorate_isr()
    {
        return +[]
        {
            const uint32_t entered = cortex_m::cycle_count();
            Routine();
            latency::record(latency::isr, entered);
        };
    }

private:
    static inline uint32_t service_call_entered_ = 0;
    static inline uint32_t systick_entered_      = 0;
    static inline uint32_t pend_sv_entered_      = 0;
};
```

Each of OpSy's handlers keeps its own entry time, as SysTick and the
service call preempt PendSV; a decorated handler keeps its on its own
stack, as another may preempt it. Read `opsy::latency` from the debugger
or from a task, and divide by `core_clock()` for seconds. It does not see
the time between an interrupt pending and its handler's first instruction,
which the core spends alone, nor what a `critical_section` or an
`isr_lock` holds back: those are a task's choice, and the task knows how
long it held them.

## ISR priorities and OpSy

An NVIC priority is a byte of which the core implements the top
`priority_bits` (4 on most parts; the vendor's `__NVIC_PRIO_BITS` when it
is defined). `scheduler::start` splits them, through `PRIGROUP`, into
`preemption_bits` of *preemption level*, which decide who interrupts whom,
and the rest as *sub-priority*, which only orders handlers pending at the
same time. The scheduler runs at one preemption level, `opsy_preemption`.
With the defaults, two bits and level 1:

| Level | Priorities | Who runs there |
|---|---|---|
| 0 | `0x00`-`0x3F` | Above OpSy: handlers that never call it. The scheduler never masks them. |
| 1 | `0x40`-`0x7F` | OpSy: `SVC` at `0x40`, `SysTick` at `0x7F`. Nothing else. |
| 2 | `0x80`-`0xBF` | `scheduler::os_callable_priority`: handlers that call OpSy, from here down. |
| 3 | `0xC0`-`0xFF` | Handlers that call OpSy; `PendSV` at `0xFF`, the very last. |

Anything at a numerically *lower* preempt value (so higher hardware
priority) than OpSy **must not** call any OpSy API: such an ISR can
preempt the SVC handler and break atomicity. Nor can an ISR at OpSy's own
preemption level: it preempts none of OpSy's exceptions, so a service call
raised from it escalates to HardFault. ISRs at a numerically *higher*
preempt value (lower priority) can use OpSy's synchronization primitives
normally, as long as the `isr_lock` or `condition_variable` they touch was
constructed with an `isr_priority` that masks them -- and no more urgent
than `scheduler::os_callable_priority`, the first level below OpSy's.

Level 0 is what OpSy costs nothing: a handler there is never delayed by a
service call, a tick or a context switch, which is the place for a bus
whose timing the hardware dictates or a control loop. It cannot wake a
task itself; it hands its data to a handler at a callable level, pended
from software as [below](#raising-an-interrupt-from-software), and that
one does.

### Choosing `preemption_bits`

The defaults leave two levels for handlers that call OpSy. When an
application has more tiers than that among them -- a protocol handler
that must not wait for a logger, say, and both wanting OpSy -- raise
`preemption_bits` in an `<opsy_config.hpp>`. Three bits give level 0 above
OpSy, OpSy at 1, and six levels for handlers that call it; the priorities
shift with the split, so refer to them as `scheduler::os_callable_priority`
and `isr_priority::from_preempt_sub<opsy::preemption_bits>(level, sub)`
rather than as numbers. `preemption_bits` goes up to `priority_bits`
(then there is no sub-priority, and `SVC` and `SysTick` share one
priority, which changes nothing: they differ only in sub-priority, which
never let one preempt the other), and must leave at least one level below
OpSy's; `config.hpp` asserts both.

This is [`tests/examples/opsy_config.hpp`](tests/examples/opsy_config.hpp),
which the sanity build compiles OpSy against:

```cpp
extern "C"
{
	extern uint32_t SystemCoreClock;
}

/** @brief The system core clock in Hz, which SysTick is programmed from. */
inline uint32_t core_clock()
{
	return SystemCoreClock;
}

/** @brief Priority bits the core implements: the vendor's __NVIC_PRIO_BITS. */
constexpr uint32_t priority_bits = 4;

/** @brief Of which this many decide who preempts whom; the rest is sub-priority. */
constexpr uint32_t preemption_bits = 3;

/** @brief The preemption level OpSy runs at. Handlers call OpSy from the level below, and only from there down. */
constexpr uint32_t opsy_preemption = 1;

/** @brief The time base of timeouts and sleep. */
using duration = std::chrono::duration<int64_t, std::milli>;

/** @brief How far priority inheritance follows a chain of blocked tasks. */
constexpr std::size_t max_inheritance_depth = 8;

/** @brief Where OpSy stops on an internal error it cannot recover from. */
[[noreturn]] inline void trap()
{
	__builtin_trap();
}
```

### Raising an interrupt from software

`cortex_m::set_pending(irq)` makes a handler run, and the usual reason is
to hand it data written just before. Pending is a volatile store to the
NVIC, and the compiler is allowed to move an ordinary store after a
volatile one: the handler can then run on what was there before. Order the
two with the standard's tool for a handler on the same core:

```cpp
std::atomic_signal_fence(std::memory_order_seq_cst);
opsy::cortex_m::set_pending(irq);
```

Without it this is not a theoretical concern: at `-O2` GCC 14 moves the
store past the pend, and the handler sees the previous value. In a real
program a call to a function the compiler cannot see often happens to sit
between the two and hide it; inlining and LTO take that away.

The same holds, and more often, for data handed to the hardware: filling
a buffer, then writing the register that starts a DMA or a peripheral. The
writes to the buffer are ordinary, the start is volatile, and the compiler
may put some of the former after the latter. There the hardware needs a
`dsb` as well (`cortex_m::data_barrier()`), and on a core with a data cache
the buffer cleaned to memory.

`set_pending` and the other register accessors in `cortex_m` do not order
anything for you: each does what it says and no more.

## License

MIT. See [`LICENSE`](LICENSE).

## History

Third iteration of the design. The first version flew on the Neuron
Flybarless flight controller (hundreds of thousands of flights logged
without a scheduler-level incident). The second iteration was rewritten
for the EzManta project, which needed more concurrent activities and a
richer synchronization model. The current iteration drops C++14 baggage
in favour of modern C++ (`constexpr` everywhere, `std::optional`,
`std::chrono`, `[[nodiscard]]`, ...) and a cleaner header layout — the
core primitives (`task`, `mutex`, `isr_lock`, `condition_variable`,
`critical_section`) are header-only; only the scheduler itself needs a
translation unit, because of its static state and the inline assembly in
`PendSV_Handler` / `SVC_Handler`.
