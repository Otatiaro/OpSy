/**
 ******************************************************************************
 * @file    test_start_check.cpp
 * @brief   A separate image: scheduler::start on a vector table that will not
 *          take its handlers must stop, in every build.
 *
 *          scheduler::start installs SVC, PendSV and SysTick through VTOR and
 *          reads the three entries back. A table a store does not change -- one
 *          in flash, as a generated table or a vendor startup file leaves it --
 *          keeps the handlers it had, and a scheduler that went on regardless
 *          would take its first tick or service call somewhere else entirely.
 *          The read-back is not an assert, so it holds with NDEBUG too; the
 *          QEMU jobs build this image at every optimisation level the suite
 *          runs at, NDEBUG included, which is what shows it.
 *
 *          The boards have no flash to put a table in: their code memory is
 *          writable RAM. A read-only table is emulated instead. The MPU makes
 *          the table read-only, and MemManage skips the faulting store, so the
 *          write goes nowhere -- as a store to flash does -- and execution goes
 *          on. The table has every handler but SVC_Handler, the last of the
 *          three start checks, so all three comparisons run.
 *
 *          Passing means reaching HardFault from a trap instruction, still on
 *          the main stack -- before start switches thread mode to the process
 *          stack -- with at least one store skipped and the SVC entry still not
 *          OpSy's. Reaching anything else is a failure: a task running means
 *          start went on, and a trap after the switch is some other check.
 *
 *          Its own image because it never returns: the suite in the main image
 *          needs a scheduler that started.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"
#include "semihosting.hpp"

#include <opsy.hpp>

#include <cstddef>
#include <cstdint>

extern "C"
{
extern uint32_t _estack;
void Reset_Handler();
void Default_Handler();

// SysTick_Handler, PendSV_Handler and SVC_Handler are declared by <opsy.hpp>.

void check_mem_manage_handler();
void check_hard_fault_handler();
void check_skip_store(uint32_t* frame);
void check_hard_fault(uint32_t* frame, uint32_t exc_return);
}

namespace
{

using handler_t = void (*)();

/** @brief The table start runs on: aligned for VTOR, and for one MPU region over its first 64 entries. */
alignas(512) handler_t g_table[16 + 64];

constexpr std::size_t protected_bytes = 64 * sizeof(handler_t);

volatile uint32_t g_skipped_stores = 0;

opsy::task<512> g_task;
opsy::idle_task<256> g_idle;

volatile uint32_t& reg(uint32_t address)
{
	return *reinterpret_cast<volatile uint32_t*>(address);
}

constexpr uint32_t shcsr = 0xE000ED24u;
constexpr uint32_t cfsr = 0xE000ED28u;
constexpr uint32_t vtor = 0xE000ED08u;
constexpr uint32_t mpu_ctrl = 0xE000ED94u;
constexpr uint32_t mpu_rnr = 0xE000ED98u;
constexpr uint32_t mpu_rbar = 0xE000ED9Cu;
constexpr uint32_t mpu_rasr_rlar = 0xE000EDA0u;
constexpr uint32_t mpu_mair0 = 0xE000EDC0u;

[[noreturn]] void fail(const char* why)
{
	qemu::write("*** ");
	qemu::write(why);
	qemu::write(" ***\nRESULT: FAIL\n");
	qemu::exit(1);
}

[[noreturn]] void unexpected_exception()
{
	fail("an exception the table does not expect was taken");
}

/** @brief Region 0: the table, read-only to privileged code, no access otherwise. */
void protect_table()
{
	const auto base = reinterpret_cast<uint32_t>(&g_table[0]);

	reg(mpu_rnr) = 0;
#if defined(__ARM_ARCH_8M_MAIN__)
	// PMSAv8: attribute 0 is normal memory, non-cacheable. RBAR holds the base,
	// AP = 0b10 (read-only, privileged only), XN; RLAR the inclusive limit,
	// attribute index 0, enabled.
	reg(mpu_mair0) = 0x44u;
	reg(mpu_rbar) = base | (0b10u << 1) | 1u;
	reg(mpu_rasr_rlar) = ((base + protected_bytes - 1) & ~0x1Fu) | 1u;
#else
	// PMSAv7: RBAR holds the base (aligned to the size), RASR XN, AP = 0b101
	// (read-only, privileged only), TEX = 0b001 (normal, non-cacheable), the
	// size as log2(bytes) - 1, enabled.
	static_assert(protected_bytes == 256);
	(void) mpu_mair0;
	reg(mpu_rbar) = base;
	reg(mpu_rasr_rlar) = (1u << 28) | (0b101u << 24) | (0b001u << 19) | ((8u - 1u) << 1) | 1u;
#endif

	// PRIVDEFENA: everything outside the region keeps the default map.
	reg(mpu_ctrl) = (1u << 2) | 1u;
	reg(shcsr) |= 1u << 16; // MEMFAULTENA, or the fault escalates to HardFault
	asm volatile("dsb\n\tisb" ::: "memory");
}

} // namespace

/**
 * @brief Entry of both fault handlers: the frame the core stacked, from
 *        whichever stack it used, and EXC_RETURN untouched in lr, so the C++
 *        handler returns from the exception as it returns.
 */
extern "C" [[gnu::naked]] void check_mem_manage_handler()
{
	asm volatile(
			"tst lr, #4\n\t"
			"ite eq\n\t"
			"mrseq r0, msp\n\t"
			"mrsne r0, psp\n\t"
			"b check_skip_store");
}

extern "C" [[gnu::naked]] void check_hard_fault_handler()
{
	asm volatile(
			"tst lr, #4\n\t"
			"ite eq\n\t"
			"mrseq r0, msp\n\t"
			"mrsne r0, psp\n\t"
			"mov r1, lr\n\t"
			"b check_hard_fault");
}

/** @brief Steps over the store the MPU refused: the write goes nowhere, as it would to flash. */
extern "C" [[gnu::used]] void check_skip_store(uint32_t* frame)
{
	const auto pc = frame[6];
	const auto first = *reinterpret_cast<const volatile uint16_t*>(pc);

	// Thumb-2: a first halfword from 0b11101 up opens a 32-bit instruction.
	frame[6] = pc + ((first & 0xF800u) >= 0xE800u ? 4u : 2u);

	reg(cfsr) = reg(cfsr) & 0xFFu; // MMFSR, write-one-to-clear
	g_skipped_stores = g_skipped_stores + 1;
}

extern "C" [[gnu::used]] void check_hard_fault(uint32_t* frame, uint32_t exc_return)
{
	const auto opcode = *reinterpret_cast<const volatile uint16_t*>(frame[6]);
	const bool trap = (opcode & 0xFF00u) == 0xDE00u   // udf: __builtin_trap
	               || (opcode & 0xFF00u) == 0xBE00u;  // bkpt: a trap() that halts a debugger

	if (!trap)
		fail("a fault, not a trap");
	if ((exc_return & 4u) != 0)
		fail("a trap after start switched to the process stack: not the vector table check");
	if (g_skipped_stores == 0)
		fail("no store to the table was refused, so the table was never read-only");
	if (g_table[11] == &SVC_Handler)
		fail("SVC_Handler reached the read-only table");

	qemu::write("start stopped on a vector table without SVC_Handler\nRESULT: PASS\n");
	qemu::exit(0);
}

int opsy_test_main()
{
	qemu::write("OpSy start check: a vector table that does not take the handlers\n\n");

	g_table[0] = reinterpret_cast<handler_t>(&_estack);
	g_table[1] = &Reset_Handler;
	for (std::size_t i = 2; i < sizeof(g_table) / sizeof(g_table[0]); ++i)
		g_table[i] = &unexpected_exception;

	g_table[3] = &check_hard_fault_handler;
	g_table[4] = &check_mem_manage_handler;
	g_table[14] = &PendSV_Handler;
	g_table[15] = &SysTick_Handler;
	// g_table[11] stays unexpected_exception: SVC_Handler is what start cannot install.

	reg(vtor) = reinterpret_cast<uint32_t>(&g_table[0]);
	asm volatile("dsb\n\tisb" ::: "memory");

	protect_table();

	(void) g_task.start([]
	{
		fail("a task ran: start went on with a table it could not write");
	}, "should-not-run");

	opsy::scheduler::start(g_idle);
}
