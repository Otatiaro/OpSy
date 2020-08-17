#include "PriorityMutex.hpp"
#include "Scheduler.hpp"
#include "Hooks.hpp"
#include "CortexM.hpp"
#include "Config.hpp"

void opsy::PriorityMutex::lock()
{
	if (m_priority.has_value())
	{
		if (m_priority.value().value() == 0)
		{
			assert(!CortexM::isPrimask());

			Hooks::enterFullLock();
			CortexM::disableInterrupts();
		}
		else
		{
			if (CortexM::ipsr() == 0) // ask for critical section only when in task
				m_criticalSection = Scheduler::criticalSection();
			else
				assert(CortexM::currentPriority().value().maskedValue<kPreemptionBits>() >= m_priority.value().maskedValue<kPreemptionBits>()); // in interrupt, check that current priority level is lower than what is needed to lock, because if an interrupt with higher priority participate in the lock, synchronization cannot be guaranteed

			Hooks::enterPriorityLock(m_priority.value());
			m_previousLock = CortexM::setBasepri(IsrPriority(m_priority.value().maskedValue<kPreemptionBits>()));
			assert(m_previousLock.maskedValue<kPreemptionBits>() <= m_priority.value().maskedValue<kPreemptionBits>()); // a new mutex lock cannot LOWER the priority mutex (basepri)
		}
	}
	else
	{
		assert(CortexM::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		m_criticalSection = Scheduler::criticalSection();
	}

	m_locked = true;
}

void opsy::PriorityMutex::unlock()
{
	if (!m_locked)
		return;

	if (m_priority.has_value())
	{
		if (m_priority.value().value() == 0)
		{
			assert(CortexM::isPrimask());
			CortexM::enableInterrupts();
			Hooks::exitFullLock();
		}
		else
		{
#ifndef NDEBUG
			auto was = CortexM::setBasepri(m_previousLock);
			assert(was.maskedValue<kPreemptionBits>() == m_priority.value().maskedValue<kPreemptionBits>());
#else
			CortexM::setBasepri(m_previousLock);
#endif
			Hooks::exitPriorityLock();
			auto tmp = std::move(m_criticalSection);
		}
	}
	else
	{
		assert(CortexM::ipsr() == 0); // there is no reason to lock task switch from anything but a task
		auto tmp = std::move(m_criticalSection);
	}
}

uint32_t opsy::PriorityMutex::reLockFromPendSv(CriticalSection section)
{
	m_criticalSection = std::move(section); // it is a task, so critical section is mandatory

	if (m_priority.has_value())
	{
		assert(m_priority.value().value() != 0); // 0 is full mutex, can't be preempted by system
		m_previousLock = IsrPriority(0);
	}

	m_locked = true;
	return m_priority.value_or(IsrPriority(0)).maskedValue<kPreemptionBits>();
}

void opsy::PriorityMutex::releaseFromServiceCall()
{
	assert(m_locked);

	if (m_priority.has_value())
	{
		if (m_priority.value().value() == 0)
		{
			assert(CortexM::isPrimask());
			CortexM::enableInterrupts();
		}
		else
		{
#ifndef NDEBUG
			auto was = CortexM::setBasepri(m_previousLock);
			assert(was.maskedValue<kPreemptionBits>() == m_priority.value().maskedValue<kPreemptionBits>());
#else
			CortexM::setBasepri(m_previousLock);
#endif
		}
	}
}


