/**
 ******************************************************************************
 * @file    qemu_test.hpp
 * @brief   Test harness for the on-target (QEMU) OpSy suite.
 *
 *          Unlike the host suite, these cases cannot run from @c main : the
 *          behaviour under test only exists once the scheduler owns the CPU.
 *          Every case therefore runs from inside an OpSy task, and may block,
 *          sleep, and start or stop other tasks.
 *
 *          Cases run one after another on the same runner task, so each one
 *          must leave the system as it found it — no task left started, no
 *          mutex left locked.
 *
 * @warning Do not use @c assert : OpSy installs its own, which routes to
 *          @c opsy::trap . Use @ref CHECK.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

namespace opsy { class idle_task_control_block; }

/**
 * @brief Entry point of the test image.
 *
 *        Deliberately not called @c main : ISO C++ forbids naming @c ::main
 *        from the program, and -Wpedantic enforces it. A freestanding image
 *        has no reason to insist on the name anyway.
 */
int opsy_test_main();

namespace qemu_test
{

using test_fn = void (*)();

/** @brief One registered case; constructing it links it into the run list. */
struct test_case
{
	test_case(const char* case_name, test_fn body);

	const char* name;
	test_fn     run;
	test_case*  next;
};

test_case*& registry();

void report_failure(const char* file, int line, const char* expression);

/** @brief Runs every case, then ends the emulation with the right exit code. */
[[noreturn]] void run_all_and_exit();

/** @brief Returned by @ref find_usable_irq when the NVIC accepts none. */
inline constexpr unsigned no_irq = 0xFFFFFFFFu;

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
 * @return An IRQ the NVIC accepts, or @ref no_irq
 */
unsigned find_usable_irq();

/** @brief The idle task the suite starts the scheduler with. */
opsy::idle_task_control_block& idle();

} // namespace qemu_test

#define OPSY_QEMU_TEST(name)                                                   \
	static void name();                                                        \
	static ::qemu_test::test_case name##_registration{#name, &name};           \
	static void name()

#define CHECK(expression)                                                      \
	do                                                                         \
	{                                                                          \
		if (!(expression))                                                     \
			::qemu_test::report_failure(__FILE__, __LINE__, #expression);      \
	} while (false)
