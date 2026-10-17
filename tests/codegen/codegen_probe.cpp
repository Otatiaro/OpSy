/**
 ******************************************************************************
 * @file    codegen_probe.cpp
 * @brief   Probes whose *generated code* is the assertion.
 *
 *          A class of OpSy bug only exists once the optimiser is allowed to
 *          work: a memory-mapped register read through a non-volatile pointer
 *          can be cached in a register or folded with an identical read, and a
 *          store to one can be dropped as dead. Neither the compile-only suite
 *          (built without @c -O ) nor the QEMU suite catches that — emulation
 *          only shows it if the compiler happened to take the dangerous
 *          transformation on that build.
 *
 *          So this file is compiled at @c -O2 and its disassembly is checked
 *          by @c check_codegen.cmake . Each function below is @c noinline and
 *          @c used so it survives as a distinct symbol to inspect.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include <cortex_m.hpp>

#include <cstdint>

extern "C"
{

// Extern so check_codegen.py finds them in the object by name.
uint32_t probe_two_reads_stay_two();
uint32_t probe_spin_on_register();
void probe_stores_are_not_eliminated();
uint32_t probe_strex_then_read(uint32_t* word);
void probe_pend_sv_is_synchronised();
void probe_disable_interrupt_is_synchronised();
void probe_control_write_is_synchronised(uint32_t control);
void probe_vtor_move_is_synchronised(opsy::cortex_m::isr_handler_t* table);

/**
 * @brief Reads the same MMIO register twice and returns both halves.
 *
 *        SysTick's current-value register changes on its own, so the two reads
 *        must both happen. With a non-volatile access the optimiser is entitled
 *        to fold them into one — which is what made a delay loop built on
 *        systick_count() never terminate.
 *
 *        Expected: two loads of the register.
 */
[[gnu::noinline, gnu::used]] uint32_t probe_two_reads_stay_two()
{
	const uint32_t first = opsy::cortex_m::systick_count();
	const uint32_t second = opsy::cortex_m::systick_count();
	return (first << 16) ^ second;
}

/**
 * @brief Reads the same MMIO register in a loop until it changes.
 *
 *        The canonical spin. If the read is hoisted out, the loop never ends.
 *        Expected: the load sits inside the loop body, not before it.
 */
[[gnu::noinline, gnu::used]] uint32_t probe_spin_on_register()
{
	const uint32_t start = opsy::cortex_m::systick_count();
	uint32_t current = start;

	while (current == start)
		current = opsy::cortex_m::systick_count();

	return current;
}

/**
 * @brief Programs SysTick, which writes the control register twice.
 *
 *        enable_systick stops the timer, reprograms LOAD and VAL, then starts
 *        it again. The first store has no visible effect on any C++ object, so
 *        a non-volatile access lets it be eliminated as a dead store — leaving
 *        the timer running while its reload value is rewritten underneath it.
 *
 *        Expected: at least four stores (CTRL off, LOAD, VAL, CTRL on).
 */
[[gnu::noinline, gnu::used]] void probe_stores_are_not_eliminated()
{
	opsy::cortex_m::enable_systick(1000);
}

/**
 * @brief Reads a word back after a store-exclusive to it.
 *
 *        The store-exclusive writes memory if it succeeds. Told nothing, the
 *        compiler answers the read with the value stored just before; told it
 *        always writes, it drops that store, which a failed strex leaves as
 *        the word's value.
 *
 *        Expected: the store, the strex, then a load.
 */
[[gnu::noinline, gnu::used]] uint32_t probe_strex_then_read(uint32_t* word)
{
	*word = 5;
	(void) opsy::cortex_m::store_exclusive(word, uint32_t{7});
	return *word;
}

/**
 * @brief Pends PendSV.
 *
 *        The caller relies on the switch happening before its next instruction
 *        once the mask drops -- a task that blocked on a mutex must not run on
 *        into the section it does not own. The architecture only guarantees
 *        that after a dsb, and an isb for the instructions that follow.
 *
 *        Expected: the store, then dsb, then isb.
 */
[[gnu::noinline, gnu::used]] void probe_pend_sv_is_synchronised()
{
	opsy::cortex_m::trigger_pend_sv();
}

/**
 * @brief Disables an interrupt in the NVIC.
 *
 *        Code that disables an interrupt then touches what its handler
 *        touches assumes the handler can no longer run. Without a dsb and an
 *        isb it can, for a few instructions.
 *
 *        Expected: the store, then dsb, then isb.
 */
[[gnu::noinline, gnu::used]] void probe_disable_interrupt_is_synchronised()
{
	opsy::cortex_m::disable_interrupt(27);
}

/**
 * @brief Writes CONTROL.
 *
 *        Switching the stack pointer or the privilege level takes effect for
 *        the instructions after an isb, not before.
 *
 *        Expected: msr control, then isb.
 */
[[gnu::noinline, gnu::used]] void probe_control_write_is_synchronised(uint32_t control)
{
	opsy::cortex_m::set_control(control);
}

/**
 * @brief Moves the vector table.
 *
 *        An exception taken right after the VTOR write may still be vectored
 *        through the old table unless a dsb completes the write first.
 *
 *        Expected: the store, then dsb.
 */
[[gnu::noinline, gnu::used]] void probe_vtor_move_is_synchronised(opsy::cortex_m::isr_handler_t* table)
{
	opsy::cortex_m::move_vtor(table);
}

} // extern "C"
