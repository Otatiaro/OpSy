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

#include <chrono>
#include <concepts>

namespace {

using namespace std::chrono_literals;

// One instance of every primitive — exercises all default constructors
// and the priority-aware overloads.
opsy::task<512>          g_task;
opsy::idle_task<256>     g_idle;
opsy::isr_lock           g_isr_lock;
opsy::isr_lock           g_isr_lock_with_priority{ opsy::isr_priority{ 0x80 } };
// Two of them: the standard multi-mutex algorithms below need more than one.
opsy::mutex              g_mutex;
opsy::mutex              g_second_mutex;
opsy::condition_variable g_cv;
opsy::condition_variable g_cv_with_priority{ opsy::isr_priority{ 0x80 } };

[[gnu::used]] void use_task()
{
	(void) g_task.start([]{ while (true) opsy::sleep_for(1ms); });
	g_task.set_name("blinker");
	(void) g_task.name();
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

[[gnu::used]] void use_scheduler()
{
	(void) opsy::scheduler::all_tasks();
	(void) opsy::scheduler::now();
	auto cs = opsy::scheduler::try_critical_section();
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
	auto previous = opsy::cortex_m::set_basepri(opsy::isr_priority{ 0x80 });
	(void) previous;
	opsy::cortex_m::wfi();
	opsy::cortex_m::nop();
	(void) opsy::cortex_m::is_primask();
	(void) opsy::cortex_m::msp();
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

[[gnu::used]] void use_callback()
{
	opsy::callback<void(int)> cb{ [](int x){ (void) x; } };
	cb(42);
	if (cb) cb(7);
}

} // namespace
