#include "Scheduler.hpp"

namespace opsy
{

__attribute__((section(".bss.opsy.scheduler.isstarted"))) bool Scheduler::s_isStarted = false;
__attribute__((section(".bss.opsy.scheduler.ticks"))) opsy::time_point Scheduler::s_ticks = opsy::Startup;
__attribute__((section(".bss.opsy.scheduler.alltasks"))) EmbeddedList<TaskControlBlock, TaskLists::Handle> Scheduler::s_allTasks;
__attribute__((section(".bss.opsy.scheduler.timeouts"))) EmbeddedList<TaskControlBlock, TaskLists::Timeout> Scheduler::s_timeouts;
__attribute__((section(".bss.opsy.scheduler.ready"))) EmbeddedList<TaskControlBlock, TaskLists::Waiting> Scheduler::s_ready;
__attribute__((section(".bss.opsy.scheduler.idling"))) bool Scheduler::s_idling = false;
__attribute__((section(".bss.opsy.scheduler.mayneedswitch"))) bool Scheduler::s_mayNeedSwitch = false;
__attribute__((section(".bss.opsy.scheduler.idle"))) IdleTaskControlBlock* Scheduler::s_idle;
__attribute__((section(".bss.opsy.scheduler.previoustask"))) TaskControlBlock* Scheduler::s_previousTask = nullptr;
__attribute__((section(".bss.opsy.scheduler.currenttask"))) TaskControlBlock* Scheduler::s_currentTask = nullptr;
__attribute__((section(".bss.opsy.scheduler.nexttask"))) TaskControlBlock* Scheduler::s_nextTask = nullptr;
__attribute__((section(".bss.opsy.scheduler.criticalsection"))) volatile bool Scheduler::s_criticalSection = false;

bool __attribute__((section(".text.opsy.start"))) Scheduler::start(IdleTaskControlBlock& idle)
{
	assert((CortexM::getType() == CortexM::Type::M4) ||(CortexM::getType() == CortexM::Type::M7)); // only Cortex-M4 and M7 are officially supported so far
	assert(!s_isStarted);
	s_isStarted = true;

	uint32_t ratio = std::chrono::duration_cast<duration>(
			std::chrono::duration<int64_t>(1)).count(); // get ratio from timeout clock (system ticks to 1Hz)

	auto coreClock = getCoreClock();
	s_idle = &idle;

	CortexM::preemptBits(kPreemptionBits);
	CortexM::setPriority(CortexM::SystemIrq::ServiceCall, kServiceCallPriority); // service call is at system preemption and highest sub priority
	CortexM::setPriority(CortexM::SystemIrq::PendSV, kPendSvPriority); // pending service is at lowest possible priority
	CortexM::setPriority(CortexM::SystemIrq::Systick, kSystickPriority); // systick handler is at system preemption with lowest sub priority
	CortexM::setIsrHandler(CortexM::SystemIrq::Systick, ::SysTick_Handler);
	CortexM::setIsrHandler(CortexM::SystemIrq::PendSV, ::PendSV_Handler);
	CortexM::setIsrHandler(CortexM::SystemIrq::ServiceCall, ::SVC_Handler);

	assert(coreClock % ratio == 0u); // for exact time clock the core clock divided by ratio should not leave a remainder
	CortexM::enableSystick(coreClock / ratio);

	Hooks::starting(idle, coreClock, [](Callback<void(const TaskControlBlock&)> callback)
	{
		for(const auto& task : s_allTasks)
			callback(task);
	});

	CortexM::setPsp(CortexM::getMsp());
	CortexM::setControl(0b10);
	CortexM::setMsp(CortexM::mspAtReset());

	return doSwitch();
}

bool __attribute__((section(".text.opsy.doswitch"))) Scheduler::doSwitch()
{
	assert(s_isStarted);
	assert((s_currentTask != nullptr) || (s_criticalSection == false)); // can't have no current task in critical section

	if(s_criticalSection)
	{
		s_mayNeedSwitch = true;
		return false;
	}

	if (s_nextTask != nullptr)
	{
		assert(s_currentTask != s_nextTask);

		s_ready.insertWhen(TaskControlBlock::priorityIsLower, *s_nextTask);
		s_nextTask = nullptr;
	}

	const auto current = s_currentTask;

	if (s_currentTask != nullptr)
	{
		s_ready.insertWhen(TaskControlBlock::priorityIsLower, *s_currentTask);
		s_currentTask = nullptr;
	}

	if (s_ready.empty())
	{
		CortexM::triggerPendSv();
		return true;
	}
	else
	{
		s_nextTask = &s_ready.front();
		s_ready.pop_front();

		if (s_nextTask == current)
		{
			s_currentTask = s_nextTask;
			s_nextTask = nullptr;
			return false;
		}
		else
		{
			CortexM::triggerPendSv();
			return true;
		}
	}
}

void __attribute__((naked)) Scheduler::terminateTask(TaskControlBlock* task)
{
	asm volatile(
			"nop \n\t"
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (ServiceCallNumber::Terminate), [task] "r" (task)
			: "r0");
}





void __attribute__((section(".text.opsy.wakeup"))) Scheduler::wakeUp(TaskControlBlock& task, ConditionVariable& condition)
{
	auto previous = CortexM::setBasepri(kServiceCallPriority); // get a lock up to service call
	assert(task.m_waiting == &condition); // check the condition is the one the task was waiting for

	condition.removeWaiting(task);
	task.m_waiting = nullptr;
	task.setReturnValue(static_cast<uint32_t>(std::cv_status::no_timeout)); // set the return value to no timeout

	if(task.m_waitUntil.has_value()) // task was also waiting for a timeout
	{
		task.m_waitUntil = std::nullopt;
		s_timeouts.erase(task);
	}

	s_ready.insertWhen(TaskControlBlock::priorityIsLower, task);
	doSwitch(); // ask for a switch if needed (released a task with higher priority)
	CortexM::setBasepri(previous); // and restore the basepri to its previous value
}

void __attribute__((section(".text.opsy.updatepriority"))) Scheduler::updatePriority(TaskControlBlock& task, Priority newPriority)
{
	auto previous = CortexM::setBasepri(kServiceCallPriority); // get a lock up to service call

	task.m_priority = newPriority;
	if(task.isStarted()) // check task is started
	{
		if(&task == s_currentTask || &task == s_nextTask) // task is current or next to switch to
			doSwitch(); // ask for a switch, this will compare priority again with other ready tasks
		else if(task.m_waiting != nullptr) // task is waiting a condition variable
		{
			task.m_waiting->removeWaiting(task); // remove
			task.m_waiting->addWaiting(task); // then re-insert back the task in the list
		}
		else if(!task.m_waitUntil.has_value()) // task is not current or next, is not waiting for a condition variable, and not waiting for a timeout, then it is in ready list
		{
			s_ready.erase(task); // remove the task from ready list
			s_ready.insertWhen(TaskControlBlock::priorityIsLower, task); // then re insert it, this will update its order
			if(&s_ready.front() == &task) // this brought the task to the first place in the ready list
				doSwitch(); // so test if it is higher priority than current / next task
		}
	}

	Hooks::taskPriorityChanged(task);
	CortexM::setBasepri(previous);
}

uint64_t __attribute__((section(".text.opsy.isr.pendsv_handler"))) Scheduler::pendSvHandler(uint32_t* psp)
{
	Hooks::enterPendSv();
	CortexM::clearPendSv();

	uint64_t result = 0;

	if (s_previousTask != nullptr)
	{
		s_previousTask->m_stackPointer = psp;
		Hooks::taskStopped(*s_previousTask);
	}

	if (s_idling)
		s_idle->m_stackPointer = psp;

	if (s_nextTask == nullptr)
	{
		s_idling = true;
		s_previousTask = nullptr;
		result = reinterpret_cast<uint64_t>(s_idle->m_stackPointer);
		Hooks::enterIdle();
	}
	else
	{
		s_idling = false;
		s_previousTask = s_nextTask;
		s_currentTask = s_nextTask;
		result = reinterpret_cast<uint64_t>(s_currentTask->m_stackPointer);
		s_currentTask->m_lastStarted = s_ticks;
		s_nextTask = nullptr;

		if(s_currentTask->m_mutex != nullptr) // there is a mutex we need to re-acquire before exit
		{
			result |= static_cast<uint64_t>(s_currentTask->m_mutex->reLockFromPendSv(CriticalSection(true))) <<32;
			s_criticalSection = true;
			s_currentTask->m_mutex = nullptr;
			Hooks::mutexRestoredForTask(*s_currentTask);
		}

		assert(s_currentTask->isStarted());
		Hooks::taskStarted(*s_currentTask);
	}
	return result;
}

void __attribute__((section(".text.opsy.isr.svc_handler"))) Scheduler::serviceCallHandler(StackFrame* frame,
		ServiceCallNumber parameter, [[maybe_unused]] bool isThread)
{
	Hooks::enterServiceCall();
	bool taskSwitch = false;

	switch (parameter)
	{
	default:
		assert(false);
		break;

	case ServiceCallNumber::Terminate:
	{
		assert(frame->r0 != 0); // task pointer is nullptr
		auto& task = *reinterpret_cast<TaskControlBlock*>(frame->r0);

		if(!task.m_active.exchange(false)) // was not active, abort termination
			break;

		s_allTasks.erase(task);

		if(task.m_waitUntil.has_value())
		{
			s_timeouts.erase(task);
			task.m_waitUntil = std::nullopt;
		}

		if(task.m_waiting != nullptr)
		{
			task.m_waiting->removeWaiting(task);
			task.m_waiting = nullptr;
		}

		if (&task == s_currentTask)
		{
			assert(!s_criticalSection);
			s_previousTask = s_currentTask = nullptr;
			taskSwitch = doSwitch();
		}
		Hooks::taskTerminated(task);
		break;
	}

	case ServiceCallNumber::Sleep:
	{
		assert(isThread); // should not be called from non thread mode
		assert(!s_criticalSection); // should not be called in critical section
		assert(s_currentTask != nullptr); // cannot be called if there is no current task running

		auto delta = duration{static_cast<int32_t>(frame->r0) + 1}; // add one because we want to wait at least the required time
		assert(delta.count() >= 0);
		s_currentTask->m_waitUntil = s_ticks + delta;
		s_timeouts.insertWhen(wakeupAfter, *s_currentTask);
		Hooks::taskSleep(*s_currentTask);
		s_currentTask = nullptr;
		taskSwitch = doSwitch();
		break;
	}

	case ServiceCallNumber::Switch:
	{
		assert(isThread); // should not be called from non thread mode
		assert(!s_criticalSection); // should not be called in critical section
		taskSwitch = doSwitch();
		break;
	}

	case ServiceCallNumber::Wait:
	{
		assert(isThread); // should not be called from non thread mode
		assert(frame->r0 != 0); // cannot be called with undefined condition variable
		assert(s_currentTask != nullptr); // cannot be called if there is no current task running

		ConditionVariable* condition = reinterpret_cast<ConditionVariable*>(frame->r0);
		duration timeout{frame->r1};
		Mutex* mutex = reinterpret_cast<Mutex*>(frame->r2);

		if(timeout.count() >= 0)
		{
			s_currentTask->m_waitUntil = s_ticks + timeout;
			s_timeouts.insertWhen(wakeupAfter, *s_currentTask);
			Hooks::taskWaitTimeout(*s_currentTask, *condition, s_currentTask->m_waitUntil.value());
			Hooks::conditionVariableStartWaiting(*condition, *s_currentTask, timeout);
		}
		else
		{
			Hooks::taskWait(*s_currentTask, *condition);
			Hooks::conditionVariableStartWaiting(*condition, *s_currentTask);
		}

		if(mutex != nullptr)
		{
			assert(s_criticalSection == true); // should be in critical section
			s_currentTask->m_mutex = mutex;
			mutex->releaseFromServiceCall();
			mutex->m_criticalSection.disable();
			s_criticalSection = false;
			Hooks::mutexStoredForTask(*s_currentTask);
		}

		condition->addWaiting(*s_currentTask);
		s_currentTask->m_waiting = condition;
		s_currentTask = nullptr;
		taskSwitch = doSwitch();
		break;
	}

	}
	Hooks::exitServiceCall(taskSwitch);
}

}

extern "C"
{
void __attribute__((section(".text.opsy.isr.systick"))) SysTick_Handler()
{
	opsy::Scheduler::SystickHandler();
}

void __attribute__((optimize("O0"), naked, section(".text.opsy.isr.pendsv"))) PendSV_Handler()
{
	asm volatile(
			"ldr R1, =%[mask]\n\t"
			"msr BASEPRI, R1 \n\t"
			"isb \n\t"
			"mrs R0, PSP \n\t"
			"tst LR, #16 \n\t"
			"it EQ \n\t"
			"vstmdbeq R0!, {S16-S31} \n\t"
			"mov R2, LR\n\t"
			"mrs R3, CONTROL\n\t"
			"stmdb R0!, {R2-R11} \n\t"
			"bl %[handler] \n\t"
			"ldmia R0!, {R2-R11} \n\t"
			"mov LR, R2\n\t"
			"msr CONTROL, R3\n\t"
			"tst LR, #16 \n\t"
			"it EQ \n\t"
			"vldmiaeq R0!, {S16-S31} \n\t"
			"msr PSP, R0 \n\t"
			"msr BASEPRI, R1 \n\t"
			"isb \n\t"
			"bx LR"
			:
			: [handler] "g" (opsy::Scheduler::pendSvHandler), [mask]"I"(opsy::Scheduler::kServiceCallPriority.value())
			: "r0", "r1", "r2", "r3");
}

void __attribute__((optimize("O0"), naked, section(".text.opsy.isr.svc"))) SVC_Handler()
{
	asm volatile(
			"tst LR, #0x4 \n\t"
			"ite EQ \n\t"
			"mrseq R0, MSP \n\t"
			"mrsne R0, PSP \n\t"
			"tst LR, #0x8 \n\t"
			"ite EQ \n\t"
			"ldreq R2, =0x0 \n\t"
			"ldrne R2, =0x1 \n\t"
			"ldr R1, [R0,#24] \n\t"
			"ldrb R1, [R1,#-2] \n\t"
			"push {LR} \n\t"
			"bl %[handler] \n\t"
			"isb \n\t"
			"dsb \n\t"
			"pop {LR} \n\t"
			"bx LR"
			:
			:[handler] "g" (opsy::Scheduler::serviceCallHandler)
			: "r0", "r1", "r2");
}
}
