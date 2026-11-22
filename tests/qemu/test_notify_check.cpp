/**
 ******************************************************************************
 * @file    test_notify_check.cpp
 * @brief   A separate image: an interrupt more urgent than a condition
 *          variable's lock calls notify_one, which must stop on a trap.
 *
 *          The lock does not mask such an interrupt, so the interrupt could
 *          break into a wait or a notification in progress. The condition
 *          variable's lock sits below OpSy's callable level, and a handler at
 *          that level notifies. Passing means reaching HardFault on a trap
 *          taken in that handler, before notify_one returns: the check in
 *          notify_one, or the same one in isr_lock::lock right after it.
 *
 *          The check is an assert: with NDEBUG there is nothing to reach, and
 *          the image says so and passes.
 *
 *          Its own image because it never returns.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"
#include "semihosting.hpp"

#include <opsy.hpp>

#include <cstdint>

#ifdef NDEBUG

int opsy_test_main()
{
	qemu::write("OpSy notify check: the check is an assert, compiled out with NDEBUG: nothing to reach\nRESULT: PASS\n");
	qemu::exit(0);
}

#else

extern "C"
{
void notify_check_hard_fault_handler();
void notify_check_hard_fault(uint32_t* frame);
}

namespace
{

constexpr auto lock_priority = opsy::isr_priority::from_preempt_sub<opsy::preemption_bits>(opsy::opsy_preemption + 2, 0);
static_assert(lock_priority.masked_value<opsy::preemption_bits>() > opsy::scheduler::os_callable_priority.masked_value<opsy::preemption_bits>(),
		"the handler, at OpSy's callable level, is more urgent than the lock");

opsy::condition_variable g_changed{ lock_priority };
opsy::task<512> g_task;
opsy::idle_task<256> g_idle;
unsigned g_irq = qemu_test::no_irq;

[[noreturn]] void fail(const char* why)
{
	qemu::write("*** ");
	qemu::write(why);
	qemu::write(" ***\nRESULT: FAIL\n");
	qemu::exit(1);
}

void urgent_handler()
{
	g_changed.notify_one();
	fail("notify_one returned in an interrupt more urgent than its lock");
}

} // namespace

/** @brief The frame the core stacked, from whichever stack it used. */
extern "C" [[gnu::naked]] void notify_check_hard_fault_handler()
{
	asm volatile(
			"tst lr, #4\n\t"
			"ite eq\n\t"
			"mrseq r0, msp\n\t"
			"mrsne r0, psp\n\t"
			"b notify_check_hard_fault");
}

extern "C" [[gnu::used]] void notify_check_hard_fault(uint32_t* frame)
{
	const auto opcode = *reinterpret_cast<const volatile uint16_t*>(frame[6]);
	const bool trap = (opcode & 0xFF00u) == 0xDE00u   // udf: __builtin_trap
	               || (opcode & 0xFF00u) == 0xBE00u;  // bkpt: a trap() that halts a debugger

	if (!trap)
		fail("a fault, not a trap");
	if ((frame[7] & 0x1FFu) != opsy::cortex_m::system_irqs_count + g_irq) // the exception the trap interrupted, from the stacked xPSR
		fail("a trap outside the urgent handler");

	qemu::write("notify_one stopped in an interrupt more urgent than its lock\nRESULT: PASS\n");
	qemu::exit(0);
}

int opsy_test_main()
{
	qemu::write("OpSy notify check: an interrupt more urgent than the lock notifies\n\n");

	opsy::cortex_m::set_isr_handler(opsy::cortex_m::system_irq::hard_fault, &notify_check_hard_fault_handler);

	g_irq = qemu_test::find_usable_irq();
	if (g_irq == qemu_test::no_irq)
		fail("the NVIC accepts no interrupt");
	opsy::cortex_m::set_priority(g_irq, opsy::scheduler::os_callable_priority);
	opsy::cortex_m::set_isr_handler(g_irq, &urgent_handler);
	opsy::cortex_m::enable_interrupt(g_irq);

	(void) g_task.start([]
	{
		qemu_test::raise(g_irq);
		fail("the urgent handler did not run");
	});

	opsy::scheduler::start(g_idle);
}

#endif
