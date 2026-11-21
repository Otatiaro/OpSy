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
 *          namespace, and the hardware the examples call (`led_on`, ...) is
 *          stubbed just below.
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

[[gnu::used, noreturn]] void application()
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

[[gnu::used, noreturn]] void application()
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

enum class exception : std::uint8_t { hard_fault = 3, mem_manage = 4, bus_fault = 5, usage_fault = 6, svcall = 11, pendsv = 14, systick = 15 };
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

// ────────────────────────────── "Faults" ───────────────────────────────────

namespace faults_example
{

using vector_table_example::_estack;
using vector_table_example::Reset_Handler;
using vector_table_example::Default_Handler;
using namespace vector_table_example::STM32H563;

[[noreturn]] void reset() { while (true) {} }

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

[[gnu::used]] void application()
{
	opsy::cortex_m::enable_configurable_faults();
	opsy::cortex_m::enable_divide_by_zero_trap();
	opsy::cortex_m::set_priority(opsy::cortex_m::system_irq::usage_fault, opsy::isr_priority{0});
	opsy::scheduler::start();
}

} // namespace faults_example

// ──────────────────────── "ISR priorities and OpSy" ────────────────────────

// What the README's table says of the levels, whatever the split: OpSy's
// level holds SVC and SysTick, PendSV is at the very bottom, and handlers
// call OpSy from the level right below OpSy's.
constexpr auto opsy_level = static_cast<uint8_t>(opsy::opsy_preemption);
static_assert(opsy::scheduler::service_call_priority.preempt<opsy::preemption_bits>() == opsy_level);
static_assert(opsy::scheduler::systick_priority.preempt<opsy::preemption_bits>() == opsy_level);
static_assert(opsy::scheduler::pend_sv_priority.value() == 0xFF);
static_assert(opsy::scheduler::os_callable_priority.preempt<opsy::preemption_bits>() == opsy_level + 1);

#ifdef OPSY_SANITY_EXAMPLES
// Built with tests/examples/ on the include path: the README's opsy_config.hpp
// is the configuration, not config.hpp's defaults.
static_assert(opsy::preemption_bits == 3);
static_assert(opsy::scheduler::service_call_priority.value() == 0x20);
static_assert(opsy::scheduler::systick_priority.value() == 0x3F);
static_assert(opsy::scheduler::os_callable_priority.value() == 0x40);

// And the README's opsy_hooks.hpp is the hooks: its latency exists, and the
// decoration it applies is what the vector table holds. The library compiling
// at all is what checks that it defines every hook scheduler.cpp calls.
[[gnu::used]] uint32_t worst_isr_cycles() { return opsy::latency::isr.load(std::memory_order_relaxed); }
static_assert(opsy::hooked_isr::wrap<vector_table_example::uart_isr> != &vector_table_example::uart_isr);
#else
static_assert(opsy::preemption_bits == 2);
static_assert(opsy::scheduler::service_call_priority.value() == 0x40);
static_assert(opsy::scheduler::systick_priority.value() == 0x7F);
static_assert(opsy::scheduler::os_callable_priority.value() == 0x80);
#endif

} // namespace
