# OpSy

OpSy is a small, header-mostly real-time operating system (RTOS) for ARMv7-M
and ARMv8-M Cortex-M microcontrollers. It is written in modern C++ (C++23)
and built around the standard library threading primitives (`std::mutex`,
`std::condition_variable`, `std::lock_guard`, `std::chrono`) so the API feels
familiar to anyone who has written multi-threaded C++ on a hosted platform.

OpSy does no heap allocation, throws no exceptions, and has no dependency
beyond a C++23-capable `arm-none-eabi-g++`.

## Status

Alpha. The API is stable enough to use in side projects but has not yet
soaked through extensive production deployment in this iteration. Earlier
iterations of the same scheduler design have flown on hundreds of thousands
of hours in commercial flight controllers.

## Supported targets

- Cortex-M3, Cortex-M4, Cortex-M7 (ARMv7-M)
- Cortex-M33 (ARMv8-M, with TrustZone disabled / non-secure)

The scheduler asserts at startup that the running core is one of the above.

## Requirements

- C++23 compiler. Tested with the ARM GNU toolchain shipped in recent
  STM32CubeIDE versions (currently `arm-none-eabi-g++ 14.3 rel1`).
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
| `opsy.hpp` | Umbrella header — pulls in everything below and exposes `sleep_for` / `sleep_until`. Include this from `main.cpp`. |
| `scheduler.hpp` | The `scheduler` class itself. Most users only call `scheduler::start()`. |
| `scheduler.cpp` | **Add this to your build** — it holds every static of the scheduler and provides the `SVC` / `PendSV` / `SysTick` handlers. |
| `scheduler_inl.hpp` | Inline definitions for the header-only primitives, included from the bottom of `scheduler.hpp`. Never include this directly. |
| `task.hpp` | `task<StackSize>`, `task_control_block`, `idle_task<StackSize>`, `task_priority`. |
| `priority_mutex.hpp` | `priority_mutex` (the default `mutex` alias). |
| `condition_variable.hpp` | `condition_variable`, plus a `cv_status` enum redefined in `namespace std` because `<condition_variable>` is not available on bare-metal Cortex-M. |
| `critical_section.hpp` | RAII handle for task-only exclusion. |
| `cortex_m.hpp` | Thin wrappers around the Cortex-M system registers used by the scheduler (`BASEPRI`, `PRIMASK`, `MSP`/`PSP`, NVIC, `VTOR`, ...). Useful from your own code too. |
| `isr_priority.hpp` | `isr_priority` value type, with split between preemption and sub-priority bits. |
| `callback.hpp` | A small `std::function` replacement with no heap and no exceptions. Used by `task::start`. |
| `embedded_list.hpp` | Intrusive doubly-linked list used internally for the ready / waiting / timeout queues. |
| `hooks.hpp` | Default (empty) hook callbacks. Override by providing `<opsy_hooks.hpp>` somewhere on the include path. |
| `config.hpp` | Default configuration. Override by providing `<opsy_config.hpp>` somewhere on the include path. |

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

Minimal `main.cpp`:

```cpp
#include <opsy.hpp>

int main()
{
    // Configure clocks, peripherals, ...
    opsy::scheduler::start(); // [[noreturn]]
}
```

`scheduler::start()` never returns — it switches the CPU to the lowest
priority task, or to the idle task (a `WFI` loop by default) if no task
has been registered. The system is now ready.

## Blinky

```cpp
#include <opsy.hpp>

using namespace std::chrono_literals;

opsy::task<512> blinker; // 512 stack slots = 2 KiB on Cortex-M

int main()
{
    // ... clock and GPIO init ...

    blinker.start([] {
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

### `opsy::mutex` (alias for `priority_mutex`)

```cpp
#include <mutex>
#include <opsy.hpp>

opsy::mutex m{opsy::isr_priority{0x80}};

void from_task()
{
    std::lock_guard guard{m};
    // critical section, masks both task switch
    // and any ISR with priority numerically >= 0x80
}
```

A `priority_mutex` constructed without an `isr_priority` only synchronizes
between tasks (a critical section). With an `isr_priority`, locking the
mutex sets `BASEPRI` to that level, masking every interrupt at or below
that priority for the duration of the lock — that is how you protect
shared state between a task and an ISR.

A `priority_mutex(isr_priority{0})` is the strongest form: a full lock
that disables all maskable interrupts (`PRIMASK = 1`).

### `opsy::condition_variable`

```cpp
opsy::mutex              m{opsy::isr_priority{0x80}};
opsy::condition_variable cv{opsy::isr_priority{0x80}};

void wait_for_data()
{
    std::lock_guard guard{m};
    cv.wait(m); // atomically releases m, sleeps,
                // re-acquires m on wake
}

void from_isr()
{
    cv.notify_one(); // safe to call from ISR at the cv's priority
}
```

`condition_variable::wait` exists in four flavours: with or without a
mutex, and with or without a timeout (`wait_for(duration)` /
`wait_until(time_point)`). The timed variants return a `std::cv_status`,
matching `<condition_variable>`. Unlike the standard, OpSy does **not**
emit spurious wakeups, so the predicate-checking overloads are
intentionally absent.

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

## Configuration

OpSy ships with sensible defaults in `config.hpp` and `hooks.hpp`. To
override either, add an `<opsy_config.hpp>` or `<opsy_hooks.hpp>` file
to any directory on your include path; OpSy detects them with
`__has_include` and uses them in place of the defaults.

Typical reasons to provide an `<opsy_config.hpp>`:

- Change the time base (the default `duration` is `std::chrono::milliseconds`).
- Change the preemption-bit split or the OpSy preemption level.
- Replace the default `mutex` alias with a custom mutex (e.g. a multi-core
  semaphore on a multiprocessor system).
- Provide a different `core_clock()` than reading the linker-provided
  `SystemCoreClock` symbol.

Typical reasons to provide an `<opsy_hooks.hpp>`:

- Wire `Hooks::task_started` / `task_stopped` / `enter_pend_sv` / etc.
  to a tracing tool such as SEGGER SystemView.
- Add custom asserts or logging on context switches.

## ISR priorities and OpSy

The scheduler runs at one specific NVIC preemption level
(`opsy_preemption`, default 1). Anything at a numerically *lower* preempt
value (so higher hardware priority) than OpSy **must not** call any OpSy
API: such an ISR can preempt the SVC handler and break atomicity. ISRs
at a numerically *higher* preempt value (lower priority) can use OpSy's
synchronization primitives normally, as long as the `priority_mutex` or
`condition_variable` they touch was constructed with an `isr_priority`
that masks them.

## License

MIT. See [`LICENSE`](LICENSE).

## History

Third iteration of the design. The first version flew on the Neuron
Flybarless flight controller (hundreds of thousands of flights logged
without a scheduler-level incident). The second iteration was rewritten
for the EzManta project, which needed more concurrent activities and a
richer synchronization model. The current iteration drops C++14 baggage
in favour of C++23 features (`constexpr` everywhere, `std::optional`,
`std::chrono`, `[[nodiscard]]`, ...) and a cleaner header layout — the
core primitives (`task`, `priority_mutex`, `condition_variable`,
`critical_section`) are header-only; only the scheduler itself needs a
translation unit, because of its static state and the inline assembly in
`PendSV_Handler` / `SVC_Handler`.
