# OpSy utility headers

A small set of self-contained C++23 headers shipped alongside OpSy. Most
have no dependency on the scheduler — feel free to use them on a hosted
platform too. The only exception is `interrupt_vector.hpp`, which is
ARM Cortex-M specific by design (it builds the architectural vector
table).

All identifiers live in `namespace opsy::utility`. The headers are
header-only, allocate nothing on the heap, throw no exceptions, and
exercise their `static_assert` / `constexpr` checks in CI alongside the
core scheduler.

## Files

| File | Purpose |
|---|---|
| `allocator.hpp` | `allocator<N, UseDummy, Dummy>` — fixed-size, in-band-tagged allocator. Slots are `int` (negative = allocated, positive = free); free space is coalesced on `deallocate`. Optional sentinel fill helps catch use-after-free. |
| `biquad.hpp` | `biquad<T, Coef>` — second-order IIR filter (direct form II transposed). `filter_type::{low_pass, high_pass, notch, band_pass}`, Butterworth Q default, value and coefficient types are independent template parameters. |
| `interrupt_vector.hpp` | `interrupt_vector<PeripheralIrqs>` — compile-time builder for the ARM Cortex-M vector table. System exception slots are passed by name to the constructor; peripheral ISRs are added by chaining `.with_handler<IRQ, fn>()`. The whole expression is constant-initialisable so the table lives in `.isr_vector` in flash. **Cortex-M only.** |
| `matrix.hpp` | `matrix<Rows, Cols, T>` — fixed-size full matrix targeted at small dimensions (rotations, small Kalman blocks, ellipsoid fits up to 9×9, …). Row-major storage, element access via `m(i, j)` (Eigen-style), compile-time `m.at<I, J>()`, plus `m.row(i)` / `m.col(j)` returning the matching `vector<>`. Basic algebra: `==`, `+`, `-`, unary `-`, scalar `*` / `/` and compound forms, `transposed()`, `operator*(matrix, matrix)`, `operator*(matrix, vector)`. Row primitives `swap_rows`, `scale_row`, `add_scaled_row` for Gauss-style algorithms, plus `sub_matrix<R, C>(top, left)` and in-place `apply(f)`. `determinant()` and `inverse()` for square matrices: closed-form (1×1…4×4) or Gauss-Jordan with partial pivoting (≥ 5×5). For real symmetric square matrices, `eigenvalues()` and `symmetric_eigen_decomposition()` (Householder + QL with implicit Wilkinson shifts; eigenvalues sorted ascending). Free function `identity_matrix<N, T>()`. Multiplications and inverse are `[[gnu::always_inline]]` so they stay inlined down to `-Og`. |
| `memory.hpp` | Building blocks for typed, atomic-aware memory-mapped register layouts: `memory<TInt, TReg>` / `read_only_memory<TInt, TReg>` / `write_only_memory<TInt, TReg>` for register storage, `clear_set<TReg>` for read-modify-write masks, the `atomic` tag and `\| atomic` / `\|\| atomic` postfix syntax that promote a write to a `fetch_or` / `compare_exchange`, plus `padding<Size>` to align successive registers in a struct to the offsets the hardware expects. Used by generated peripheral header packs (RCC, GPIO, USART, ...). |
| `quaternion.hpp` | `quaternion<T>` — strong-typed unit quaternion built as a thin composition wrapper around `vector<4, T>` (xyz = i/j/k, w = scalar). Distinct from a plain `vector<4>` so `q1 * q2` (Hamilton product) and `q * v3` (rotation) are unambiguous and a `vector<4>` holding RGBA / homogeneous coordinates / etc. is not misinterpreted. Constructors from raw components, from a rotation vector (axis × angle, the form gyroscope integrators produce — zero-length input maps to identity), and shortest-arc from two unit vectors (with antiparallel handling). Includes `identity_quaternion`, `hamilton_product`, `conjugate`, `inverse`, `from_axis_angle`, `rotate`, `slerp`, Tait-Bryan ZYX extraction `roll()` / `pitch()` / `yaw()` (pitch clamped at the gimbal-lock singularity), plus the usual element-wise `+`, `-`, scalar `*`/`/`, `norm`/`length`/`normalize` (used internally by `slerp`). The wrapper costs zero at any optimisation level: `[[gnu::always_inline]]` on the hot ops keeps them inlined down to `-Og` — verified byte-for-byte against a hand-written direct-on-`vector<4>` version. |
| `routine.hpp` | `routine` / `routine_storage<Size>` / `suspended<Value>` — an interrupt's state machine written as straight-line code, using C++ coroutines. `co_await` marks where the next hardware event is needed; the loop stays a loop and the error paths stay `return`s. The frame goes in a `routine_storage` you provide, sized by the compiler and checked, never on a heap. See [the section below](#routinehpp-in-more-detail). |
| `slope.hpp` | `slope<N, T, Coef>` — FIR numerical derivative based on a least-squares linear fit over `N` samples. Coefficients are computed at compile time, so `value()` is a fixed-size dot product. Mean delay is `N/2` samples. |
| `vector.hpp` | `vector<N, T>` — compile-time-sized math vector with the usual operators (`+`, `-`, `*`, `/`), `norm()` / `length()` / `normalized()`, typed `x()` / `y()` / `z()` / `w()` accessors constrained by size, plus `append` / `prepend` / `sub`, `dot_product`, `cross_product`, `rotate`. |

## Use

If the OpSy repository root is on your include path (as suggested in the
top-level README), reach the headers via `<utility/...>`:

```cpp
#include <utility/biquad.hpp>
#include <utility/vector.hpp>

opsy::utility::biquad<float> filter{
    48000.0f, 1000.0f, opsy::utility::filter_type::low_pass};

constexpr opsy::utility::vector<3> v{1.0f, 2.0f, 3.0f};
static_assert(v.norm() == 14.0f);
```

## Conventions

Same as OpSy core: `snake_case` identifiers, trailing `_` on members,
short STL-style template parameter names (`T`, `N`, `Coef`, `I`, `Is`).

## CI coverage

`tests/utility_sanity.cpp` instantiates every template under the same
strict warning set as the scheduler (`-Wshadow`, `-Wcast-align`,
`-Wconversion`, `-Wsign-conversion`, `-Wdouble-promotion`, `-Werror`,
…) on every Cortex-M target in the matrix. Add a header here and add it
to that include list, or nothing in the repository will compile it.

That build never runs, so behaviour is covered separately by the host
suite in [`tests/host`](../tests/host) — `allocator`, `biquad` and the
`embedded_list` used by the scheduler each have cases there.

## `routine.hpp` in more detail

The one header here that needs more than a table row.

### The problem

A peripheral driver written in an interrupt handler is a state machine
whether you want one or not. The handler is entered once per hardware
event, must decide where in the protocol it is, act, and remember where
it got to. So you write an enum of states, a struct of everything that
has to survive between events, and a `switch`.

The protocol's structure disappears in the process. A loop over the bytes
to send becomes an index in the struct plus a comparison in two of the
cases. An error path that should be a `return` becomes an assignment to
the state variable, repeated at every point that can fail. Adding a step
means finding every case that transitions into the one after it.

### What a routine is

The same state machine, written as the sequence it is:

```cpp
#include <utility/routine.hpp>

opsy::utility::routine write(opsy::utility::routine_storage<128>&,
                    uint8_t address, const uint8_t* data, std::size_t length)
{
    I2C1->CR1 |= I2C_CR1_START;
    if (co_await opsy::utility::suspended<uint32_t>{ &I2C1->SR1 } & I2C_SR1_ARLO)
        co_return;                                   // lost arbitration

    I2C1->DR = address;
    if (co_await opsy::utility::suspended<uint32_t>{ &I2C1->SR1 } & I2C_SR1_AF)
    {
        I2C1->CR1 |= I2C_CR1_STOP;
        co_return;                                   // no such device
    }

    for (std::size_t i = 0; i < length; ++i)
    {
        I2C1->DR = data[i];

        const uint32_t status = co_await opsy::utility::suspended<uint32_t>{ &I2C1->SR1 };
        if (status & (I2C_SR1_AF | I2C_SR1_ARLO))
        {
            I2C1->CR1 |= I2C_CR1_STOP;
            co_return;
        }
    }

    co_await std::suspend_always{};  // byte transfer finished
    I2C1->CR1 |= I2C_CR1_STOP;
}
```

Each `co_await` means *this is where I need the next interrupt*. The
loop is a loop, the error paths are returns, and the state that had to be
kept by hand — which step, which byte, which failure — is the routine's
own local variables.

The handler, in full:

```cpp
opsy::utility::routine g_transfer;

extern "C" void I2C1_EV_IRQHandler()
{
    g_transfer.resume();
}
```

And starting one:

```cpp
opsy::utility::routine_storage<128> g_storage;

void begin(uint8_t address, const uint8_t* data, std::size_t length)
{
    g_transfer = opsy::utility::routine{};   // release the previous one first
    g_transfer = write(g_storage, address, data, length);
    g_transfer.resume();                     // runs to the first co_await
}
```

That first line matters, and leaving it out is the easiest mistake to
make here. Writing `g_transfer = write(g_storage, ...)` on its own builds
the new frame in `g_storage` **before** the assignment releases the old
one — the right operand is evaluated first — so the new routine is built
over a frame that is still live, whose destructors then never run, and
the assignment tears down the new routine believing it is tearing down
the old. A retry of a failed transfer is exactly this call.

`routine_storage` refuses it rather than letting it happen: it knows
whether it holds a live routine, and a debug build asserts. Releasing
first, as above, is all it takes — and `used` goes back to zero when a
routine is released, so a storage can be reused as often as you like.

Note that `Size` covers both the frame and a few bytes of the storage's
own bookkeeping (`routine_storage::reserved`, one alignment's worth).
`used` reports the frame alone, so budget a little above it.

### What it costs

The compiler turns the routine into the state machine you would have
written, and stores what has to survive a suspension in a **frame** whose
size it works out at compile time. That frame goes in the
`routine_storage` you pass in — never on a heap. Measured on the transfer
above, Cortex-M4, `-Os`:

| | flash | state |
|---|---|---|
| written by hand as a `switch` | 152 B | 16 B |
| as a routine | 328 B | 48 B |

Around 180 bytes of flash and 32 of RAM, against a protocol you can read.

### Sizing the storage

The compiler works out the frame size, but there is no standard way to
ask it: no constant to put in a `static_assert`, and the size is not
available where you declare the storage. So it is checked twice, and
neither check lets it overflow.

**In an optimised build, too small is a compile error.** The size is a
constant by the time the allocation is inlined, so the mismatch is caught
there and reported by calling a function that cannot be called:

```
error: call to 'opsy::utility::the_routine_frame_does_not_fit_its_storage'
declared with attribute error: this routine's frame does not fit the
routine_storage it was given: raise its Size
```

Both GCC and clang, from `-O1` up. Without `-O` the allocation is a real
call and the size is not a constant there, so the check cannot be made:
the build succeeds, the routine is not created, `operator bool` is false
and a debug build asserts. Same mistake, caught one step later.

**To find the size rather than bracket it**, give the storage more than
it can need, run once, and read `used` — it holds exactly what the frame
took. Then set `Size` to that. The compile error is what stops you
setting it too low afterwards, so trimming is safe.

### Two things that surprise people

**A routine takes one more resumption than it has steps.** After the last
`co_await` it is suspended on that `co_await`, not finished: it needs one
further `resume()` to run off the end of its body and report `done()`. A
driver that stops resuming at what it thinks is the last event leaves the
routine suspended for good.

**Clang, without optimisation, needs `__aeabi_unwind_cpp_pr0` to exist.**
It emits an `.ARM.exidx` entry for every coroutine even under
`-fno-exceptions`, and each entry names the ARM personality routine. A
linker script that discards those tables — the right thing in a build
without exceptions — still leaves the relocation naming the symbol, and
the link fails on it. Providing the real one drags in the unwinder, which
wants `stderr`. Define an empty one, or one that traps: in a build
without exceptions it is never called. GCC emits no such reference, and
neither compiler does at `-O1` or above. The test image defines one in
`tests/qemu/startup.cpp`.

### What you cannot do in one

A routine runs in interrupt context. It must not call into OpSy: no
`mutex`, no `sleep_for`, no `condition_variable`. Those suspend a task,
and an interrupt has no task to suspend. `assert`s in the scheduler catch
it, but the rule is simpler than the diagnosis: a routine talks to
hardware, nothing else.

It also cannot suspend from inside an ordinary function it calls. `co_await`
only works in the routine's own body — that is what makes the frame small
and fixed. A helper *can* suspend if it is itself a routine that the
caller awaits, at the cost of its own frame.

If you need to suspend from arbitrary depth — from a vendor HAL callback,
say, or from a function shared with non-routine code — this is the wrong
tool, and what you want is a stack per routine rather than a frame.
