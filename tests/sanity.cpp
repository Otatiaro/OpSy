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

namespace {

using namespace std::chrono_literals;

// One instance of every primitive — exercises all default constructors
// and the priority-aware overloads.
opsy::task<512>          g_task;
opsy::idle_task<256>     g_idle;
opsy::mutex              g_mutex_task_only;
opsy::mutex              g_mutex_with_priority{ opsy::isr_priority{ 0x80 } };
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
	(void) g_task.stop();
}

[[gnu::used]] void use_mutex()
{
	g_mutex_with_priority.lock();
	(void) g_mutex_with_priority.priority();
	g_mutex_with_priority.unlock();
}

[[gnu::used]] void use_condition_variable()
{
	g_cv.notify_one();
	g_cv.notify_all();
	g_cv.wait();
	g_cv.wait(g_mutex_task_only);
	(void) g_cv.wait_for(10ms);
	(void) g_cv.wait_for(g_mutex_task_only, 10ms);
	(void) g_cv.wait_until(opsy::scheduler::now() + 10ms);
	(void) g_cv.wait_until(g_mutex_task_only, opsy::scheduler::now() + 10ms);
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
