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

#include <atomic>
#include <mutex>
#include <utility>

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

// ─────────────────────── "opsy::mutex — between tasks" ─────────────────────

namespace mutex_example
{

opsy::mutex m;

[[gnu::used]] void from_task()
{
	std::lock_guard guard{m};
	// exclusive against other tasks; interrupts keep running
}

} // namespace mutex_example

// ────────────────── "opsy::isr_lock — between a task and an ISR" ───────────

namespace isr_lock_example
{

opsy::isr_lock l{opsy::isr_priority{0x80}};

[[gnu::used]] void from_task()
{
	std::lock_guard guard{l};
	// masks every ISR at priority numerically >= 0x80,
	// and task switching along with it
}

[[gnu::used]] void from_isr()
{
	std::lock_guard guard{l};   // same lock, from the handler side
}

} // namespace isr_lock_example

// ─────────────────────── "opsy::condition_variable" ────────────────────────

namespace condition_variable_example
{

opsy::isr_lock           lock{opsy::scheduler::os_callable_priority};
opsy::condition_variable events_changed{opsy::scheduler::os_callable_priority};
uint32_t                 events = 0;      // guarded by lock

constexpr uint32_t rx_done = 1u << 0;
constexpr uint32_t error   = 1u << 1;

[[gnu::used]] void uart_isr()             // strictly below OpSy's preemption level
{
	{
		std::lock_guard guard{lock};
		events |= rx_done;
	}
	events_changed.notify_one();
}

[[gnu::used]] void uart_task()
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
		(void) got;                        // handle got, outside the lock
	}
}

} // namespace condition_variable_example

// ──────────────── "Raising an interrupt from software" ─────────────────────

namespace raising_an_interrupt
{

uint32_t g_request = 0;

[[gnu::used]] void hand_over(uint32_t request, uint32_t irq)
{
	g_request = request;
	std::atomic_signal_fence(std::memory_order_seq_cst);
	opsy::cortex_m::set_pending(irq);
}

} // namespace raising_an_interrupt

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

// ─────────────────────────── "The vector table" ────────────────────────────

namespace vector_table_example
{

// Stand-in for the vectors.hpp SvdGenerator generates: the part of its
// vector_table the example uses, and the symbols a startup file provides.
// OpSy does not depend on the generator; sanity.cpp checks what the table
// ends up holding.
std::uint32_t _estack = 0;
void Reset_Handler() {}
void Default_Handler() {}
void uart_isr() {}

namespace STM32H563
{

using isr_handler_t = void (*)();

enum class exception : std::uint8_t { svcall = 11, pendsv = 14, systick = 15 };
enum IRQn_Type : std::uint8_t { USART3_IRQn = 60 };

template<isr_handler_t Unhandled, typename Decoration>
class vector_table
{
public:
	using exception_type = exception;

	constexpr vector_table(std::uint32_t*, isr_handler_t)
	{
		for (auto& handler : handlers_)
			handler = Unhandled;
	}

	template<exception Exception, isr_handler_t Handler>
	constexpr auto with() const -> vector_table
	{
		auto table = *this;
		table.handlers_[static_cast<std::size_t>(Exception)] = Handler;
		return table;
	}

	template<IRQn_Type Irq, isr_handler_t Handler>
	constexpr auto with() const -> vector_table
	{
		auto table = *this;
		table.handlers_[16 + Irq] = Decoration::template wrap<Handler>;
		return table;
	}

	constexpr auto with(auto&& install) const { return install(*this); }

private:
	isr_handler_t handlers_[16 + 64]{};
};

} // namespace STM32H563

using namespace STM32H563;

[[gnu::section(".isr_vector"), gnu::used]]
constexpr auto g_vectors = vector_table<Default_Handler, opsy::hooked_isr>(&_estack, Reset_Handler)
    .with(opsy::scheduler::isrs)
    .with<USART3_IRQn, uart_isr>();

} // namespace vector_table_example

} // namespace
