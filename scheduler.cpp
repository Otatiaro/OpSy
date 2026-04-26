#include "scheduler.hpp"

namespace opsy
{

__attribute__((section(".bss.opsy.scheduler.isstarted"))) bool Scheduler::is_started_ = false;
__attribute__((section(".bss.opsy.scheduler.ticks"))) opsy::time_point Scheduler::ticks_ = opsy::Startup;
__attribute__((section(".bss.opsy.scheduler.alltasks"))) EmbeddedList<TaskControlBlock, TaskLists::Handle> Scheduler::all_tasks_;
__attribute__((section(".bss.opsy.scheduler.timeouts"))) EmbeddedList<TaskControlBlock, TaskLists::Timeout> Scheduler::timeouts_;
__attribute__((section(".bss.opsy.scheduler.ready"))) EmbeddedList<TaskControlBlock, TaskLists::Waiting> Scheduler::ready_;
__attribute__((section(".bss.opsy.scheduler.idling"))) bool Scheduler::idling_ = false;
__attribute__((section(".bss.opsy.scheduler.mayneedswitch"))) bool Scheduler::may_need_switch_ = false;
__attribute__((section(".bss.opsy.scheduler.idle"))) IdleTaskControlBlock* Scheduler::idle_;
__attribute__((section(".bss.opsy.scheduler.previoustask"))) TaskControlBlock* Scheduler::previous_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.currenttask"))) TaskControlBlock* Scheduler::current_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.nexttask"))) TaskControlBlock* Scheduler::next_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.criticalsection"))) volatile bool Scheduler::critical_section_ = false;

[[noreturn]] bool __attribute__((section(".text.opsy.start"))) Scheduler::start(IdleTaskControlBlock& idle)
{
	assert((CortexM::getType() == CortexM::Type::M4) || (CortexM::getType() == CortexM::Type::M7) || (CortexM::getType() == CortexM::Type::M33)); // only Cortex-M4, M7 and M33 are officially supported
	assert(!is_started_);
	is_started_ = true;

	uint32_t ratio = std::chrono::duration_cast<duration>(
			std::chrono::duration<int64_t>(1)).count(); // get ratio from timeout clock (system ticks to 1Hz)

	auto coreClock = getCoreClock();
	idle_ = &idle;

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
		for(const auto& task : all_tasks_)
			callback(task);
	});

	CortexM::setPsp(CortexM::getMsp());
	CortexM::setControl(0b10);
	CortexM::setMsp(CortexM::mspAtReset());

	if(!doSwitch())
		__builtin_trap();
	while(true) {} // can not reach
}

bool __attribute__((section(".text.opsy.doswitch"))) Scheduler::doSwitch()
{
	assert(is_started_);
	assert((current_task_ != nullptr) || (critical_section_ == false)); // can't have no current task in critical section

	if(critical_section_)
	{
		may_need_switch_ = true;
		return false;
	}

	if (next_task_ != nullptr)
	{
		assert(current_task_ != next_task_);

		ready_.insertWhen(TaskControlBlock::priorityIsLower, *next_task_);
		next_task_ = nullptr;
	}

	const auto current = current_task_;

	if (current_task_ != nullptr)
	{
		ready_.insertWhen(TaskControlBlock::priorityIsLower, *current_task_);
		current_task_ = nullptr;
	}

	if (ready_.empty())
	{
		CortexM::triggerPendSv();
		return true;
	}
	else
	{
		next_task_ = &ready_.front();
		ready_.pop_front();

		if (next_task_ == current)
		{
			current_task_ = next_task_;
			next_task_ = nullptr;
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
	assert(task.waiting_ == &condition); // check the condition is the one the task was waiting for

	condition.removeWaiting(task);
	task.waiting_ = nullptr;
	task.setReturnValue(static_cast<uint32_t>(std::cv_status::no_timeout)); // set the return value to no timeout

	if(task.wait_until_.has_value()) // task was also waiting for a timeout
	{
		task.wait_until_ = std::nullopt;
		timeouts_.erase(task);
	}

	ready_.insertWhen(TaskControlBlock::priorityIsLower, task);
	doSwitch(); // ask for a switch if needed (released a task with higher priority)
	CortexM::setBasepri(previous); // and restore the basepri to its previous value
}

void __attribute__((section(".text.opsy.updatepriority"))) Scheduler::updatePriority(TaskControlBlock& task, Priority newPriority)
{
	auto previous = CortexM::setBasepri(kServiceCallPriority); // get a lock up to service call

	task.priority_ = newPriority;
	if(task.isStarted()) // check task is started
	{
		if(&task == current_task_ || &task == next_task_) // task is current or next to switch to
			doSwitch(); // ask for a switch, this will compare priority again with other ready tasks
		else if(task.waiting_ != nullptr) // task is waiting a condition variable
		{
			task.waiting_->removeWaiting(task); // remove
			task.waiting_->addWaiting(task); // then re-insert back the task in the list
		}
		else if(!task.wait_until_.has_value()) // task is not current or next, is not waiting for a condition variable, and not waiting for a timeout, then it is in ready list
		{
			ready_.erase(task); // remove the task from ready list
			ready_.insertWhen(TaskControlBlock::priorityIsLower, task); // then re insert it, this will update its order
			if(&ready_.front() == &task) // this brought the task to the first place in the ready list
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

	if (previous_task_ != nullptr)
	{
		assert(psp >= previous_task_->stack_base_); // Process stack pointer below the task stack base, stack overflow !
		assert(*previous_task_->stack_base_ == TaskControlBlock::Dummy); // The lowest slot of the task stack has been modified, this shows a stack overflow
		previous_task_->stack_pointer_ = psp;
		Hooks::taskStopped(*previous_task_);
	}

	if (idling_)
		idle_->stack_pointer_ = psp;

	if (next_task_ == nullptr)
	{
		idling_ = true;
		previous_task_ = nullptr;
		result = reinterpret_cast<uint64_t>(idle_->stack_pointer_);
		Hooks::enterIdle();
	}
	else
	{
		idling_ = false;
		previous_task_ = next_task_;
		current_task_ = next_task_;
		result = reinterpret_cast<uint64_t>(current_task_->stack_pointer_);
		current_task_->last_started_ = ticks_;
		next_task_ = nullptr;

		if(current_task_->mutex_ != nullptr) // there is a mutex we need to re-acquire before exit
		{
			result |= static_cast<uint64_t>(current_task_->mutex_->reLockFromPendSv(CriticalSection(true))) <<32;
			critical_section_ = true;
			current_task_->mutex_ = nullptr;
			Hooks::mutexRestoredForTask(*current_task_);
		}

		assert(current_task_->isStarted());
		Hooks::taskStarted(*current_task_);
	}

	if constexpr (CortexM::kHasStackLimitRegs)
		CortexM::setPsplim(idling_ ? idle_->stack_base_ : current_task_->stack_base_);

	return result;
}

void __attribute__((section(".text.opsy.isr.svc_handler"))) Scheduler::serviceCallHandler(StackFrame* frame,
		ServiceCallNumber parameter, [[maybe_unused]] bool isThread, uint32_t excReturn)
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

		if(!task.active_.exchange(false)) // was not active, abort termination
			break;

		all_tasks_.erase(task);

		if(task.wait_until_.has_value())
		{
			timeouts_.erase(task);
			task.wait_until_ = std::nullopt;
		}

		if(task.waiting_ != nullptr)
		{
			task.waiting_->removeWaiting(task);
			task.waiting_ = nullptr;
		}

		if (&task == current_task_)
		{
			assert(!critical_section_);
			previous_task_ = current_task_ = nullptr;
			taskSwitch = doSwitch();
		}
		Hooks::taskTerminated(task);
		break;
	}

	case ServiceCallNumber::Sleep:
	{
		assert(isThread); // should not be called from non thread mode
		assert(!critical_section_); // should not be called in critical section
		assert(current_task_ != nullptr); // cannot be called if there is no current task running

		const int64_t raw = static_cast<int64_t>(frame->r0) | (static_cast<int64_t>(frame->r1) << 32);
		auto delta = duration{raw + 1}; // add one because we want to wait at least the required time
		assert(delta.count() >= 0);
		current_task_->wait_until_ = ticks_ + delta;
		timeouts_.insertWhen(wakeupAfter, *current_task_);
		Hooks::taskSleep(*current_task_);
		current_task_ = nullptr;
		taskSwitch = doSwitch();
		break;
	}

	case ServiceCallNumber::Switch:
	{
		assert(isThread); // should not be called from non thread mode
		assert(!critical_section_); // should not be called in critical section
		taskSwitch = doSwitch();
		break;
	}

	case ServiceCallNumber::Wait:
	{
		assert(isThread); // should not be called from non thread mode
		assert(frame->r0 != 0); // cannot be called with undefined condition variable
		assert(current_task_ != nullptr); // cannot be called if there is no current task running

		ConditionVariable* condition = reinterpret_cast<ConditionVariable*>(frame->r0);
		const int64_t raw = static_cast<int64_t>(frame->r1) | (static_cast<int64_t>(frame->r2) << 32);
		duration timeout{raw};
		Mutex* mutex = reinterpret_cast<Mutex*>(frame->r3);

		if(timeout.count() >= 0)
		{
			current_task_->wait_until_ = ticks_ + timeout;
			timeouts_.insertWhen(wakeupAfter, *current_task_);
			Hooks::taskWaitTimeout(*current_task_, *condition, current_task_->wait_until_.value());
			Hooks::conditionVariableStartWaiting(*condition, *current_task_, timeout);
		}
		else
		{
			Hooks::taskWait(*current_task_, *condition);
			Hooks::conditionVariableStartWaiting(*condition, *current_task_);
		}

		if(mutex != nullptr)
		{
			assert(critical_section_ == true); // should be in critical section
			current_task_->mutex_ = mutex;
			mutex->releaseFromServiceCall();
			mutex->critical_section_.disable();
			critical_section_ = false;
			Hooks::mutexStoredForTask(*current_task_);
		}

		condition->addWaiting(*current_task_);
		current_task_->waiting_ = condition;

		// Pre-set stack_pointer_ so setReturnValue() is safe if an ISR tail-chains
		// before PendSV saves the context. PendSV will save Context (and FpContext
		// when the FPU was active) below 'frame'; computing that address here lets
		// wakeUp() → setReturnValue() navigate to StackFrame::r0 correctly regardless
		// of whether PendSV has already run.
		{
			const bool fpuActive = !(excReturn & TaskControlBlock::kFpFlag);
			auto* sp = reinterpret_cast<TaskControlBlock::StackItem*>(frame)
					  - sizeof(Context) / sizeof(TaskControlBlock::StackItem)
					  - (fpuActive ? sizeof(FpContext) / sizeof(TaskControlBlock::StackItem) : 0u);
			reinterpret_cast<Context*>(sp)->lr = excReturn; // pre-populate lr for setReturnValue's FPU check
			current_task_->stack_pointer_ = sp;
		}

		current_task_ = nullptr;
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
			"mov R3, LR \n\t"        // pass EXC_RETURN as 4th argument
			"bl %[handler] \n\t"
			"isb \n\t"
			"dsb \n\t"
			"pop {LR} \n\t"
			"bx LR"
			:
			:[handler] "g" (opsy::Scheduler::serviceCallHandler)
			: "r0", "r1", "r2", "r3");
}
}
