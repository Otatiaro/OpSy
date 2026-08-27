/**
 ******************************************************************************
 * @file    startup.cpp
 * @brief   Reset path for the QEMU test images: C++ runtime bring-up, vector
 *          table relocation, and the handful of symbols OpSy expects the
 *          platform to provide.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "semihosting.hpp"

#include <cstddef>
#include <cstdint>

extern "C"
{

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

void Reset_Handler();
void Default_Handler();

/** @brief newlib: runs everything in .init_array (static constructors). */
void __libc_init_array();

// Defined by OpSy in scheduler.cpp; scheduler::start_impl installs them
// through set_isr_handler, but the table has to name them from reset so a
// fault before that point still lands somewhere deliberate.
void SysTick_Handler();
void PendSV_Handler();
void SVC_Handler();

/**
 * @brief OpSy reads this through @c opsy::core_clock() to program SysTick.
 *
 *        QEMU does not model a real clock frequency — SysTick counts emulated
 *        cycles — so the only thing that matters is that the value divides
 *        evenly by 1000, which start_impl asserts when converting to the 1 ms
 *        tick. 25 MHz is the MPS2's nominal rate.
 */
uint32_t SystemCoreClock = 25'000'000u;

} // extern "C"

/**
 * @brief Entry point of the test image.
 *
 *        Deliberately not called @c main : ISO C++ forbids naming @c ::main
 *        from the program, and -Wpedantic enforces it. A freestanding image
 *        has no reason to insist on the name anyway.
 */
int opsy_test_main();

namespace
{

using handler_t = void (*)();

/**
 * @brief The live vector table, in RAM.
 *
 *        Relocated out of the boot table for two reasons. The Secure code
 *        region of mps2-an505 is not writable, and scheduler::start_impl
 *        installs its SysTick / PendSV / SVC handlers by writing through
 *        VTOR — so on that board a table left in flash would silently fail to
 *        take, or fault. Alignment must cover the table size rounded up to a
 *        power of two, and 128 entries * 4 bytes is the ARMv7-M minimum.
 */
alignas(512) handler_t g_vectors[16 + 64];

[[noreturn]] void fault_handler()
{
	qemu::write("\n*** fault: unhandled exception ***\n");
	qemu::exit(1);
}

} // namespace

/**
 * @brief The ARM EHABI personality routine, defined so nothing has to provide
 *        a real one
 *
 *        Clang emits an .ARM.exidx entry for every coroutine even under
 *        -fno-exceptions, and each entry names this routine. The linker script
 *        discards those tables -- there are no exceptions here, so they are
 *        dead weight -- but the relocation naming this symbol survives the
 *        discard, and the link fails on it. Pulling in the real one would
 *        bring the unwinder, which wants stderr, which a bare-metal image has
 *        not got.
 *
 *        It is never called: reaching it would mean an exception was being
 *        propagated, in a build compiled without them. Trapping says so.
 *
 * @remark Only clang needs it, and only without optimisation; GCC emits no
 *         such reference. Defined unconditionally all the same, since a
 *         definition nobody references costs nothing and a build that needs
 *         it and has not got it fails at link time with a message that says
 *         nothing about coroutines.
 */
extern "C" void __aeabi_unwind_cpp_pr0()
{
	fault_handler();
}

extern "C" void Default_Handler()
{
	fault_handler();
}

/** @brief The boot table: only the first two entries are read by the core. */
__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[] = {
	&_estack,
	reinterpret_cast<const void*>(&Reset_Handler),
	reinterpret_cast<const void*>(&Default_Handler),   // NMI
	reinterpret_cast<const void*>(&Default_Handler),   // HardFault
	reinterpret_cast<const void*>(&Default_Handler),   // MemManage
	reinterpret_cast<const void*>(&Default_Handler),   // BusFault
	reinterpret_cast<const void*>(&Default_Handler),   // UsageFault
	nullptr, nullptr, nullptr, nullptr,
	reinterpret_cast<const void*>(&SVC_Handler),
	reinterpret_cast<const void*>(&Default_Handler),   // DebugMon
	nullptr,
	reinterpret_cast<const void*>(&PendSV_Handler),
	reinterpret_cast<const void*>(&SysTick_Handler),
};

extern "C" void Reset_Handler()
{
	// .data from its load address in code memory, then .bss to zero. Neither
	// the compiler nor QEMU does this for us.
	const uint32_t* source = &_sidata;
	for (uint32_t* destination = &_sdata; destination < &_edata; ++destination, ++source)
		*destination = *source;

	for (uint32_t* destination = &_sbss; destination < &_ebss; ++destination)
		*destination = 0;

	// Relocate the vector table into RAM and point VTOR at it. The first
	// entry stays the initial MSP: OpSy reads it back through
	// cortex_m::msp_at_reset() when it hands the main stack to the handlers.
	g_vectors[0] = reinterpret_cast<handler_t>(&_estack);
	g_vectors[1] = &Reset_Handler;
	for (std::size_t i = 2; i < sizeof(g_vectors) / sizeof(g_vectors[0]); ++i)
		g_vectors[i] = &Default_Handler;

	g_vectors[11] = &SVC_Handler;
	g_vectors[14] = &PendSV_Handler;
	g_vectors[15] = &SysTick_Handler;

	*reinterpret_cast<volatile uint32_t*>(0xE000ED08u) = reinterpret_cast<uint32_t>(&g_vectors[0]);

#if defined(__ARM_FP)
	// CPACR: full access to CP10/CP11. The images are built -mfloat-abi=hard
	// on every target that has an FPU, so the first floating-point
	// instruction would otherwise take a UsageFault.
	*reinterpret_cast<volatile uint32_t*>(0xE000ED88u) |= (0xFu << 20);
	asm volatile("dsb" ::: "memory");
	asm volatile("isb" ::: "memory");
#endif

	// Static constructors. The test cases register themselves through
	// file-scope objects, so skipping this boots a suite of zero cases.
	__libc_init_array();

	opsy_test_main();

	qemu::write("\n*** main returned ***\n");
	qemu::exit(1);
}

/**
 * @brief Symbols normally supplied by the C runtime start files.
 *
 *        The link uses -nostartfiles, which drops crti/crtbegin along with
 *        these. __dso_handle is referenced by __aeabi_atexit registrations,
 *        and __libc_init_array calls _init before walking .init_array.
 */
extern "C"
{

void* __dso_handle = nullptr;

void _init() {}
void _fini() {}

} // extern "C"

/**
 * @brief Newlib syscall stubs.
 *
 *        Linking against newlib drags in a few POSIX hooks even though the
 *        images never allocate or use stdio: the semihosting path bypasses
 *        both. Defining them here is cheaper than -nostdlib, which would also
 *        drop the compiler support routines OpSy relies on (64-bit division
 *        in the chrono conversions, for one).
 *
 *        _sbrk is deliberately hostile: nothing here should ever reach the
 *        heap, and a silent success would hide that it did.
 */
extern "C"
{

void* _sbrk(int)
{
	qemu::write("\n*** _sbrk called: the image must not allocate ***\n");
	qemu::write("RESULT: FAIL\n");
	qemu::exit(1);
}

int _close(int)                       { return -1; }
int _fstat(int, void*)                { return -1; }
int _isatty(int)                      { return 0; }
int _lseek(int, int, int)             { return -1; }
int _read(int, char*, int)            { return -1; }
int _write(int, const char*, int len) { return len; }
int _getpid()                         { return 1; }
int _kill(int, int)                   { return -1; }

[[noreturn]] void _exit(int code)
{
	qemu::exit(code);
}

} // extern "C"

/**
 * @brief Pulled in by any static object with a non-trivial destructor.
 *
 *        Nothing here ever runs destructors — the image exits through
 *        semihosting — so registration is a no-op rather than a dependency on
 *        a full C++ runtime.
 */
extern "C" int __aeabi_atexit(void*, void (*)(void*), void*)
{
	return 0;
}
