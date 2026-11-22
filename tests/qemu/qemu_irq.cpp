/**
 ******************************************************************************
 * @file    qemu_irq.cpp
 * @brief   The interrupt helpers of qemu_test.hpp, apart from the runner, for
 *          the images that raise an interrupt without running the suite.
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#include "qemu_test.hpp"

#include <opsy.hpp>

#include <atomic>

unsigned qemu_test::find_usable_irq()
{
	for (unsigned irq = 48; irq-- > 0;)
	{
		opsy::cortex_m::set_pending(irq);
		if (opsy::cortex_m::is_pending(irq))
		{
			opsy::cortex_m::clear_pending(irq);
			return irq;
		}
	}
	return no_irq;
}

void qemu_test::raise(unsigned irq)
{
	std::atomic_signal_fence(std::memory_order_seq_cst);
	opsy::cortex_m::set_pending(irq);
}
