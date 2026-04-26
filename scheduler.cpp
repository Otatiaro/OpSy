#include "scheduler.hpp"

namespace opsy
{

__attribute__((section(".bss.opsy.scheduler.isstarted"))) bool scheduler::is_started_ = false;
__attribute__((section(".bss.opsy.scheduler.ticks"))) opsy::time_point scheduler::ticks_ = opsy::startup;
__attribute__((section(".bss.opsy.scheduler.alltasks"))) embedded_list<task_control_block, task_lists::handle> scheduler::all_tasks_;
__attribute__((section(".bss.opsy.scheduler.timeouts"))) embedded_list<task_control_block, task_lists::timeout> scheduler::timeouts_;
__attribute__((section(".bss.opsy.scheduler.ready"))) embedded_list<task_control_block, task_lists::waiting> scheduler::ready_;
__attribute__((section(".bss.opsy.scheduler.idling"))) bool scheduler::idling_ = false;
__attribute__((section(".bss.opsy.scheduler.mayneedswitch"))) bool scheduler::may_need_switch_ = false;
__attribute__((section(".bss.opsy.scheduler.idle"))) idle_task_control_block* scheduler::idle_;
__attribute__((section(".bss.opsy.scheduler.previoustask"))) task_control_block* scheduler::previous_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.currenttask"))) task_control_block* scheduler::current_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.nexttask"))) task_control_block* scheduler::next_task_ = nullptr;
__attribute__((section(".bss.opsy.scheduler.criticalsection"))) volatile bool scheduler::critical_section_ = false;

[[noreturn]] void __attribute__((section(".text.opsy.start"))) scheduler::start(idle_task_control_block& idle)
{
	{
		const auto running = cortex_m::type();
		assert(running == cortex_m::core_type::m3
			|| running == cortex_m::core_type::m4
			|| running == cortex_m::core_type::m7
			|| running == cortex_m::core_type::m33); // only M3/M4/M7/M33 are officially supported
	}
	assert(!is_started_);
	is_started_ = true;

	uint32_t ratio = std::chrono::duration_cast<duration>(
			std::chrono::duration<int64_t>(1)).count(); // get ratio from timeout clock (system ticks to 1Hz)

	auto core_clock_value = core_clock();
	idle_ = &idle;

	cortex_m::preempt_bits(preemption_bits);
	cortex_m::set_priority(cortex_m::system_irq::service_call, service_call_priority); // service call is at system preemption and highest sub priority
	cortex_m::set_priority(cortex_m::system_irq::pend_sv, pend_sv_priority); // pending service is at lowest possible priority
	cortex_m::set_priority(cortex_m::system_irq::systick, systick_priority); // systick handler is at system preemption with lowest sub priority
	cortex_m::set_isr_handler(cortex_m::system_irq::systick, ::SysTick_Handler);
	cortex_m::set_isr_handler(cortex_m::system_irq::pend_sv, ::PendSV_Handler);
	cortex_m::set_isr_handler(cortex_m::system_irq::service_call, ::SVC_Handler);

	assert(core_clock_value % ratio == 0u); // for exact time clock the core clock divided by ratio should not leave a remainder
	cortex_m::enable_systick(core_clock_value / ratio);

	hooks::starting(idle, core_clock_value, [](callback<void(const task_control_block&)> callback)
	{
		for(const auto& task : all_tasks_)
			callback(task);
	});

	cortex_m::set_psp(cortex_m::msp());
	cortex_m::set_control(cortex_m::control_thread_psp_privileged);
	cortex_m::set_msp(cortex_m::msp_at_reset());

	if(!do_switch())
		opsy::trap();
	while(true) {} // can not reach
}

bool __attribute__((section(".text.opsy.doswitch"))) scheduler::do_switch()
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

		ready_.insert_when(task_control_block::priority_is_lower, *next_task_);
		next_task_ = nullptr;
	}

	const auto current = current_task_;

	if (current_task_ != nullptr)
	{
		ready_.insert_when(task_control_block::priority_is_lower, *current_task_);
		current_task_ = nullptr;
	}

	if (ready_.empty())
	{
		cortex_m::trigger_pend_sv();
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
			cortex_m::trigger_pend_sv();
			return true;
		}
	}
}

void __attribute__((naked)) scheduler::terminate_task(task_control_block* task)
{
	asm volatile(
			"nop \n\t"
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (service_call_number::terminate), [task] "r" (task)
			: "r0");
}





void __attribute__((section(".text.opsy.wakeup"))) scheduler::wake_up(task_control_block& task, condition_variable& condition)
{
	auto previous = cortex_m::set_basepri(service_call_priority); // get a lock up to service call
	assert(task.waiting_ == &condition); // check the condition is the one the task was waiting for

	condition.remove_waiting(task);
	task.waiting_ = nullptr;
	task.set_return_value(static_cast<uint32_t>(cv_status::no_timeout)); // set the return value to no timeout

	if(task.wait_until_.has_value()) // task was also waiting for a timeout
	{
		task.wait_until_ = std::nullopt;
		timeouts_.erase(task);
	}

	ready_.insert_when(task_control_block::priority_is_lower, task);
	do_switch(); // ask for a switch if needed (released a task with higher priority)
	cortex_m::set_basepri(previous); // and restore the basepri to its previous value
}

void __attribute__((section(".text.opsy.updatepriority"))) scheduler::update_priority(task_control_block& task, task_priority new_priority)
{
	auto previous = cortex_m::set_basepri(service_call_priority); // get a lock up to service call

	task.priority_ = new_priority;
	if(task.is_started()) // check task is started
	{
		if(&task == current_task_ || &task == next_task_) // task is current or next to switch to
			do_switch(); // ask for a switch, this will compare priority again with other ready tasks
		else if(task.waiting_ != nullptr) // task is waiting a condition variable
		{
			task.waiting_->remove_waiting(task); // remove
			task.waiting_->add_waiting(task); // then re-insert back the task in the list
		}
		else if(!task.wait_until_.has_value()) // task is not current or next, is not waiting for a condition variable, and not waiting for a timeout, then it is in ready list
		{
			ready_.erase(task); // remove the task from ready list
			ready_.insert_when(task_control_block::priority_is_lower, task); // then re insert it, this will update its order
			if(&ready_.front() == &task) // this brought the task to the first place in the ready list
				do_switch(); // so test if it is higher priority than current / next task
		}
	}

	hooks::task_priority_changed(task);
	cortex_m::set_basepri(previous);
}

uint64_t __attribute__((section(".text.opsy.isr.pendsv_handler"))) scheduler::pend_sv_handler(uint32_t* psp)
{
	hooks::enter_pend_sv();
	cortex_m::clear_pend_sv();

	uint64_t result = 0;

	if (previous_task_ != nullptr)
	{
		assert(psp >= previous_task_->stack_base_); // Process stack pointer below the task stack base, stack overflow !
		assert(*previous_task_->stack_base_ == task_control_block::dummy_pattern); // The lowest slot of the task stack has been modified, this shows a stack overflow
		previous_task_->stack_pointer_ = psp;
		hooks::task_stopped(*previous_task_);
	}

	if (idling_)
		idle_->stack_pointer_ = psp;

	if (next_task_ == nullptr)
	{
		idling_ = true;
		previous_task_ = nullptr;
		result = reinterpret_cast<uint64_t>(idle_->stack_pointer_);
		hooks::enter_idle();
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
			result |= static_cast<uint64_t>(current_task_->mutex_->relock_from_pend_sv(critical_section(true))) <<32;
			critical_section_ = true;
			current_task_->mutex_ = nullptr;
			hooks::mutex_restored_for_task(*current_task_);
		}

		assert(current_task_->is_started());
		hooks::task_started(*current_task_);
	}

	if constexpr (cortex_m::has_stack_limit_regs)
		cortex_m::set_psplim(idling_ ? idle_->stack_base_ : current_task_->stack_base_);

	return result;
}

void __attribute__((section(".text.opsy.isr.svc_handler"))) scheduler::service_call_handler(stack_frame* frame,
		service_call_number parameter, [[maybe_unused]] bool is_thread, uint32_t exc_return)
{
	hooks::enter_service_call();
	bool task_switch = false;

	switch (parameter)
	{
	default:
		assert(false);
		break;

	case service_call_number::terminate:
	{
		assert(frame->r0 != 0); // task pointer is nullptr
		auto& task = *reinterpret_cast<task_control_block*>(frame->r0);

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
			task.waiting_->remove_waiting(task);
			task.waiting_ = nullptr;
		}

		if (&task == current_task_)
		{
			assert(!critical_section_);
			previous_task_ = current_task_ = nullptr;
			task_switch = do_switch();
		}
		hooks::task_terminated(task);
		break;
	}

	case service_call_number::sleep:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(!critical_section_); // should not be called in critical section
		assert(current_task_ != nullptr); // cannot be called if there is no current task running

		const int64_t raw = static_cast<int64_t>(frame->r0) | (static_cast<int64_t>(frame->r1) << 32);
		auto delta = duration{raw + 1}; // add one because we want to wait at least the required time
		assert(delta.count() >= 0);
		current_task_->wait_until_ = ticks_ + delta;
		timeouts_.insert_when(wakeup_after, *current_task_);
		hooks::task_sleep(*current_task_);
		current_task_ = nullptr;
		task_switch = do_switch();
		break;
	}

	case service_call_number::context_switch:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(!critical_section_); // should not be called in critical section
		task_switch = do_switch();
		break;
	}

	case service_call_number::wait:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(frame->r0 != 0); // cannot be called with undefined condition variable
		assert(current_task_ != nullptr); // cannot be called if there is no current task running

		condition_variable* condition = reinterpret_cast<condition_variable*>(frame->r0);
		const int64_t raw = static_cast<int64_t>(frame->r1) | (static_cast<int64_t>(frame->r2) << 32);
		duration timeout{raw};
		mutex* mtx = reinterpret_cast<mutex*>(frame->r3);

		if(timeout.count() >= 0)
		{
			current_task_->wait_until_ = ticks_ + timeout;
			timeouts_.insert_when(wakeup_after, *current_task_);
			hooks::task_wait_timeout(*current_task_, *condition, current_task_->wait_until_.value());
			hooks::condition_variable_start_waiting(*condition, *current_task_, timeout);
		}
		else
		{
			hooks::task_wait(*current_task_, *condition);
			hooks::condition_variable_start_waiting(*condition, *current_task_);
		}

		if(mtx != nullptr)
		{
			assert(critical_section_ == true); // should be in critical section
			current_task_->mutex_ = mtx;
			mtx->release_from_service_call();
			mtx->critical_section_.disable();
			critical_section_ = false;
			hooks::mutex_stored_for_task(*current_task_);
		}

		condition->add_waiting(*current_task_);
		current_task_->waiting_ = condition;

		// Pre-set stack_pointer_ so set_return_value() is safe if an ISR tail-chains
		// before PendSV saves the context. PendSV will save context (and fp_context
		// when the FPU was active) below 'frame'; computing that address here lets
		// wake_up() → set_return_value() navigate to stack_frame::r0 correctly regardless
		// of whether PendSV has already run.
		{
			auto* sp = reinterpret_cast<task_control_block::stack_item*>(frame)
					  - sizeof(context) / sizeof(task_control_block::stack_item);
			if constexpr (cortex_m::has_fpu)
			{
				const bool fpu_active = !(exc_return & cortex_m::exc_return_fp_flag);
				if (fpu_active)
					sp -= sizeof(fp_context) / sizeof(task_control_block::stack_item);
			}
			reinterpret_cast<context*>(sp)->lr = exc_return; // pre-populate lr for set_return_value's FPU check
			current_task_->stack_pointer_ = sp;
		}

		current_task_ = nullptr;
		task_switch = do_switch();
		break;
	}

	}
	hooks::exit_service_call(task_switch);
}

}

extern "C"
{
void __attribute__((section(".text.opsy.isr.systick"))) SysTick_Handler()
{
	opsy::scheduler::systick_handler();
}

/**
 * @brief @c PendSV handler — ARMv7-M / ARMv8-M Mainline (Cortex-M3 / M4 / M7 / M33).
 * @remark Uses @c BASEPRI to lock at the service-call priority, Thumb-2 @c stmdb / @c ldmia
 *         to save and restore @c R4-R11 in a single instruction, and conditionally saves
 *         the FP callee-saved registers (@c S16-S31) when the running task had the FPU
 *         active. The FP save/restore block is compiled in only when the toolchain reports
 *         a hardware FPU (@c __ARM_FP) so cores without an FPU (Cortex-M3) pay zero clock
 *         cycle for the absent feature.
 */
void __attribute__((optimize("O0"), naked, section(".text.opsy.isr.pendsv"))) PendSV_Handler()
{
	asm volatile(
			"ldr R1, =%[mask]\n\t"
			"msr BASEPRI, R1 \n\t"
			"isb \n\t"
			"mrs R0, PSP \n\t"
#if defined(__ARM_FP)
			"tst LR, %[fp_flag] \n\t"
			"it EQ \n\t"
			"vstmdbeq R0!, {S16-S31} \n\t"
#endif
			"mov R2, LR\n\t"
			"mrs R3, CONTROL\n\t"
			"stmdb R0!, {R2-R11} \n\t"
			"bl %[handler] \n\t"
			"ldmia R0!, {R2-R11} \n\t"
			"mov LR, R2\n\t"
			"msr CONTROL, R3\n\t"
#if defined(__ARM_FP)
			"tst LR, %[fp_flag] \n\t"
			"it EQ \n\t"
			"vldmiaeq R0!, {S16-S31} \n\t"
#endif
			"msr PSP, R0 \n\t"
			"msr BASEPRI, R1 \n\t"
			"isb \n\t"
			"bx LR"
			:
			: [handler] "g" (opsy::scheduler::pend_sv_handler),
			  [mask] "I" (opsy::scheduler::service_call_priority.value()),
			  [fp_flag] "I" (opsy::cortex_m::exc_return_fp_flag)
			: "r0", "r1", "r2", "r3");
}

void __attribute__((optimize("O0"), naked, section(".text.opsy.isr.svc"))) SVC_Handler()
{
	asm volatile(
			"tst LR, %[psp_flag] \n\t"
			"ite EQ \n\t"
			"mrseq R0, MSP \n\t"
			"mrsne R0, PSP \n\t"
			"tst LR, %[thread_flag] \n\t"
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
			: [handler] "g" (opsy::scheduler::service_call_handler),
			  [psp_flag] "I" (opsy::cortex_m::exc_return_psp_flag),
			  [thread_flag] "I" (opsy::cortex_m::exc_return_thread_flag)
			: "r0", "r1", "r2", "r3");
}
}
