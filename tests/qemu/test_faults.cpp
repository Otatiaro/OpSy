/**
 ******************************************************************************
 * @file    test_faults.cpp
 * @brief   A separate image: one fault after another, each reaching the
 *          application's handler through opsy::fault_isrs with the status
 *          decoded as the architecture says, and execution resumed past it.
 *
 *          The table is built the way a user builds it -- with(scheduler::isrs)
 *          then with(fault_isrs<on_fault>) -- and the faults are raised from a
 *          task, so the frame is on the process stack. In order:
 *
 *          - an undefined instruction with the configurable faults still
 *            disabled, as out of reset: taken as HardFault, @c escalated ;
 *          - the same after enable_configurable_faults: UsageFault, not escalated;
 *          - a divide by zero, with its trap enabled;
 *          - a store the MPU refuses: MemManage, with the address;
 *          - a load from an address nothing answers: a precise BusFault;
 *          - on ARMv8-M, the process stack pointer taken below PSPLIM: STKOF.
 *            Last, because the stack pointer is not to be trusted after it,
 *            so the handler ends the run from there rather than return.
 *
 *          Its own image because it owns the vector table and the fault
 *          configuration, and because the last case does not come back.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "mpu.hpp"
#include "qemu_test.hpp"
#include "semihosting.hpp"
#include "thumb.hpp"

#include <opsy.hpp>

#include <cstddef>
#include <cstdint>

extern "C"
{
extern uint32_t _estack;
void Reset_Handler();
}

namespace
{

using handler_t = void (*)();

/** @brief The shape SvdGenerator generates, enough of it for with(): the exceptions by name, and a slot filled per call. */
enum class exception : uint8_t { nmi = 2, hard_fault = 3, mem_manage = 4, bus_fault = 5, usage_fault = 6, svcall = 11, pendsv = 14, systick = 15 };

[[noreturn]] void unexpected_exception();

/**
 * @brief The initial stack pointer, then the handlers by slot. Built at compile
 *        time, as a user builds it: a runtime with() chain copies this
 *        512-byte object once per call, on the stack.
 */
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
	constexpr auto with() const -> vector_table
	{
		auto copy = *this;
		copy.handlers[static_cast<std::size_t>(Exception) - 1] = Handler;
		return copy;
	}

	constexpr auto with(auto&& install) const { return install(*this); }

	uint32_t* initial_sp;
	handler_t handlers[15 + 64];
};

static_assert(offsetof(vector_table, handlers) == sizeof(uint32_t*), "slot 1, the reset handler, follows the initial stack pointer");

/** @brief What the handler saw last, read by the task once the fault is over. */
struct record
{
	opsy::fault_status status;
	uint32_t pc;
	uint32_t exc_return;
};

record g_last;
volatile uint32_t g_faults = 0;

/** @brief 256 bytes the MPU will make read-only; aligned to its size, as PMSAv7 needs. */
alignas(256) volatile uint32_t g_guarded[64];

opsy::task<1024> g_task;
opsy::idle_task<256> g_idle;

constexpr uint32_t vtor = 0xE000ED08u;

/** @brief Device space nothing answers on the boards this runs on; a load from it is a precise BusFault. */
constexpr uint32_t unmapped = 0xF0000000u;

[[noreturn]] void fail(const char* why)
{
	qemu::write("*** ");
	qemu::write(why);
	qemu::write(" ***\nRESULT: FAIL\n");
	qemu::exit(1);
}

[[noreturn]] void pass()
{
	qemu::write("every fault reached the handler, decoded, and was stepped over\nRESULT: PASS\n");
	qemu::exit(0);
}

void expect(bool condition, const char* what)
{
	if (!condition)
		fail(what);
}

[[noreturn]] void unexpected_exception()
{
	fail("an exception the table does not expect was taken");
}

/** @brief hook::fault ran once for this fault, before the handler, and saw what the handler is given. */
void expect_hooked(const opsy::fault_status& status, const opsy::exception_frame& frame, uint32_t exc_return)
{
	using seen = opsy::fault_seen;
	expect(seen::count == g_faults + 1, "hook::fault did not run once before the handler");
	expect(seen::frame == &frame, "hook::fault was given another frame");
	expect(seen::exc_return == exc_return, "hook::fault was given another EXC_RETURN");
	expect(seen::status.cfsr == status.cfsr && seen::status.hfsr == status.hfsr, "hook::fault was given another status");
}

/** @brief The application's handler: records, steps over the faulting instruction, clears the status, returns. */
void on_fault(const opsy::fault_status& status, opsy::exception_frame& frame, uint32_t exc_return)
{
	expect_hooked(status, frame, exc_return);

	// The last case: the stack pointer is below its limit, and no more pushes
	// may happen on it. The run ends here.
	if (status.stack_overflow())
		pass();

	g_last = {status, frame.pc, exc_return};
	g_faults = g_faults + 1;

	frame.pc += qemu_test::thumb_instruction_length(frame.pc);
	opsy::cortex_m::clear_fault_status();
}

constexpr auto g_table = vector_table(&_estack, Reset_Handler)
	.with(opsy::scheduler::isrs)
	.with(opsy::fault_isrs<on_fault>);

/** @brief Runs @p raise and checks it faulted exactly once. */
template<typename Raise>
void one_fault(Raise raise, const char* name)
{
	const uint32_t before = g_faults;
	raise();
	expect(g_faults == before + 1, name);
	qemu::write("  ok: ");
	qemu::write(name);
	qemu::write("\n");
}

void undefined_instruction()
{
	asm volatile("udf #0" ::: "memory");
}

void divide_by_zero()
{
	asm volatile(
			"movs r0, #1 \n\t"
			"movs r1, #0 \n\t"
			"sdiv r0, r0, r1"
			::: "r0", "r1", "memory");
}

void store_to_read_only()
{
	g_guarded[3] = 1;
}

void load_from_unmapped()
{
	(void) *reinterpret_cast<volatile uint32_t*>(unmapped);
}

#if defined(__ARM_ARCH_8M_MAIN__)
/** @brief Takes PSP below PSPLIM. The fault's own frame has room above the limit; what follows never runs. */
[[noreturn]] void overflow_the_stack()
{
	asm volatile(
			"mrs r0, psplim \n\t"
			"add r0, r0, #256 \n\t"
			"mov sp, r0 \n\t"
			"sub sp, sp, #512 \n\t"
			"push {r0, r1}"
			::: "r0", "memory");
	fail("the stack pointer went below PSPLIM and nothing happened");
}
#endif

void run_cases()
{
	qemu::write("configurable faults disabled, as out of reset\n");

	one_fault(undefined_instruction, "undefined instruction, escalated to HardFault");
	expect(g_last.status.escalated(), "HFSR.FORCED is not set");
	expect(g_last.status.undefined_instruction(), "UFSR.UNDEFINSTR is not set");
	expect((g_last.exc_return & opsy::cortex_m::exc_return_psp_flag) != 0, "the frame is not on the process stack");
	expect((g_last.exc_return & opsy::cortex_m::exc_return_thread_flag) != 0, "the fault was not taken from thread mode");

	opsy::cortex_m::enable_configurable_faults();
	qemu::write("configurable faults enabled\n");

	one_fault(undefined_instruction, "undefined instruction, as UsageFault");
	expect(!g_last.status.escalated(), "HFSR.FORCED is set for a UsageFault");
	expect(g_last.status.undefined_instruction(), "UFSR.UNDEFINSTR is not set");
	expect(!g_last.status.divide_by_zero() && !g_last.status.data_access_violation(), "bits of another fault are set");

	opsy::cortex_m::enable_divide_by_zero_trap();
	one_fault(divide_by_zero, "divide by zero");
	expect(g_last.status.divide_by_zero(), "UFSR.DIVBYZERO is not set");
	expect(!g_last.status.undefined_instruction(), "UFSR.UNDEFINSTR is still set: the status was not cleared");

	qemu_test::mpu::make_read_only(reinterpret_cast<uint32_t>(&g_guarded[0]), sizeof(g_guarded));
	one_fault(store_to_read_only, "store to memory the MPU protects");
	expect(g_last.status.data_access_violation(), "MMFSR.DACCVIOL is not set");
	expect(g_last.status.memory_fault_address() == reinterpret_cast<uint32_t>(&g_guarded[3]), "MMFAR is not the address stored to");
	expect(!g_last.status.bus_fault_address().has_value(), "BFARVALID is set on a MemManage");
	expect(g_guarded[3] == 0, "the store the MPU refused went through");

	one_fault(load_from_unmapped, "load from an address nothing answers");
	expect(g_last.status.precise_bus_fault(), "BFSR.PRECISERR is not set");
	expect(g_last.status.bus_fault_address() == unmapped, "BFAR is not the address loaded from");
	expect(!g_last.status.memory_fault_address().has_value(), "MMARVALID is set on a BusFault");

#if defined(__ARM_ARCH_8M_MAIN__)
	qemu::write("  stack limit: taking PSP below PSPLIM, the handler ends the run\n");
	overflow_the_stack();
#else
	pass();
#endif
}

} // namespace

int opsy_test_main()
{
	qemu::write("OpSy faults: every fault through one handler, decoded and stepped over\n\n");

	*reinterpret_cast<volatile uint32_t*>(vtor) = reinterpret_cast<uint32_t>(&g_table);
	asm volatile("dsb\n\tisb" ::: "memory");

	(void) g_task.start([] { run_cases(); });

	opsy::scheduler::start(g_idle);
}
