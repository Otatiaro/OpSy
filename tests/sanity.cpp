/**
 ******************************************************************************
 * @file    sanity.cpp
 * @brief   Compile-only smoke test of the OpSy public API.
 *
 *          This translation unit references every public symbol exposed by
 *          @c <opsy.hpp> so that building it as a static library together
 *          with @c scheduler.cpp verifies the API still compiles and the
 *          templates can be instantiated. No code is meant to run; the
 *          functions are kept alive with @c [[gnu::used]] only so the
 *          linker does not eliminate them in @c -ffunction-sections /
 *          @c --gc-sections builds.
 *
 *          Invoked by the CI matrix for Cortex-M3 / M4 / M7 / M33.
 ******************************************************************************
 */

#include <opsy.hpp>

#include <chrono> // IWYU pragma: keep (the duration literals; https://github.com/llvm/llvm-project/issues/68213)
#include <concepts>
#include <utility>

namespace {

using namespace std::chrono_literals;

// One instance of every primitive — exercises all default constructors
// and the priority-aware overloads. All constinit: an application declares
// them at namespace scope and they must be ready before any constructor runs.
constinit opsy::task<512>          g_task;
constinit opsy::idle_task<256>     g_idle;
constinit opsy::isr_lock           g_isr_lock;
constinit opsy::isr_lock           g_isr_lock_with_priority{ opsy::isr_priority{ 0x80 } };
// Two of them: the standard multi-mutex algorithms below need more than one.
constinit opsy::mutex              g_mutex;
constinit opsy::mutex              g_second_mutex;
constinit opsy::condition_variable g_cv;
constinit opsy::condition_variable g_cv_with_priority{ opsy::isr_priority{ 0x80 } };

[[gnu::used]] void use_task()
{
	(void) g_task.start([]{ while (true) opsy::sleep_for(1ms); });
	(void) g_task.priority();
	g_task.priority(opsy::task_priority::normal);
	(void) g_task.is_started();
	(void) g_task.kill();
}

[[gnu::used]] void use_isr_lock()
{
	g_isr_lock_with_priority.lock();
	(void) g_isr_lock_with_priority.priority();
	g_isr_lock_with_priority.unlock();
}

[[gnu::used]] void use_mutex()
{
	g_mutex.lock();
	(void) g_mutex.is_locked();
	g_mutex.unlock();

	if (g_mutex.try_lock())
		g_mutex.unlock();

	g_second_mutex.lock();
	g_second_mutex.unlock();
}

// A mutex has to satisfy the standard's Lockable requirements, or none of the
// library's lock utilities work with it: std::lock_guard, and the algorithms
// that take several mutexes at once in an order of their own choosing so two
// tasks locking the same pair cannot deadlock by taking them in opposite
// orders. Those algorithms need try_lock() to decline rather than block, so
// they can back out of a partial acquisition and retry.
//
// Stated as concepts rather than by instantiating std::lock against the type.
// The standard library shipped with a bare-metal ARM toolchain is a
// freestanding one, and what it provides varies: the GNU toolchain's has
// std::lock but not std::scoped_lock, and the LLVM Embedded Toolchain for
// Arm's has neither. A check written against those would be testing which
// library happens to be installed. The requirements themselves do not vary.
//
// That std::lock_guard actually drives opsy::mutex is checked where it can
// run, in the on-target suite: std_lock_guard_works_with_it.

template<typename T>
concept basic_lockable = requires(T& lock)
{
	{ lock.lock() } -> std::same_as<void>;
	{ lock.unlock() } -> std::same_as<void>;
};

template<typename T>
concept lockable = basic_lockable<T> && requires(T& lock)
{
	{ lock.try_lock() } -> std::same_as<bool>;
};

static_assert(basic_lockable<opsy::mutex>, "opsy::mutex must work with std::lock_guard");
static_assert(lockable<opsy::mutex>, "opsy::mutex must work with the multi-mutex algorithms");

[[gnu::used]] void use_condition_variable()
{
	g_cv.notify_one();
	g_cv.notify_all();
	g_cv_with_priority.notify_one();
	g_cv.wait();
	g_cv.wait(g_isr_lock);
	(void) g_cv.wait_for(10ms);
	(void) g_cv.wait_for(g_isr_lock, 10ms);
	(void) g_cv.wait_until(opsy::scheduler::now() + 10ms);
	(void) g_cv.wait_until(g_isr_lock, opsy::scheduler::now() + 10ms);

	// The same three overloads with the other lock type. A condition variable
	// accepts either an isr_lock or a mutex, so both have to be instantiated
	// here or half of that surface never reaches a compiler.
	g_cv.wait(g_mutex);
	(void) g_cv.wait_for(g_mutex, 10ms);
	(void) g_cv.wait_until(g_mutex, opsy::scheduler::now() + 10ms);
}

// constinit: both are ready before any constructor runs, for an interrupt
// handler that fires that early.
constinit opsy::counting_semaphore<16> g_semaphore{ 0 };
constinit opsy::binary_semaphore g_binary{ 1 };
static_assert(opsy::counting_semaphore<16>::max() == 16);
static_assert(opsy::binary_semaphore::max() == 1);

[[gnu::used]] void use_semaphore()
{
	g_semaphore.release();
	g_semaphore.release(3);
	g_semaphore.acquire();
	(void) g_semaphore.try_acquire();
	(void) g_semaphore.try_acquire_for(10ms);
	(void) g_semaphore.try_acquire_until(opsy::scheduler::now() + 10ms);
	g_binary.acquire();
	g_binary.release();
}

constinit opsy::queue<uint32_t, 8> g_queue;
constinit opsy::queue<opsy::callback<void()>, 4> g_work;
static_assert(decltype(g_queue)::capacity() == 8);

[[gnu::used]] void use_queue()
{
	(void) g_queue.try_push(1U);
	(void) g_queue.try_emplace(2U);
	(void) g_queue.push(3U);
	(void) g_queue.push_for(4U, 10ms);
	(void) g_queue.try_pop();
	(void) g_queue.pop();
	(void) g_queue.pop_for(10ms);
	(void) g_queue.size();
	(void) g_queue.empty();
	(void) g_queue.dropped();
	(void) g_queue.high_water();
	(void) g_queue.is_closed();
	g_queue.close();

	// A move-only element with a non-trivial destructor: the work-queue shape.
	(void) g_work.try_push([] {});
	if (auto job = g_work.try_pop())
		(*job)();
}

[[gnu::used]] void use_scheduler()
{
	(void) opsy::scheduler::all_tasks();
	(void) opsy::scheduler::now();
	const auto cs = opsy::scheduler::try_critical_section();
	(void) cs;
}

[[gnu::used]] void use_idle_task()
{
	// Exercise both forms: explicit entry through prepare(), and the
	// default WFI loop pulled in by scheduler::start's default arguments.
	g_idle.prepare([]{ while (true) opsy::cortex_m::wfi(); });
	g_idle.prepare(opsy::default_idle_loop);
}

[[gnu::used]] void use_sleep()
{
	opsy::sleep_for(100ms);
	opsy::sleep_until(opsy::scheduler::now() + 100ms);
}

[[gnu::used]] void use_cortex_m()
{
	(void) opsy::cortex_m::ipsr();
	(void) opsy::cortex_m::current_priority();
	const auto previous = opsy::cortex_m::set_basepri(opsy::isr_priority{ 0x80 });
	(void) previous;
	opsy::cortex_m::wfi();
	opsy::cortex_m::nop();
	(void) opsy::cortex_m::is_primask();
	(void) opsy::cortex_m::msp();
	(void) opsy::cortex_m::enable_cycle_count();
	opsy::cortex_m::cycle_count(0);
	(void) opsy::cortex_m::cycle_count();
	(void) opsy::cortex_m::psp();
}

[[gnu::used]] void use_isr_priority()
{
	constexpr auto p = opsy::isr_priority::from_preempt_sub<opsy::preemption_bits>(0, 0);
	(void) p.value();
	(void) p.preempt<opsy::preemption_bits>();
	(void) p.sub<opsy::preemption_bits>();
	(void) p.masked_value<opsy::preemption_bits>();
}

void fake_handler() {}

// Dispatch slots set at compile time: no dynamic initialisation.
constinit opsy::callback<void()> g_by_address = &fake_handler;
constinit opsy::callback<void()> g_by_name = fake_handler;
constinit opsy::callback<void()> g_stateless = [] { fake_handler(); };
#ifdef __cpp_lib_constant_wrapper
constinit opsy::callback<void()> g_wrapped = std::cw<&fake_handler>;
#endif

[[gnu::used]] void use_callback()
{
	opsy::callback<void(int)> cb{ [](int x){ (void) x; } };
	cb(42);
	if (cb) cb(7);

	g_by_address();
	g_by_name();
	g_stateless();
	opsy::callback<void()> moved = static_cast<opsy::callback<void()>&&>(g_stateless);
	moved();
	g_stateless = &fake_handler;
}

// ───────────────────── a vector table built at compile time ─────────────────
// scheduler::isrs and hooked_isr are written against what a generated table
// offers -- SvdGenerator's vectors.hpp -- without OpSy depending on it. This
// stand-in has the shape that matters and nothing more: the exception
// enumeration, with<>() filling one slot and returning the table, with(install)
// handing the table to a function, and a Decoration applied to the device
// interrupts. Checked at compile time, on every target.

using handler_t = void (*)();

enum class fake_exception : std::uint8_t { nmi = 2, hard_fault = 3, mem_manage = 4, bus_fault = 5, usage_fault = 6, svcall = 11, pendsv = 14, systick = 15 };
enum class fake_irq : std::uint8_t { uart = 0, timer = 1 };

template<typename Decoration>
class fake_vector_table
{
public:
	using exception_type = fake_exception;
	using irq_type = fake_irq;

	template<fake_exception E, handler_t H>
	[[nodiscard]] constexpr auto with() const -> fake_vector_table
	{
		auto copy = *this;
		copy.slots_[static_cast<std::size_t>(E)] = H;
		return copy;
	}

	template<fake_irq I, handler_t H>
	[[nodiscard]] constexpr auto with() const -> fake_vector_table
	{
		auto copy = *this;
		copy.slots_[16 + static_cast<std::size_t>(I)] = Decoration::template wrap<H>;
		return copy;
	}

	[[nodiscard]] constexpr auto with(auto&& install) const { return install(*this); }

	[[nodiscard]] constexpr auto handler(std::size_t slot) const -> handler_t { return slots_[slot]; }

private:
	std::array<handler_t, 18> slots_{};
};

void fake_uart_isr() {}

constexpr auto g_fake_vectors = fake_vector_table<opsy::hooked_isr>{}
	.with(opsy::scheduler::isrs)
	.with<fake_irq::uart, fake_uart_isr>();

static_assert(opsy::vector_table<fake_vector_table<opsy::hooked_isr>>);
static_assert(std::same_as<decltype(g_fake_vectors), const fake_vector_table<opsy::hooked_isr>>);

// The three handlers, in the slots the core vectors them from, and nothing else.
static_assert(g_fake_vectors.handler(11) == &SVC_Handler);
static_assert(g_fake_vectors.handler(14) == &PendSV_Handler);
static_assert(g_fake_vectors.handler(15) == &SysTick_Handler);
static_assert(g_fake_vectors.handler(2) == nullptr && g_fake_vectors.handler(3) == nullptr);

// A device interrupt goes through hook::decorate_isr; whatever the hooks make
// of it is what the table holds.
static_assert(g_fake_vectors.handler(16) == opsy::hook::decorate_isr<fake_uart_isr>());
static_assert(opsy::hooked_isr::wrap<fake_uart_isr> == opsy::hook::decorate_isr<fake_uart_isr>());

// hook forwards to the hooks type it is given. One that defines nothing
// leaves the handler alone; one that decorates is what the table gets.
void other_isr() {}

struct no_hooks
{};

struct decorating_hooks
{
	template<void (*)()>
	static constexpr auto decorate_isr() { return &other_isr; }
};

static_assert(opsy::hook::decorate_isr<fake_uart_isr, no_hooks>() == &fake_uart_isr);
static_assert(opsy::hook::decorate_isr<fake_uart_isr, decorating_hooks>() == &other_isr);

// A hook with two forms takes the fuller one when the hooks define it, the
// shorter one otherwise, and nothing when they define neither. Seen through
// what the relay returns: these hooks return a tag where OpSy's return
// nothing, which is what the relay hands back.
struct full_form {};
struct short_form {};

struct short_form_hooks
{
	static short_form task_ready(opsy::task_control_block&);
	static short_form task_sleep(opsy::task_control_block&);
	static short_form task_priority_changed(opsy::task_control_block&);
};

struct both_forms_hooks
{
	static full_form task_ready(opsy::task_control_block&, opsy::wake_reason);
	static short_form task_ready(opsy::task_control_block&);
	static full_form task_sleep(opsy::task_control_block&, opsy::time_point);
	static short_form task_sleep(opsy::task_control_block&);
	static full_form task_priority_changed(opsy::task_control_block&, opsy::task_priority);
	static short_form task_priority_changed(opsy::task_control_block&);
};

template<class Hooks>
using task_ready_result = decltype(opsy::hook::task_ready<Hooks>(std::declval<opsy::task_control_block&>(), opsy::wake_reason::notified));

template<class Hooks>
using task_sleep_result = decltype(opsy::hook::task_sleep<Hooks>(std::declval<opsy::task_control_block&>(), opsy::time_point{}));

static_assert(std::same_as<task_ready_result<both_forms_hooks>, full_form>);
static_assert(std::same_as<task_ready_result<short_form_hooks>, short_form>);
static_assert(std::same_as<task_ready_result<no_hooks>, void>);
static_assert(std::same_as<task_sleep_result<both_forms_hooks>, full_form>);
static_assert(std::same_as<task_sleep_result<short_form_hooks>, short_form>);
static_assert(std::same_as<task_sleep_result<no_hooks>, void>);

template<class Hooks>
using task_priority_changed_result = decltype(opsy::hook::task_priority_changed<Hooks>(std::declval<opsy::task_control_block&>(), opsy::task_priority::normal));

static_assert(std::same_as<task_priority_changed_result<both_forms_hooks>, full_form>);
static_assert(std::same_as<task_priority_changed_result<short_form_hooks>, short_form>);
static_assert(std::same_as<task_priority_changed_result<no_hooks>, void>);

// A table that cannot name one of the three exceptions is not one OpSy can
// start on, and says so before a line of it is built.
enum class no_pendsv_exception : std::uint8_t { svcall = 11, systick = 15 };

struct no_pendsv_table
{
	using exception_type = no_pendsv_exception;

	template<no_pendsv_exception, handler_t>
	[[nodiscard]] constexpr auto with() const -> no_pendsv_table { return *this; }
};

static_assert(!opsy::vector_table<no_pendsv_table>);

// One whose with<>() does not hand back a table cannot be chained.
struct no_chaining_table
{
	using exception_type = fake_exception;

	template<fake_exception, handler_t>
	constexpr void with() const {}
};

static_assert(!opsy::vector_table<no_chaining_table>);

// ─────────────────────────── opsy::fault_isrs ──────────────────────────────
// One entry behind the four fault exceptions, next to the scheduler's three.

void fake_on_fault(const opsy::fault_status& /*status*/, opsy::exception_frame& /*frame*/, uint32_t /*exc_return*/) {}

constexpr auto g_fake_fault_vectors = fake_vector_table<opsy::hooked_isr>{}
	.with(opsy::scheduler::isrs)
	.with(opsy::fault_isrs<fake_on_fault>);

static_assert(opsy::fault_vector_table<fake_vector_table<opsy::hooked_isr>, fake_on_fault>);

static_assert(g_fake_fault_vectors.handler(3) == &opsy::detail::fault_entry<fake_on_fault>);
static_assert(g_fake_fault_vectors.handler(4) == &opsy::detail::fault_entry<fake_on_fault>);
static_assert(g_fake_fault_vectors.handler(5) == &opsy::detail::fault_entry<fake_on_fault>);
static_assert(g_fake_fault_vectors.handler(6) == &opsy::detail::fault_entry<fake_on_fault>);
static_assert(g_fake_fault_vectors.handler(2) == nullptr);
static_assert(g_fake_fault_vectors.handler(11) == &SVC_Handler);

// A table that cannot name one of the four faults is not one the installer fills.
enum class no_usage_fault_exception : std::uint8_t { hard_fault = 3, mem_manage = 4, bus_fault = 5 };

struct no_usage_fault_table
{
	using exception_type = no_usage_fault_exception;

	template<no_usage_fault_exception, handler_t>
	[[nodiscard]] constexpr auto with() const -> no_usage_fault_table { return *this; }
};

static_assert(!opsy::fault_vector_table<no_usage_fault_table, fake_on_fault>);

// ──────────────────────── compile-time evaluation ──────────────────────────
// The parts of the kernel that hold no hardware state run in a constant
// evaluation: the intrusive list the scheduler is built on, callbacks (empty,
// or built from a function or a stateless target), isr_lock reassignment, a
// fresh mutex.

namespace compile_time
{

struct item : opsy::embedded_node<item>
{
	int priority;
	constexpr explicit item(int p) : priority(p) {}
};

constexpr bool by_priority(const item& a, const item& b) { return a.priority < b.priority; }

// Sorted insertion, iteration, erase and pop, as the scheduler uses them.
constexpr int sorted_insertion()
{
	item a{ 3 };
	item b{ 1 };
	item c{ 4 };
	item d{ 2 };
	opsy::embedded_list<item> list;
	list.insert_when(by_priority, a);
	list.insert_when(by_priority, b);
	list.insert_when(by_priority, c);
	list.insert_when(by_priority, d);
	int order = 0;
	for (const item& i : list)
		order = (order * 10) + i.priority;
	list.erase(a);
	list.pop_front();
	order = (order * 100) + (static_cast<int>(list.size()) * 10) + list.front().priority;
	list.clear();
	return order;
}
static_assert(sorted_insertion() == 123422);

// The const iterator and the move constructor.
constexpr int moved_list()
{
	item a{ 3 };
	item b{ 1 };
	opsy::embedded_list<item> list;
	list.push_front(a);
	list.push_front(b);
	const auto& view = list;
	int sum = 0;
	for (const item& i : view)
		sum += i.priority;
	opsy::embedded_list<item> moved{ static_cast<opsy::embedded_list<item>&&>(list) };
	sum += static_cast<int>(moved.size()) * 100;
	moved.clear();
	return sum;
}
static_assert(moved_list() == 204);

constexpr bool empty_callback()
{
	const opsy::callback<void()> unset;
	opsy::callback<void()> null = nullptr;
	null = nullptr;
	return !unset && null == nullptr;
}
static_assert(empty_callback());

// Callbacks called inside a constant evaluation, as code that takes one is.
constexpr int twice(int x) { return 2 * x; }
constexpr int apply_twice(const opsy::callback<int(int) const>& f, int x) { return f(f(x)); }

static_assert(apply_twice([](int y) { return y + 1; }, 5) == 7);
#if __cpp_constexpr >= 202306L // a cast from void* in a constant expression
static_assert(apply_twice(&twice, 5) == 20);
static_assert(apply_twice(twice, 5) == 20);
#endif
#ifdef __cpp_lib_constant_wrapper
static_assert(apply_twice(std::cw<&twice>, 5) == 20);
#endif

constexpr int noexcept_call()
{
	opsy::callback<int(int) noexcept> f = [](int y) noexcept { return y * 3; };
	return f(14);
}
static_assert(noexcept_call() == 42);

constexpr bool isr_lock_reassignment()
{
	opsy::isr_lock with_priority{ opsy::isr_priority{ 0x80 } };
	opsy::isr_lock lock;
	lock = static_cast<opsy::isr_lock&&>(with_priority);
	return lock.priority().has_value();
}
static_assert(isr_lock_reassignment());

constexpr bool fresh_mutex_is_free()
{
	const opsy::mutex m;
	return !m.is_locked();
}
static_assert(fresh_mutex_is_free());

} // namespace compile_time

// ─────────────────────────── opsy::fault_status ────────────────────────────
// The decoder, on values written by hand from the ARMv7-M / ARMv8-M reference
// manuals: each predicate reads its own bit and no other.

namespace fault_status_decoding
{

constexpr opsy::fault_status nothing{.cfsr = 0, .hfsr = 0, .mmfar = 0, .bfar = 0};
static_assert(!nothing.escalated() && !nothing.vector_table_read());
static_assert(!nothing.undefined_instruction() && !nothing.divide_by_zero() && !nothing.stack_overflow());
static_assert(!nothing.memory_fault_address().has_value() && !nothing.bus_fault_address().has_value());

// UFSR, escalated: __builtin_trap with the configurable faults disabled.
constexpr opsy::fault_status trap{.cfsr = 1U << 16, .hfsr = 1U << 30, .mmfar = 0, .bfar = 0};
static_assert(trap.escalated() && trap.undefined_instruction());
static_assert(!trap.invalid_state() && !trap.invalid_pc() && !trap.no_coprocessor());
static_assert(!trap.unaligned_access() && !trap.divide_by_zero() && !trap.stack_overflow());

constexpr opsy::fault_status usage{.cfsr = (1U << 17) | (1U << 18) | (1U << 19) | (1U << 20) | (1U << 24) | (1U << 25), .hfsr = 0, .mmfar = 0, .bfar = 0};
static_assert(usage.invalid_state() && usage.invalid_pc() && usage.no_coprocessor());
static_assert(usage.stack_overflow() && usage.unaligned_access() && usage.divide_by_zero());
static_assert(!usage.undefined_instruction() && !usage.escalated());

// MMFSR: a data access violation with its address; the bus side untouched.
constexpr opsy::fault_status mem_manage{.cfsr = (1U << 1) | (1U << 7), .hfsr = 0, .mmfar = 0x20001000U, .bfar = 0xDEADBEEFU};
static_assert(mem_manage.data_access_violation() && !mem_manage.instruction_access_violation());
static_assert(mem_manage.memory_fault_address() == 0x20001000U);
static_assert(!mem_manage.bus_fault_address().has_value());
static_assert(!mem_manage.precise_bus_fault() && !mem_manage.stacking_error());

// Without MMARVALID the address register means nothing, whatever it holds.
constexpr opsy::fault_status mem_manage_no_address{.cfsr = 1U << 1, .hfsr = 0, .mmfar = 0x20001000U, .bfar = 0};
static_assert(!mem_manage_no_address.memory_fault_address().has_value());

// BFSR: precise, with its address.
constexpr opsy::fault_status bus{.cfsr = (1U << 9) | (1U << 15), .hfsr = 0, .mmfar = 0, .bfar = 0x40000000U};
static_assert(bus.precise_bus_fault() && !bus.imprecise_bus_fault() && !bus.instruction_bus_fault());
static_assert(bus.bus_fault_address() == 0x40000000U);
static_assert(!bus.memory_fault_address().has_value());

constexpr opsy::fault_status imprecise{.cfsr = (1U << 8) | (1U << 10), .hfsr = 0, .mmfar = 0, .bfar = 0};
static_assert(imprecise.imprecise_bus_fault() && imprecise.instruction_bus_fault() && !imprecise.precise_bus_fault());

// Stacking, from either side.
static_assert(opsy::fault_status{.cfsr = 1U << 4, .hfsr = 0, .mmfar = 0, .bfar = 0}.stacking_error());
static_assert(opsy::fault_status{.cfsr = 1U << 12, .hfsr = 0, .mmfar = 0, .bfar = 0}.stacking_error());
static_assert(opsy::fault_status{.cfsr = 1U << 3, .hfsr = 0, .mmfar = 0, .bfar = 0}.unstacking_error());
static_assert(opsy::fault_status{.cfsr = 1U << 11, .hfsr = 0, .mmfar = 0, .bfar = 0}.unstacking_error());
static_assert(opsy::fault_status{.cfsr = 1U << 5, .hfsr = 0, .mmfar = 0, .bfar = 0}.lazy_fp_state_error());
static_assert(opsy::fault_status{.cfsr = 1U << 13, .hfsr = 0, .mmfar = 0, .bfar = 0}.lazy_fp_state_error());
static_assert(!opsy::fault_status{.cfsr = 1U << 4, .hfsr = 0, .mmfar = 0, .bfar = 0}.unstacking_error());

// HFSR on its own: a vector table the core could not read.
static_assert(opsy::fault_status{.cfsr = 0, .hfsr = 1U << 1, .mmfar = 0, .bfar = 0}.vector_table_read());
static_assert(!opsy::fault_status{.cfsr = 0, .hfsr = 1U << 1, .mmfar = 0, .bfar = 0}.escalated());

} // namespace fault_status_decoding

} // namespace
