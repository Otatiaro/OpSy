/**
 ******************************************************************************
 * @file    test_routine.cpp
 * @brief   On-target tests for @c opsy::utility::routine , resumed from a real
 *          interrupt handler and from a task.
 *
 *          The mechanism itself — suspending, resuming, locals surviving — is
 *          covered on the host, where it runs faster and in more detail (see
 *          @c tests/host/test_routine.cpp ). What cannot be tested there is
 *          the thing a routine exists for: being driven by an interrupt.
 *
 *          These cases put one behind a real peripheral IRQ raised through the
 *          NVIC, and check that it advances one step per interrupt while
 *          genuinely running in handler mode — and that it does not care which
 *          context, or which stack, it is resumed on.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */
#include "qemu_test.hpp"
#include <opsy.hpp>
#include <utility/routine.hpp>
#include <atomic>
#include <cstdint>
namespace
{
using namespace std::chrono_literals;
/** @brief How many steps a driven routine runs through. */
constexpr int step_count = 4;
/** @brief Returned by @ref find_usable_irq when the NVIC accepts none. */
constexpr uint32_t not_found = 0xFFFFFFFFu;
/** @brief Marks a step the routine never reached, distinct from thread mode. */
constexpr uint32_t never_ran = 0xFFFFFFFFu;
/**
 * @brief Finds a peripheral IRQ this board's NVIC actually implements
 *
 *        Probed rather than written down: how many peripheral interrupts the
 *        NVIC implements differs between the boards this suite runs on, and a
 *        number outside that range is ignored in silence -- set_pending does
 *        nothing, nothing ever fires, and the case fails for a reason that
 *        looks nothing like its cause. A number written down here would be
 *        right on some of these boards and quietly inert on the others.
 *
 *        Searched downwards, so the one picked is as far as possible from the
 *        low slots the boards wire to real peripherals.
 *
 * @return An IRQ the NVIC accepts, or @ref not_found
 */
uint32_t find_usable_irq()
{
	for (uint32_t irq = 48; irq-- > 0;)
	{
		opsy::cortex_m::set_pending(irq);
		if (opsy::cortex_m::is_pending(irq))
		{
			opsy::cortex_m::clear_pending(irq);
			return irq;
		}
	}
	return not_found;
}
uint32_t g_irq = not_found;
/** @brief How far the routine has got. */
std::atomic<int> g_step = 0;
/**
 * @brief The @c IPSR the routine saw at each of its steps
 *
 *        Zero means it ran in thread mode -- resumed by a task. Non-zero is
 *        the number of the exception it was resumed inside. Recorded rather
 *        than asserted on the spot, so each case can say which of its steps
 *        it expected to be which.
 */
std::atomic<uint32_t> g_context[step_count] {};
/**
 * @brief The stack the routine ran on at each of its steps
 * @remark A routine has no stack of its own: it runs on whichever one resumed
 *         it. These values are what shows that.
 */
std::atomic<uint32_t> g_stack[step_count] {};
opsy::utility::routine_storage<192> g_storage;
opsy::utility::routine_storage<192> g_other_storage;
/**
 * @brief The stack pointer of whoever is executing
 * @remark Read from a plain function on purpose. A local of the routine would
 *         not do: those live in the frame, which is the same storage whichever
 *         stack the routine is resumed on. This function's own frame is on the
 *         real stack.
 */
[[gnu::noinline]] uint32_t current_stack_pointer()
{
	return reinterpret_cast<uint32_t>(__builtin_frame_address(0));
}
/** @brief Records, at every step, where it ran and on which stack. */
opsy::utility::routine driven(opsy::utility::routine_storage<192>&, std::atomic<int>* step)
{
	for (int i = 0; i < step_count; ++i)
	{
		g_context[i] = opsy::cortex_m::ipsr();
		g_stack[i] = current_stack_pointer();
		step->store(i + 1, std::memory_order_relaxed);
		// Just wait: what this case is about is where the routine resumes,
		// not what it reads.
		co_await std::suspend_always{};
	}
}
opsy::utility::routine g_first;
opsy::utility::routine g_second;
/** @brief What a driver's interrupt handler looks like, in full. */
extern "C" void routine_test_irq_handler()
{
	g_first.resume();
	g_second.resume();
}
/** @brief Puts the handler in the live vector table and enables the IRQ. */
bool install_handler()
{
	// Probed once, then remembered. Probing again would be wrong, not merely
	// wasteful: this leaves the chosen IRQ enabled, so a later set_pending on
	// it takes the exception there and then, the hardware clears pending, and
	// is_pending reads false -- the probe would conclude that IRQ does not
	// exist and walk one slot lower on every call, towards the low slots that
	// are wired to real peripherals.
	if (g_irq == not_found)
		g_irq = find_usable_irq();

	if (g_irq == not_found)
		return false;

	// The table is the one in RAM the image installed through VTOR at
	// startup, so writing to it is how a driver would register at run time.
	// The 16 system exception slots come first.
	opsy::cortex_m::vtor()[16 + g_irq] = &routine_test_irq_handler;
	opsy::cortex_m::enable_interrupt(g_irq);
	return true;
}
/** @brief Raises the IRQ, and returns once its handler has run. */
void fire()
{
	opsy::cortex_m::set_pending(g_irq);
	// Any peripheral interrupt outranks every task, so the handler has run
	// before this returns. The sleep lets the suite breathe; it is not what
	// waits for the handler.
	opsy::sleep_for(2ms);
}
void reset_state()
{
	g_first = opsy::utility::routine{};
	g_second = opsy::utility::routine{};
	g_step = 0;
	for (int i = 0; i < step_count; ++i)
	{
		g_context[i] = never_ran;
		g_stack[i] = 0;
	}
}
/** @brief Whether every step from @p from onwards ran in handler mode. */
bool ran_in_handler_from(int from)
{
	for (int i = from; i < step_count; ++i)
		if (g_context[i] == 0 || g_context[i] == never_ran)
			return false;
	return true;
}
} // namespace
OPSY_QEMU_TEST(a_routine_advances_one_step_per_interrupt)
{
	reset_state();
	CHECK(install_handler());
	g_first = driven(g_storage, &g_step);
	CHECK(static_cast<bool>(g_first));
	CHECK(g_step == 0);                       // created, not started
	// Started from task context, exactly as a driver's begin() would.
	g_first.resume();
	CHECK(g_step == 1);
	CHECK(g_context[0] == 0);                 // that first step ran on a task
	for (int i = 2; i <= step_count; ++i)
	{
		fire();
		CHECK(g_step == i);
	}
	// Every step after the first was reached by the exception, not by this
	// task: the routine really is driven by the interrupt.
	CHECK(ran_in_handler_from(1));
	// One more interrupt to finish. After its last step the routine is
	// suspended on the co_await that ends the loop, not done -- a routine
	// takes one more resumption than it has steps, and a driver that stops
	// resuming at the last step leaves it forever suspended.
	CHECK(!g_first.done());
	fire();
	CHECK(g_first.done());
}
OPSY_QEMU_TEST(a_routine_driven_by_interrupts_stays_finished)
{
	reset_state();
	CHECK(install_handler());
	g_first = driven(g_storage, &g_step);
	g_first.resume();
	for (int i = 0; i < step_count + 5; ++i)
		fire();
	// The handler went on calling resume() long after the routine ended,
	// which is what lets a driver's handler be one unconditional line.
	CHECK(g_step == step_count);
	CHECK(g_first.done());
}
OPSY_QEMU_TEST(the_scheduler_keeps_running_while_a_routine_is_driven)
{
	reset_state();
	CHECK(install_handler());
	const auto started = opsy::scheduler::now();
	g_first = driven(g_storage, &g_step);
	g_first.resume();
	for (int i = 1; i < step_count; ++i)
		fire();
	CHECK(g_step == step_count);
	// A routine is not a task and never enters the scheduler: time kept
	// advancing, and this task was resumed normally throughout -- which
	// reaching this line is itself the proof of.
	CHECK(opsy::scheduler::now() > started);
}
OPSY_QEMU_TEST(two_routines_on_one_interrupt_advance_independently)
{
	reset_state();
	CHECK(install_handler());
	std::atomic<int> second_step = 0;
	g_first = driven(g_storage, &g_step);
	g_second = driven(g_other_storage, &second_step);
	CHECK(static_cast<bool>(g_first));
	CHECK(static_cast<bool>(g_second));
	g_first.resume();
	CHECK(g_step == 1);
	CHECK(second_step == 0);                  // untouched by the first starting
	// From here the handler resumes both on every interrupt, each in its own
	// storage and at its own point in the sequence.
	fire();
	CHECK(g_step == 2);
	CHECK(second_step == 1);
	fire();
	CHECK(g_step == 3);
	CHECK(second_step == 2);
	// Torn down here: g_second refers to second_step, a local about to go out
	// of scope, and a later case firing the IRQ would resume it.
	g_second = opsy::utility::routine{};
}
// A routine has no stack of its own. Its frame lives in the storage it was
// given, and resume() is an ordinary call running on whoever's stack made it
// -- a task's process stack, or the main stack an exception handler runs on.
// So alternating between the two is not a case to be supported; there is
// nothing in a routine that could notice.
//
// Worth pinning down all the same, because it is the sharpest difference from
// giving each routine a stack, where suspending on one stack and resuming on
// another is precisely what cannot be done.
OPSY_QEMU_TEST(a_routine_can_be_resumed_from_a_task_and_from_an_interrupt_in_turn)
{
	reset_state();
	CHECK(install_handler());
	g_first = driven(g_storage, &g_step);
	g_first.resume();                         // step 1, on the task
	CHECK(g_step == 1);
	fire();                                   // step 2, in the handler
	CHECK(g_step == 2);
	g_first.resume();                         // step 3, back on the task
	CHECK(g_step == 3);
	fire();                                   // step 4, in the handler again
	CHECK(g_step == 4);
	// One more, to run off the end of the loop. See the note in
	// a_routine_advances_one_step_per_interrupt.
	CHECK(!g_first.done());
	g_first.resume();
	CHECK(g_first.done());
	// Each step ran where it was resumed from, alternating.
	CHECK(g_context[0] == 0);                 // thread mode: the task
	CHECK(g_context[1] != 0);                 // handler mode: the exception
	CHECK(g_context[1] != never_ran);
	CHECK(g_context[2] == 0);
	CHECK(g_context[3] != 0);
	CHECK(g_context[3] != never_ran);
	// And on two different stacks. A handler runs on the main stack, a task on
	// the process stack; the routine carried on across the change without
	// noticing, because none of its state was ever on either of them.
	CHECK(g_stack[0] != g_stack[1]);
	CHECK(g_stack[0] == g_stack[2]);          // both task steps, same stack
	CHECK(g_stack[1] == g_stack[3]);          // both handler steps, same stack
}
// ---------------------------------------------------------------- probe ---
namespace
{
std::atomic<int> g_probe_step = 0;
opsy::utility::routine_storage<256> g_probe_storage;
// A routine that calls into the OS. Legal only because a task is what resumes
// it: it runs on the task's stack, in thread mode, so sleep_for suspends the
// task in the middle of resume().
opsy::utility::routine sleeps_inside(opsy::utility::routine_storage<256>&)
{
	g_probe_step = 1;
	opsy::sleep_for(5ms);
	g_probe_step = 2;
	co_await std::suspend_always{};
	g_probe_step = 3;
	opsy::sleep_for(5ms);
	g_probe_step = 4;
}
}
OPSY_QEMU_TEST(probe_a_routine_resumed_by_a_task_may_call_the_os)
{
	g_probe_step = 0;
	const auto started = opsy::scheduler::now();
	auto r = sleeps_inside(g_probe_storage);
	r.resume();
	CHECK(g_probe_step == 2);
	CHECK(opsy::scheduler::now() >= started + 5ms);
	r.resume();
	CHECK(g_probe_step == 4);
	CHECK(opsy::scheduler::now() >= started + 10ms);
}
