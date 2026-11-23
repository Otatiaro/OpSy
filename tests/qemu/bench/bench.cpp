/**
 ******************************************************************************
 * @file    bench.cpp
 * @brief   The paths through OpSy that cost, each between two markers.
 *
 *          One image, two instruments. Under QEMU, tests/qemu/bench/profile.py
 *          reads the execution trace and counts the instructions between the
 *          markers, per function: deterministic, and what the CI keeps as a
 *          baseline. On a board, the markers read the DWT cycle counter and
 *          the image prints cycles: what the hardware adds that instructions
 *          do not show -- exception entry and return, barriers, the memory
 *          system.
 *
 *          The markers are bench::begin<S>() and bench::end<S>(), one pair of
 *          functions per scenario so the trace tells them apart by address.
 *          Each scenario runs bench::iterations times; the image prints the
 *          minimum and the median. For the wake-up from an interrupt, the
 *          hooks in opsy_hooks.hpp add the points in between (bench::mark).
 *
 *          Two tasks: `driver`, the higher one, runs the scenarios and is the
 *          side that gets woken; `other`, the lower one, is the side that
 *          wakes it -- directly, or by pending the interrupt whose handler
 *          does. Nothing else runs: SysTick, and the idle task when both wait.
 *
 *          Built for a board with OPSY_BENCH_BOARD defined: the clock goes to
 *          the board's nominal frequency first, the cycle counter is started,
 *          and the results are also left in bench::results for a debugger to
 *          read when semihosting is not available.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"
#include "semihosting.hpp"

#include <opsy.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

extern "C"
{
extern uint32_t _estack;

void Reset_Handler();
}

namespace bench
{

enum class scenario : uint8_t
{
	switch_ping_pong,            ///< driver releases other and waits for it: two switches, one through the service call
	semaphore_from_isr,          ///< driver in acquire(), the handler calls release()
	condition_variable_from_isr, ///< driver in wait(isr_lock&), the handler sets a flag and calls notify_one()
	queue_from_isr,              ///< driver in pop(), the handler calls push()
	mutex_free,                  ///< lock() and unlock() with nobody else
	mutex_contended,             ///< other holds it, driver blocks and inherits, other unlocks
	sleep_one_tick,              ///< from the SysTick that ends the sleep to sleep_for() returning
	isr_lock_round_trip,         ///< lock() and unlock() of an isr_lock from a task
	release_no_switch,           ///< driver releases what a lower task waits on: it becomes ready, nobody switches
	release_past_eight,          ///< the same, with eight tasks above the woken one already ready: the insertion walks past them
	count,
};

constexpr const char* name(scenario s)
{
	switch (s)
	{
	case scenario::switch_ping_pong:            return "switch_ping_pong";
	case scenario::semaphore_from_isr:          return "semaphore_from_isr";
	case scenario::condition_variable_from_isr: return "condition_variable_from_isr";
	case scenario::queue_from_isr:              return "queue_from_isr";
	case scenario::mutex_free:                  return "mutex_free";
	case scenario::mutex_contended:             return "mutex_contended";
	case scenario::sleep_one_tick:              return "sleep_one_tick";
	case scenario::isr_lock_round_trip:         return "isr_lock_round_trip";
	case scenario::release_no_switch:           return "release_no_switch";
	case scenario::release_past_eight:          return "release_past_eight";
	default:                                    return "?";
	}
}

constexpr const char* name(point p)
{
	switch (p)
	{
	case point::notify:   return "notify";
	case point::ready:    return "ready";
	case point::isr_exit: return "isr_exit";
	case point::pend_sv:  return "pend_sv";
	case point::started:  return "started";
	default:              return "?";
	}
}

constexpr std::size_t iterations = 8;

/** @brief Cycles per scenario and iteration; stays 0 under QEMU, where the trace is the measure */
std::array<std::array<uint32_t, iterations>, static_cast<std::size_t>(scenario::count)> results{};

/** @brief Cycles at each point of the last wake-up from an interrupt, relative to its begin */
std::array<uint32_t, static_cast<std::size_t>(point::count)> points{};

/** @brief The core cycle counter on a board; nothing under QEMU, which does not model the DWT */
inline uint32_t cycles()
{
#ifdef OPSY_BENCH_BOARD
	return opsy::cortex_m::cycle_count();
#else
	return 0;
#endif
}

uint32_t    g_begin     = 0;
std::size_t g_iteration = 0;
bool        g_measuring = false; ///< between a begin and its end: the hooks mark only then

template<scenario S>
[[gnu::noinline]] void begin()
{
	g_measuring = true;
	g_begin     = cycles();
}

template<scenario S>
[[gnu::noinline]] void end()
{
	results[static_cast<std::size_t>(S)][g_iteration] = cycles() - g_begin;
	g_measuring = false;
}

void mark(point reached)
{
	// The semaphores that drive the scenarios go through notify_one too, and
	// their wake-ups are not the measured ones.
	if (g_measuring)
		points[static_cast<std::size_t>(reached)] = cycles() - g_begin;
}

// ─── The two tasks and what they share ──────────────────────────────────────

opsy::task<1024>    g_driver;
opsy::task<1024>    g_other;
opsy::idle_task<256> g_idle;

opsy::binary_semaphore g_other_go{0};  ///< driver → other: your turn
opsy::binary_semaphore g_driver_go{0}; ///< other → driver: done

/** @brief Eight tasks with nothing to do but be ready, for the scenario that walks the ready list past them */
constexpr std::size_t          fillers = 8;
std::array<opsy::task<256>, fillers> g_fillers;
opsy::counting_semaphore<fillers> g_filler_go{0}; ///< driver → fillers: all of you, be ready

opsy::binary_semaphore   g_semaphore{0};
opsy::isr_lock           g_lock{opsy::scheduler::os_callable_priority};
opsy::condition_variable g_condition{opsy::scheduler::os_callable_priority};
uint32_t                 g_events = 0;
opsy::queue<uint32_t, 4> g_queue;
opsy::mutex              g_mutex;

/** @brief What the handler does when `other` pends the interrupt */
scenario g_pending = scenario::count;

void systick_entered()
{
	// The sleep scenario begins at the SysTick that ends the sleep: every
	// tick during it begins again, and the last one before the task runs is
	// the one that woke it. A marker function, so the trace sees it too.
	if (g_pending == scenario::sleep_one_tick)
		begin<scenario::sleep_one_tick>();
}

namespace
{

/** @brief An interrupt nobody else uses, pended from software, at the highest priority that may call OpSy */
constexpr uint32_t irq = 0;

void bench_isr()
{
	switch (g_pending)
	{
	case scenario::semaphore_from_isr:
		begin<scenario::semaphore_from_isr>();
		g_semaphore.release();
		break;

	case scenario::condition_variable_from_isr:
		begin<scenario::condition_variable_from_isr>();
		{
			const std::scoped_lock guard{g_lock};
			g_events |= 1U;
		}
		g_condition.notify_one();
		break;

	case scenario::queue_from_isr:
		begin<scenario::queue_from_isr>();
		(void) g_queue.push(1U);
		break;

	default:
		break;
	}
}

/** @brief Runs when `driver` lets it: each turn is one step of the scenario in progress */
void other()
{
	while (true)
	{
		g_other_go.acquire();

		switch (g_pending)
		{
		case scenario::switch_ping_pong:
		case scenario::release_no_switch:
		case scenario::release_past_eight:
			g_driver_go.release();
			break;

		case scenario::mutex_contended:
			g_mutex.lock();
			g_driver_go.release();   // driver wakes, outranks us, blocks on the mutex and lends us its priority
			g_mutex.unlock();        // and runs again here
			break;

		default:
			opsy::cortex_m::set_pending(irq); // the handler does the rest
			break;
		}
	}
}

template<scenario S>
void run(auto&& once)
{
	g_pending = S;
	for (g_iteration = 0; g_iteration < iterations; ++g_iteration)
		once();
	g_pending = scenario::count;
}

void driver()
{
	using namespace std::chrono_literals;

	// From a running task, not before scheduler::start(): a priority change
	// asks for a switch, and PendSV before start has no process stack to
	// save to.
	g_driver.priority(opsy::task_priority::high);
	g_other.priority(opsy::task_priority::low);

	run<scenario::switch_ping_pong>([] {
		begin<scenario::switch_ping_pong>();
		g_other_go.release();
		g_driver_go.acquire();
		end<scenario::switch_ping_pong>();
	});

	run<scenario::semaphore_from_isr>([] {
		g_other_go.release();
		g_semaphore.acquire();
		end<scenario::semaphore_from_isr>();
	});

	run<scenario::condition_variable_from_isr>([] {
		g_other_go.release();
		{
			const std::scoped_lock guard{g_lock};
			while (g_events == 0)
				g_condition.wait(g_lock);
			g_events = 0;
		}
		end<scenario::condition_variable_from_isr>();
	});

	run<scenario::queue_from_isr>([] {
		g_other_go.release();
		(void) g_queue.pop();
		end<scenario::queue_from_isr>();
	});
	const auto queue_points = points; // before the scenarios below write the points they share

	run<scenario::mutex_free>([] {
		begin<scenario::mutex_free>();
		{
			const std::scoped_lock guard{g_mutex};
		}
		end<scenario::mutex_free>();
	});

	run<scenario::mutex_contended>([] {
		g_other_go.release();
		g_driver_go.acquire();   // other holds the mutex now
		begin<scenario::mutex_contended>();
		g_mutex.lock();          // blocks, other inherits, other unlocks, we run
		end<scenario::mutex_contended>();
		g_mutex.unlock();
	});

	run<scenario::sleep_one_tick>([] {
		// begin is the SysTick that ends the sleep: see systick_entered().
		opsy::sleep_for(1ms);
		end<scenario::sleep_one_tick>();
	});

	run<scenario::isr_lock_round_trip>([] {
		begin<scenario::isr_lock_round_trip>();
		{
			const std::scoped_lock guard{g_lock};
		}
		end<scenario::isr_lock_round_trip>();
	});

	run<scenario::release_no_switch>([] {
		begin<scenario::release_no_switch>();
		g_other_go.release();    // other becomes ready; it is lower, nobody switches
		end<scenario::release_no_switch>();
		g_driver_go.acquire();   // let it take its turn
		opsy::sleep_for(1ms);    // and get back into its acquire: we preempted it inside its release
	});

	// Above other, below us: the woken other is inserted behind all eight, and
	// none of them runs before we block.
	for (auto& filler : g_fillers)
		filler.priority(opsy::task_priority::normal);

	run<scenario::release_past_eight>([] {
		g_filler_go.release(fillers); // eight ready tasks, none above us
		begin<scenario::release_past_eight>();
		g_other_go.release();         // other goes behind the eight
		end<scenario::release_past_eight>();
		g_driver_go.acquire();        // the eight each take a turn and block again, then other
		opsy::sleep_for(1ms);         // see release_no_switch
	});

	// The report. Cycles are what a board gives; under QEMU every figure is 0
	// and profile.py's instruction counts are the measure.
	for (std::size_t s = 0; s < static_cast<std::size_t>(scenario::count); ++s)
	{
		auto& samples = results[s];
		std::ranges::sort(samples);
		qemu::write("BENCH ");
		qemu::write(name(static_cast<scenario>(s)));
		qemu::write(" min=");
		qemu::write_unsigned(samples.front());
		qemu::write(" median=");
		qemu::write_unsigned(samples[iterations / 2]);
		qemu::write(" max=");
		qemu::write_unsigned(samples.back());
		qemu::write("\n");
	}

	// The points along the last queue wake-up, as the hooks saw them.
	qemu::write("POINTS queue_from_isr");
	for (std::size_t p = 0; p < static_cast<std::size_t>(point::count); ++p)
	{
		qemu::write(" ");
		qemu::write(name(static_cast<point>(p)));
		qemu::write("=");
		qemu::write_unsigned(queue_points[p]);
	}
	qemu::write("\nRESULT: PASS\n");
	qemu::exit(0);
}

} // namespace

// ─── The vector table: OpSy's handlers, and the bench interrupt ─────────────

using handler_t = void (*)();

enum class exception : uint8_t { nmi = 2, hard_fault = 3, mem_manage = 4, bus_fault = 5, usage_fault = 6, svcall = 11, pendsv = 14, systick = 15 };

namespace
{

[[noreturn]] void unexpected_exception()
{
	qemu::write("*** an exception the table does not expect was taken: ");
	qemu::write_unsigned(opsy::cortex_m::ipsr());
	qemu::write(" ***\nRESULT: FAIL\n");
	qemu::exit(1);
}

} // namespace

/** @brief The shape SvdGenerator generates, enough of it for with(): the exceptions by name, the interrupts by number, decorated */
struct alignas(512) vector_table
{
	using exception_type = exception;

	constexpr vector_table(uint32_t* stack, handler_t reset) : initial_sp(stack)
	{
		for (auto& handler : handlers)
			handler = &unexpected_exception;
		handlers[0] = reset;
	}

	template<exception Exception, handler_t Handler>
	[[nodiscard]] constexpr auto with() const -> vector_table
	{
		auto copy = *this;
		copy.handlers[static_cast<std::size_t>(Exception) - 1] = Handler;
		return copy;
	}

	template<uint32_t Irq, handler_t Handler>
	[[nodiscard]] constexpr auto with() const -> vector_table
	{
		auto copy = *this;
		copy.handlers[15 + Irq] = opsy::hooked_isr::wrap<Handler>;
		return copy;
	}

	[[nodiscard]] constexpr auto with(auto&& install) const { return install(*this); }

	uint32_t* initial_sp;
	std::array<handler_t, 15 + 64> handlers{};
};

constexpr auto g_table = vector_table(&_estack, Reset_Handler)
	.with(opsy::scheduler::isrs)
	.with<irq, bench_isr>();

constexpr uint32_t vtor = 0xE000ED08U;

#ifdef OPSY_BENCH_BOARD
namespace
{

/**
 * @brief STM32H563 to 250 MHz: VOS0, flash at 5 wait states, PLL1 from the
 *        32 MHz HSI (/8, x125, /2), instruction cache on. The sequence and
 *        the values are the Bornier board's, written on the registers
 *        directly: this image has no device headers.
 */
void board_clock()
{
	auto reg = [](uint32_t address) -> volatile uint32_t& { return *reinterpret_cast<volatile uint32_t*>(address); };

	constexpr uint32_t pwr_voscr    = 0x44020810U, pwr_vossr = 0x44020814U;
	constexpr uint32_t flash_acr    = 0x40022000U;
	constexpr uint32_t rcc_cr       = 0x44020C00U, rcc_cfgr1 = 0x44020C1CU, rcc_cfgr2 = 0x44020C20U;
	constexpr uint32_t rcc_pll1cfgr = 0x44020C28U, rcc_pll1divr = 0x44020C34U;
	constexpr uint32_t icache_cr    = 0x40030400U, icache_sr = 0x40030404U;

	reg(pwr_voscr) = 3U << 4;                              // VOS0
	while ((reg(pwr_vossr) & (1U << 3)) == 0) {}           // VOSRDY
	reg(flash_acr) = 5U | (2U << 4) | (1U << 8);           // LATENCY 5, WRHIGHFREQ 2, PRFTEN
	while ((reg(flash_acr) & 0xFU) != 5U) {}               // the write lands before the clock rises
	reg(rcc_pll1cfgr) = 0b01U | (0b10U << 2) | (8U << 8) | (1U << 16); // HSI, 4-8 MHz in, M 8, P out
	reg(rcc_pll1divr) = 124U | (1U << 9);                  // N 125, P 2: 32 / 8 * 125 / 2 = 250 MHz
	reg(rcc_cr) |= 1U << 24;                               // PLL1ON
	while ((reg(rcc_cr) & (1U << 25)) == 0) {}             // PLL1RDY
	reg(rcc_cfgr2) = 0;                                    // every prescaler at 1
	reg(rcc_cfgr1) = 0b011U;                               // SW: PLL1
	while (((reg(rcc_cfgr1) >> 3) & 0b11U) != 0b011U) {}   // SWS
	while (reg(icache_sr) & 1U) {}                         // BUSYF
	reg(icache_cr) |= 1U;                                  // EN

	SystemCoreClock = 250'000'000U;
	(void) opsy::cortex_m::enable_cycle_count();
}

} // namespace
#endif

} // namespace bench

int opsy_test_main()
{
#ifdef OPSY_BENCH_BOARD
	bench::board_clock();
#endif
	*reinterpret_cast<volatile uint32_t*>(bench::vtor) = reinterpret_cast<uint32_t>(&bench::g_table);
	opsy::cortex_m::set_priority(bench::irq, opsy::scheduler::os_callable_priority);
	opsy::cortex_m::enable_interrupt(bench::irq);

	(void) bench::g_driver.start([] { bench::driver(); });
	(void) bench::g_other.start([] { bench::other(); });
	for (auto& filler : bench::g_fillers)
		(void) filler.start([] { while (true) bench::g_filler_go.acquire(); });

	opsy::scheduler::start(bench::g_idle);
}
