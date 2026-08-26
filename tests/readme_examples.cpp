/**
 ******************************************************************************
 * @file    readme_examples.cpp
 * @brief   The code blocks from README.md, compiled.
 *
 *          Documentation that does not compile is worse than no documentation:
 *          a reader trusts it, pastes it, and loses time on an error that is
 *          ours. This translation unit holds every ```cpp block from the
 *          top-level README so the compiler checks them on every target, under
 *          the same strict warning set as the rest.
 *
 *          Keep it in sync by hand: if you edit an example in README.md, edit
 *          it here too. The only changes allowed are the ones an example needs
 *          to exist inside a shared translation unit — each block gets its own
 *          namespace, `main` becomes a named function, and the hardware the
 *          examples call (`led_on`, ...) is stubbed just below.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include <mutex>

#include <opsy.hpp>

namespace
{

// Stand-ins for the board support the examples assume.
void led_on() {}
void led_off() {}

// ─────────────────────────── "Quick start" ─────────────────────────────────

namespace quick_start
{

[[gnu::used]] int main_example()
{
	// Configure clocks, peripherals, ...
	opsy::scheduler::start(); // [[noreturn]]
}

} // namespace quick_start

// ───────────────────────────── "Blinky" ────────────────────────────────────

namespace blinky
{

using namespace std::chrono_literals;

opsy::task<512> blinker; // 512 stack slots = 2 KiB on Cortex-M

[[gnu::used]] int main_example()
{
	// ... clock and GPIO init ...

	// start() is [[nodiscard]] -- it returns false if the task was already
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

} // namespace blinky

// ─────────────────────────── "opsy::mutex" ─────────────────────────────────

namespace mutex_example
{

opsy::mutex m{opsy::isr_priority{0x80}};

[[gnu::used]] void from_task()
{
	std::lock_guard guard{m};
	// critical section, masks both task switch
	// and any ISR with priority numerically >= 0x80
}

} // namespace mutex_example

// ─────────────────────── "opsy::condition_variable" ────────────────────────

namespace condition_variable_example
{

opsy::mutex              m{opsy::isr_priority{0x80}};
opsy::condition_variable cv{opsy::isr_priority{0x80}};

[[gnu::used]] void wait_for_data()
{
	std::lock_guard guard{m};
	cv.wait(m); // atomically releases m, sleeps,
	            // re-acquires m on wake
}

[[gnu::used]] void from_isr()
{
	cv.notify_one(); // safe to call from ISR at the cv's priority
}

} // namespace condition_variable_example

// ────────────────────────────── "Sleeping" ─────────────────────────────────

namespace sleeping
{

using namespace std::chrono_literals;

[[gnu::used]] void sleeps()
{
	opsy::sleep_for(500ms);
	opsy::sleep_until(opsy::scheduler::now() + 1s);
}

} // namespace sleeping

} // namespace
