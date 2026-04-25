#include <mutex>
#include <cassert>

#include "ConditionVariable.hpp"
#include "Scheduler.hpp"
#include "Hooks.hpp"

namespace opsy
{

void ConditionVariable::notify_one()
{
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= CortexM::currentPriority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>()); // do not call notify when priority is higher than the mutex priority !
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	{
		std::lock_guard<Mutex> lock(m_mutex);

		Hooks::conditionVariableNotifyOne(*this);

		if (m_waitingList.empty())
			return;
		else
		{
			TaskControlBlock& task = m_waitingList.front();
			m_waitingList.pop_front();
			Scheduler::wakeUp(task, *this);
		}
	}
}

void ConditionVariable::notify_all()
{
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= CortexM::currentPriority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>()); // do not call notify when priority is higher than the mutex priority !
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	{
		std::lock_guard<Mutex> lock(m_mutex);

		Hooks::conditionVariableNotifyAll(*this);

		while (!m_waitingList.empty())
		{
			TaskControlBlock& task = m_waitingList.front();
			m_waitingList.pop_front();
			Scheduler::wakeUp(task, *this);
		}
	}

}

void ConditionVariable::wait()
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, #-1 \n\t"
			"mov r2, #0 \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this)
			: "r0", "r1", "r2");
}

void ConditionVariable::wait(Mutex& mutex)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, #-1 \n\t"
			"mov r2, %[mutex] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this), [mutex] "r" (&mutex)
			: "r0", "r1", "r2");
}

std::cv_status ConditionVariable::wait_for(duration timeout)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	uint32_t result;
	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, %[duration] \n\t"
			"mov r2, #0 \n\t"
			"svc %[immediate] \n\t"
			"mov %[result], r0"
			: [result] "=r" (result)
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this), [duration] "r" (timeout.count())
			: "r0", "r1", "r2");

	assert(result == 0 || result == 1); // result can only be 0 or 1 (timeout or notimeout)
	return static_cast<std::cv_status>(result);
}

std::cv_status ConditionVariable::wait_for(Mutex& mutex, duration timeout)
{
	assert(CortexM::ipsr() == 0); // cannot call in interrupt
	assert(m_mutex.priority().value_or(Scheduler::kServiceCallPriority).maskedValue<kPreemptionBits>() >= Scheduler::kServiceCallPriority.maskedValue<kPreemptionBits>()); // mutex priority can't be higher than service call

	uint32_t result;

	asm volatile(
			"mov r0, %[this_ptr] \n\t"
			"mov r1, %[duration] \n\t"
			"mov r2, %[mutex] \n\t"
			"svc %[immediate] \n\t"
			"mov %[result], r0"
			: [result] "=r" (result)
			: [immediate] "I" (Scheduler::ServiceCallNumber::Wait), [this_ptr] "r" (this), [duration] "r" (timeout.count()), [mutex] "r" (&mutex)
			: "r0", "r1", "r2");

	assert(result == 0 || result == 1); // result can only be 0 or 1 (timeout or notimeout)

	return static_cast<std::cv_status>(result);
}

std::cv_status ConditionVariable::wait_until(time_point timeout_time)
{
	return wait_for(timeout_time - Scheduler::now());
}

std::cv_status ConditionVariable::wait_until(Mutex& mutex, time_point timeout_time)
{
	return wait_for(mutex, timeout_time - Scheduler::now());
}

void ConditionVariable::addWaiting(TaskControlBlock& task)
{
	m_waitingList.insertWhen(TaskControlBlock::priorityIsLower, task);
}

void ConditionVariable::removeWaiting(TaskControlBlock& task)
{
	m_waitingList.erase(task);
}

}
