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
#include <mutex>

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

	std::lock_guard<opsy::mutex> guard(g_second_mutex);
}

/**
 * @brief Checks that @c opsy::mutex satisfies the standard @c Lockable concept
 *
 *        @c std::lock takes several mutexes at once, in an order of its own
 *        choosing, so that two tasks locking the same pair cannot deadlock by
 *        taking them in opposite orders. To do so it needs each one to provide
 *        @c lock() , @c try_lock() and @c unlock() , with @c try_lock()
 *        declining rather than blocking so the algorithm can back out and
 *        retry. Instantiating it here is what checks that @c opsy::mutex
 *        provides all three with the right signatures.
 *
 *        This lives in the compile-only build rather than in the on-target
 *        suite because it cannot be linked into an image: @c std::lock pulls
 *        in the ARM unwinder even under @c -fno-exceptions , and the linker
 *        scripts for the test images discard the exception index tables it
 *        needs. Nothing here is ever linked or run, so the instantiation
 *        costs nothing and still typechecks every call.
 *
 * @remark @c std::scoped_lock would express the same thing as a guard object,
 *         but the freestanding libstdc++ shipped with the bare-metal ARM
 *         toolchain does not define it.
 */
[[gnu::used]] void use_mutex_as_lockable()
{
	std::lock(g_mutex, g_second_mutex);
	std::lock_guard<opsy::mutex> first(g_mutex, std::adopt_lock);
	std::lock_guard<opsy::mutex> second(g_second_mutex, std::adopt_lock);
}

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
