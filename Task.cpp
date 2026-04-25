#include <algorithm>

#include "Task.hpp"
#include "Scheduler.hpp"

namespace opsy
{
bool TaskControlBlock::start(Callback<void(void)> && entry, const char* name)
{
	if(m_active.exchange(true)) // we put true in the boolean value, and were expecting false, so we return if exchange return true
		return false;

	m_entry = std::move(entry);
	m_name = name;

#ifndef NDEBUG
	std::fill(m_stackBase, m_stackBase + m_stackSize, Dummy);
#endif

	m_stackPointer = &m_stackBase[m_stackSize - 1]; // this pointer is reserved to stop stack trace unwinding
	m_stackBase[m_stackSize - 1] = 0; // keep this pointer to zero to stop stack trace

	m_stackPointer -= sizeof(StackFrame) / sizeof(StackItem);
	const auto frame = reinterpret_cast<StackFrame*>(m_stackPointer);

	frame->psr = 1 << 24;
	frame->pc = reinterpret_cast<CodePointer>(taskStarter);
	frame->lr = reinterpret_cast<CodePointer>(reinterpret_cast<uint32_t>(Scheduler::terminateTask) + 2); // the 2 byte offset is to skip the "nop" instruction at the beginning of terminate task. This is to make GDB happy about the link return ...
	frame->r0 = reinterpret_cast<uint32_t>(this);

	m_stackPointer -= sizeof(Context) / sizeof(StackItem);
	const auto context = reinterpret_cast<Context*>(m_stackPointer);
	context->control = 0b10;
	context->lr = 0xFFFFFFFD;

	Scheduler::addTask(*this);
	return true;
}

bool TaskControlBlock::stop()
{
	if(!isStarted()) // can only stop an active task
		return false;

	asm volatile(
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (Scheduler::ServiceCallNumber::Terminate), [task] "r" (this)
			: "r0");
	return true;
}

void TaskControlBlock::priority(Priority newPriority)
{
	if(newPriority == m_priority)
		return;
	else
		Scheduler::updatePriority(*this, newPriority);
}


void TaskControlBlock::taskStarter(TaskControlBlock* thisPtr)
{
	thisPtr->m_entry();
	Scheduler::terminateTask(thisPtr);
}
}
