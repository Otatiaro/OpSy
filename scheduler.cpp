#include "scheduler.hpp"
#include "condition_variable.hpp"
#include "isr_lock.hpp"
#include "mutex.hpp"

#include <algorithm>
#include <ranges>

// Per-function "do not optimize this body" attribute. The exception
// handlers below are also @c naked (no compiler-emitted prologue or
// epilogue) so that the inline asm has full control of the stack and
// the register set; layering an O0 attribute on top is belt-and-suspenders
// against any later edit accidentally letting the compiler reorder or
// fold the asm.
//
// GCC and Clang spell this differently:
//   - GCC: __attribute__((optimize("O0")))   (per-function -O0)
//   - Clang: __attribute__((optnone))        (no per-function optimize attribute)
#if defined(__clang__)
#define OPSY_NO_OPT __attribute__((optnone))
#else
#define OPSY_NO_OPT __attribute__((optimize("O0")))
#endif

namespace opsy
{

__attribute__((section(".bss.opsy.scheduler.isstarted"))) std::atomic<bool> scheduler::is_started_{false};
__attribute__((section(".bss.opsy.scheduler.ticks"))) opsy::time_point scheduler::ticks_ = opsy::startup;
__attribute__((section(".bss.opsy.scheduler.alltasks"))) embedded_list<task_control_block, task_lists::handle> scheduler::all_tasks_;
__attribute__((section(".bss.opsy.scheduler.timeouts"))) embedded_list<task_control_block, task_lists::timeout> scheduler::timeouts_;
__attribute__((section(".bss.opsy.scheduler.ready"))) embedded_list<task_control_block, task_lists::waiting> scheduler::ready_;
__attribute__((section(".bss.opsy.scheduler.lockedmutexes"))) embedded_list<mutex, mutex> scheduler::locked_mutexes_;
__attribute__((section(".bss.opsy.scheduler.idling"))) std::atomic<bool> scheduler::idling_{false};
__attribute__((section(".bss.opsy.scheduler.mayneedswitch"))) std::atomic<bool> scheduler::may_need_switch_{false};
__attribute__((section(".bss.opsy.scheduler.idle"))) std::atomic<idle_task_control_block*> scheduler::idle_{nullptr};
__attribute__((section(".bss.opsy.scheduler.previoustask"))) std::atomic<task_control_block*> scheduler::previous_task_{nullptr};
__attribute__((section(".bss.opsy.scheduler.currenttask"))) std::atomic<task_control_block*> scheduler::current_task_{nullptr};
__attribute__((section(".bss.opsy.scheduler.nexttask"))) std::atomic<task_control_block*> scheduler::next_task_{nullptr};
__attribute__((section(".bss.opsy.scheduler.criticalsection"))) std::atomic<bool> scheduler::critical_section_{false};

[[noreturn]] void __attribute__((section(".text.opsy.start"))) scheduler::start_impl(idle_task_control_block& idle)
{
	{
		[[maybe_unused]] const auto running = cortex_m::type();
		assert(running == cortex_m::core_type::m3
			|| running == cortex_m::core_type::m4
			|| running == cortex_m::core_type::m7
			|| running == cortex_m::core_type::m33); // only M3/M4/M7/M33 are officially supported
	}
	assert(!is_started_.load(std::memory_order_relaxed));

	uint32_t ratio = std::chrono::duration_cast<duration>(
			std::chrono::duration<int64_t>(1)).count(); // get ratio from timeout clock (system ticks to 1Hz)

	auto core_clock_value = core_clock();
	idle_.store(&idle, std::memory_order_relaxed);

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

	// Started means add_task asks for a switch. That is only safe once thread
	// mode runs on the process stack and the first do_switch has edited ready_,
	// so the flag is set here, with every handler that may call into OpSy held
	// off until the switch is pended.
	[[maybe_unused]] const auto unmasked = cortex_m::set_basepri(service_call_priority);
	assert(unmasked.value() == 0); // started from plain thread mode, nothing masked

	cortex_m::set_psp(cortex_m::msp());
	cortex_m::set_control(cortex_m::control_thread_psp_privileged);
	cortex_m::set_msp(cortex_m::msp_at_reset());

	is_started_.store(true, std::memory_order_relaxed);
	if(!do_switch())
		opsy::trap();

	// Back to 0 rather than to a saved value: from set_msp on, an exception
	// stacks over this function's frame, so nothing kept there is reliable.
	cortex_m::set_basepri(isr_priority(0)); // PendSV is taken here
	while(true) {} // can not reach
}

inline void scheduler::systick_handler()
{
	hooks::enter_systick();
	ticks_+=duration(1); // the only writer, and nothing above system preemption level runs while it does; readers below that level go through now(), which masks

	bool dirty = false;


	// Tasks on the timeouts_ list always have wait_until_ engaged (it is set
	// before insertion and cleared after pop). Use * not .value() to avoid
	// the bad_optional_access -> abort path under -fno-exceptions.
	while(!timeouts_.empty() && *timeouts_.front().wait_until_ <= ticks_)
	{
		auto& task = timeouts_.front();
		timeouts_.pop_front();
		task.wait_until_ = std::nullopt;

		if(task.waiting_ != nullptr)
		{
			task.waiting_->remove_waiting(task);
			task.waiting_ = nullptr; // the task no longer waits on it -- see wake_up, which does the same
			task.set_return_value(static_cast<uint32_t>(cv_status::timeout)); // notify timeout to thread (write value to its R0 frame)
		}

		// Same path as a notification: a task that waited holding a mutex
		// must own it again before it can run, and may have to block on
		// it instead of becoming runnable.
		resume_waiter(task);
		dirty = true;
	}

	if(dirty)
		hooks::exit_systick(do_switch());
	else
		hooks::exit_systick(false);
}

bool __attribute__((section(".text.opsy.doswitch"))) scheduler::do_switch()
{
	assert(is_started_.load(std::memory_order_relaxed));
	auto* current = current_task_.load(std::memory_order_relaxed);
	auto* next    = next_task_.load(std::memory_order_relaxed);
	assert((current != nullptr) || (critical_section_.load(std::memory_order_relaxed) == false)); // can't have no current task in critical section

	if(critical_section_.load(std::memory_order_relaxed))
	{
		may_need_switch_.store(true, std::memory_order_relaxed);
		return false;
	}

	if (next != nullptr)
	{
		assert(current != next);

		ready_.insert_when(task_control_block::priority_is_lower, *next);
		next_task_.store(nullptr, std::memory_order_relaxed);
		next = nullptr;
	}

	if (current != nullptr)
	{
		ready_.insert_when(task_control_block::priority_is_lower, *current);
		current_task_.store(nullptr, std::memory_order_relaxed);
	}

	if (ready_.empty())
	{
		cortex_m::trigger_pend_sv();
		return true;
	}
	else
	{
		auto& front = ready_.front();
		ready_.pop_front();

		if (&front == current)
		{
			current_task_.store(&front, std::memory_order_relaxed);
			next_task_.store(nullptr, std::memory_order_relaxed);
			return false;
		}
		else
		{
			next_task_.store(&front, std::memory_order_relaxed);
			cortex_m::trigger_pend_sv();
			return true;
		}
	}
}

void __attribute__((naked)) scheduler::terminate_task([[maybe_unused]] task_control_block* task)
{
	// The function is `naked`, so the compiler emits no prologue that could
	// touch r0 before our asm runs. Per AAPCS the first pointer-sized
	// argument (`task`) is already passed in r0 at entry, so the SVC handler
	// receives it without any explicit `mov` here. We deliberately do NOT
	// reference `task` through a `[task] "r" (task)` operand: clang refuses
	// parameter references inside naked functions
	// ("parameter references not allowed in naked functions"), since with no
	// prologue there is no place for the compiler to materialize the value
	// into the requested constraint. Relying on the AAPCS register placement
	// works for both gcc and clang.
	//
	// The leading `nop` exists for one reason only: when GDB unwinds the
	// stack of a task that is currently running its entry callable, the
	// link register saved on entry points at the "return target" of that
	// implicit call. Without the nop, that target would be the very start
	// of terminate_task — GDB renders that as "terminate_task at +0",
	// which looks weird. The nop pushes the actual return target
	// (i.e. the SVC) one instruction back so the backtrace renders like a
	// normal call return.
	//
	// Rather than computing the address of the SVC as
	//   reinterpret_cast<uint32_t>(terminate_task) + 2
	// — which silently breaks if the leading nop is ever emitted as a
	// 32-bit Thumb-2 instruction, if alignment changes, or if LTO ever
	// decides to pad the function — we expose its address directly as the
	// global label `opsy_terminate_task_resume`. `.thumb_func` makes the
	// linker mark the symbol as a Thumb function, so taking its address
	// in C++ yields a value with the LSB set, exactly what `bx lr` needs
	// for interworking. task_control_block::start_impl assigns
	// frame->lr = &opsy_terminate_task_resume, no offset arithmetic.
	asm volatile(
			"nop\n\t"
			".global opsy_terminate_task_resume\n\t"
			".thumb_func\n\t"
			"opsy_terminate_task_resume:\n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (service_call_number::terminate)
			: "r0");
}





/**
 * @brief The return address of the idle task's entry function
 *
 *        An idle loop never returns; if one does, it stops on the breakpoint.
 *        Same shape as terminate_task: the label sits one instruction in, so a
 *        backtrace shows a call returning rather than a function at +0, and
 *        the address is taken from the label rather than computed.
 */
[[gnu::used]] static void __attribute__((naked, section(".text.opsy.idlereturned"))) idle_return_site()
{
	asm volatile(
			"nop\n\t"
			".global opsy_idle_returned\n\t"
			".thumb_func\n\t"
			"opsy_idle_returned:\n\t"
			"bkpt 0");
}

/**
 * @brief Puts a task woken from a condition variable back where it belongs
 *
 *        A task that waited with an @c opsy::mutex has to own it again before
 *        it may run. If it is free the task takes it and becomes runnable; if
 *        another task holds it, the wake turns straight into blocking on the
 *        mutex, and the task goes into no list at all.
 *
 *        This is the one place where the two waits meet, and the order matters:
 *        decide about the mutex first, insert into ready_ only if it was
 *        obtained. Inserting first and sorting it out afterwards is what put a
 *        task in two lists at once in earlier versions of this scheduler.
 */
void __attribute__((section(".text.opsy.resumewaiter"))) scheduler::resume_waiter(task_control_block& task)
{
	if (auto* const* held = std::get_if<mutex*>(&task.released_lock_))
	{
		auto& target = **held;
		task.released_lock_ = std::monostate{};

		if (target.owner_ != nullptr)
		{
			// Not free: the wake becomes a block on the mutex instead. The
			// task ends up in no list, carrying blocked_on_.
			block_on(target, task);
			return;
		}

		take_mutex(target, task);
	}

	ready_.insert_when(task_control_block::priority_is_lower, task);
	hooks::task_ready(task);
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

	resume_waiter(task); // may block on the mutex instead of becoming runnable
	do_switch(); // ask for a switch if needed (released a task with higher priority)
	cortex_m::set_basepri(previous); // and restore the basepri to its previous value
}

void __attribute__((section(".text.opsy.takemutex"))) scheduler::take_mutex(mutex& m, task_control_block& task)
{
	assert(m.owner_ == nullptr);
	m.owner_ = &task;
	locked_mutexes_.push_front(m);
	hooks::mutex_taken(m, task);
}

void __attribute__((section(".text.opsy.releasemutex"))) scheduler::release_mutex(mutex& m, task_control_block& owner)
{
	assert(m.owner_ == &owner);

	locked_mutexes_.erase(m);
	m.owner_ = nullptr;
	hooks::mutex_released(m, owner);

	hand_over(m);

	// Releasing may drop an inherited priority — though not necessarily to the
	// base one, if another held mutex still has waiters of its own.
	recompute_priority(owner);
}

bool __attribute__((section(".text.opsy.handover"))) scheduler::hand_over(mutex& m)
{
	assert(m.owner_ == nullptr);

	auto* next = highest_waiter(m);
	if (next == nullptr)
		return false;

	// Handed over rather than left free: whoever was blocked owns it on wake,
	// so it cannot be taken by someone else in between.
	next->blocked_on_ = nullptr;
	take_mutex(m, *next);
	ready_.insert_when(task_control_block::priority_is_lower, *next);
	hooks::task_ready(*next);
	return true;
}

void __attribute__((section(".text.opsy.blockon"))) scheduler::block_on(mutex& m, task_control_block& task)
{
	assert(m.owner_ != nullptr);
	assert(m.owner_ != &task);

	task.blocked_on_ = &m;
	hooks::mutex_blocked(m, task);

	// Raise the holder — and whoever it is itself waiting for — so it can run
	// to the point of releasing rather than being starved by a task of
	// middling priority.
	inherit_priority(*m.owner_, task.priority_);
}

task_control_block* __attribute__((section(".text.opsy.highestwaiter"))) scheduler::highest_waiter(const mutex& m)
{
	// Walked rather than looked up: the waiters of a mutex are derived from
	// task::blocked_on_, which is the only place the relation is stored. A
	// per-mutex wait list would be a second copy of it, and a second thing to
	// keep in step.
	auto waiters = all_tasks_ | std::views::filter([&m](const auto& task) { return task.blocked_on_ == &m; });

	const auto best = std::ranges::min_element(waiters, task_control_block::priority_is_lower);
	return best == std::ranges::end(waiters) ? nullptr : &*best;
}

void __attribute__((section(".text.opsy.recomputepriority"))) scheduler::recompute_priority(task_control_block& task)
{
	auto effective = task.base_priority_;

	// Still-held mutexes may have waiters of their own, so releasing one does
	// not necessarily drop the task back to its base priority: the strongest
	// donation still owed wins over the requested priority.
	for (const auto& held : locked_mutexes_ | std::views::filter([&task](const auto& m) { return m.owner_ == &task; }))
		if (const auto* waiter = highest_waiter(held); waiter != nullptr && waiter->priority_ < effective)
			effective = waiter->priority_;

	task.priority_ = effective;
}

void __attribute__((section(".text.opsy.requeue"))) scheduler::requeue(task_control_block& task)
{
	// Both lists are ordered by priority, and both are read from the front:
	// do_switch elects the front of ready_, notify_one wakes the front of a
	// condition variable's waiters. A task whose priority changed while it was
	// queued stays where its old priority put it until it is moved.
	if (task.is_ready())
	{
		ready_.erase(task);
		ready_.insert_when(task_control_block::priority_is_lower, task);
	}
	else if (task.waiting_ != nullptr)
	{
		task.waiting_->remove_waiting(task);
		task.waiting_->add_waiting(task);
	}
}

void __attribute__((section(".text.opsy.inheritpriority"))) scheduler::inherit_priority(task_control_block& owner, task_priority priority)
{
	// Follows the chain: the owner may itself be blocked on a mutex held by
	// someone else, who must be raised too, or the task at the end of the
	// chain still gets starved. Bounded so a cycle cannot spin forever —
	// a cycle is a deadlock, caught by the assert in the service call.
	auto* current = &owner;

	for (std::size_t depth = 0; current != nullptr && depth < max_inheritance_depth; ++depth)
	{
		if (!(priority < current->priority_))
			break; // already at least this important, and so is the rest of the chain

		current->priority_ = priority;
		hooks::task_priority_changed(*current);

		requeue(*current);

		current = current->blocked_on_ != nullptr ? current->blocked_on_->owner_ : nullptr;
	}
}

bool __attribute__((section(".text.opsy.releaseallmutexes"))) scheduler::release_all_mutexes(task_control_block& task)
{
	bool woke = false;

	// A killed task never unwinds, so anything it held would stay locked for
	// good — and with a blocking mutex that is a permanent deadlock, not a
	// leak. Release them here and wake whoever was waiting.
	auto it = locked_mutexes_.begin();

	while (it != locked_mutexes_.end())
	{
		auto& held = *it;
		++it;

		if (held.owner_ != &task)
			continue;

		locked_mutexes_.erase(held);
		held.owner_ = nullptr;
		hooks::mutex_released(held, task); // a tracer must see every take paired

		woke |= hand_over(held);
	}

	return woke;
}

void __attribute__((section(".text.opsy.updatepriority"))) scheduler::update_priority(task_control_block& task, task_priority new_priority)
{
	auto previous = cortex_m::set_basepri(service_call_priority); // get a lock up to service call

	task.base_priority_ = new_priority;

	// Recomputed, not assigned: the task may be holding a mutex a more
	// important one is waiting for, and lowering it here would re-open the
	// very inversion the inheritance exists to close. recompute_priority
	// takes the stronger of the requested priority and every donation still
	// owed to it.
	(void) recompute_priority(task);

	if(task.is_started()) // check task is started
	{
		if(&task == current_task_.load(std::memory_order_relaxed)
			|| &task == next_task_.load(std::memory_order_relaxed)) // task is current or next to switch to
			do_switch(); // ask for a switch, this will compare priority again with other ready tasks
		else if(task.waiting_ != nullptr) // task is waiting a condition variable
			requeue(task);
		else if(task.blocked_on_ != nullptr)
		{
			// Blocked on a mutex, so in no list to re-sort — but the holder
			// may now owe it more than before, and the chain has to hear it.
			inherit_priority(*task.blocked_on_->owner_, task.priority_);
		}
		else if(task.is_ready())
		{
			// is_ready() rather than a hand-rolled condition: it is the one
			// predicate that knows every state a task can be in, next_task_
			// and blocked_on_ included.
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
	auto* previous = previous_task_.load(std::memory_order_relaxed);
	auto* next     = next_task_.load(std::memory_order_relaxed);
	auto* idle     = idle_.load(std::memory_order_relaxed);

	if (previous != nullptr)
	{
		assert(psp >= previous->stack_base_); // Process stack pointer below the task stack base, stack overflow !
		assert(*previous->stack_base_ == task_control_block::dummy_pattern); // The lowest slot of the task stack has been modified, this shows a stack overflow
		previous->stack_pointer_ = psp;
		hooks::task_stopped(*previous);
	}

	if (idling_.load(std::memory_order_relaxed))
		idle->stack_pointer_ = psp;

	if (next == nullptr)
	{
		idling_.store(true, std::memory_order_relaxed);
		previous_task_.store(nullptr, std::memory_order_relaxed);
		result = reinterpret_cast<uint64_t>(idle->stack_pointer_);
		hooks::enter_idle();
	}
	else
	{
		idling_.store(false, std::memory_order_relaxed);
		previous_task_.store(next, std::memory_order_relaxed);
		current_task_.store(next, std::memory_order_relaxed);
		result = reinterpret_cast<uint64_t>(next->stack_pointer_);
		next->last_started_ = ticks_;
		next_task_.store(nullptr, std::memory_order_relaxed);

		// Only an isr_lock is re-acquired here: it is a BASEPRI level, which
		// belongs to the context being restored. A mutex is taken back when
		// the task is woken instead — ownership is not per-context, and
		// re-acquiring one can block, which PendSV cannot do.
		if (auto* const* held = std::get_if<isr_lock*>(&next->released_lock_))
		{
			result |= static_cast<uint64_t>((*held)->relock_from_pend_sv(critical_section(true))) << 32;
			critical_section_.store(true, std::memory_order_relaxed);
			next->released_lock_ = std::monostate{};
			hooks::mutex_restored_for_task(*next);
		}

		assert(next->is_started());
		hooks::task_started(*next);
	}

	if constexpr (cortex_m::has_stack_limit_regs)
		cortex_m::set_psplim(idling_.load(std::memory_order_relaxed) ? idle->stack_base_ : next->stack_base_);

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

		// Anything it still held would stay locked for good, and with a
		// blocking mutex that is a permanent deadlock for every waiter.
		const bool woke_someone = release_all_mutexes(task);
		bool holder_dropped = false;

		if (task.blocked_on_ != nullptr)
		{
			// It was waiting on someone else's mutex, so it was donating its
			// priority to the holder. Stop, or the holder keeps running raised
			// for the rest of its hold — preempting work that genuinely
			// outranks it.
			auto* const holder = task.blocked_on_->owner_;
			task.blocked_on_ = nullptr;
			if (holder != nullptr)
			{
				recompute_priority(*holder);
				requeue(*holder);
				holder_dropped = true;
			}
		}

		// A runnable-but-not-running task is linked into ready_, and nothing
		// above unlinks it. Done after the waiting_ branch on purpose: both
		// lists share the task_lists::waiting node pair, so the task must have
		// left the condition variable's list before this touches ready_.
		ready_.erase(task);

		if (&task == current_task_.load(std::memory_order_relaxed))
		{
			assert(!critical_section_.load(std::memory_order_relaxed));
			previous_task_.store(nullptr, std::memory_order_relaxed);
			current_task_.store(nullptr, std::memory_order_relaxed);
			task_switch = do_switch();
		}
		// A waiter handed a mutex may outrank whoever called kill(), and a
		// holder that lost the dying task's donation may now be outranked --
		// it may be the very task that called kill(). Nothing else here would
		// ask for the switch when the dying task was not the running one.
		// After the ready_.erase above, so the dying task cannot be elected.
		if (woke_someone || holder_dropped)
			task_switch = do_switch() || task_switch;

		hooks::task_terminated(task);
		break;
	}

	case service_call_number::sleep:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(!critical_section_.load(std::memory_order_relaxed)); // should not be called in critical section
		auto* current = current_task_.load(std::memory_order_relaxed);
		assert(current != nullptr); // cannot be called if there is no current task running

		const int64_t raw = static_cast<int64_t>(frame->r0) | (static_cast<int64_t>(frame->r1) << 32);
		auto delta = duration{raw + 1}; // add one because we want to wait at least the required time
		assert(delta.count() >= 0);
		current->wait_until_ = ticks_ + delta;
		timeouts_.insert_when(wakeup_after, *current);
		hooks::task_sleep(*current);
		current_task_.store(nullptr, std::memory_order_relaxed);
		task_switch = do_switch();
		break;
	}

	case service_call_number::context_switch:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(!critical_section_.load(std::memory_order_relaxed)); // should not be called in critical section
		task_switch = do_switch();
		break;
	}

	case service_call_number::wait:
	{
		assert(is_thread); // should not be called from non thread mode
		assert(frame->r0 != 0); // cannot be called with undefined condition variable
		auto* current = current_task_.load(std::memory_order_relaxed);
		assert(current != nullptr); // cannot be called if there is no current task running

		condition_variable* condition = reinterpret_cast<condition_variable*>(frame->r0);
		const int64_t raw = static_cast<int64_t>(frame->r1) | (static_cast<int64_t>(frame->r2) << 32);
		duration timeout{raw};
		// The lock to release was recorded by the caller before issuing the
		// service call, because which kind it is decides how it is released
		// and there is no register left to carry a discriminant.
		auto& released = current->released_lock_;

		if(timeout.count() >= 0)
		{
			current->wait_until_ = ticks_ + timeout;
			timeouts_.insert_when(wakeup_after, *current);
			// Just engaged on the previous line; use * not .value() to avoid
			// the bad_optional_access -> abort path under -fno-exceptions.
			hooks::task_wait_timeout(*current, *condition, *current->wait_until_);
			hooks::condition_variable_start_waiting(*condition, *current, timeout);
		}
		else
		{
			hooks::task_wait(*current, *condition);
			hooks::condition_variable_start_waiting(*condition, *current);
		}

		if (auto* const* isr = std::get_if<isr_lock*>(&released))
		{
			assert(critical_section_.load(std::memory_order_relaxed) == true); // should be in critical section
			(*isr)->release_from_service_call();
			(*isr)->critical_section_.disable();
			critical_section_.store(false, std::memory_order_relaxed);
			hooks::mutex_stored_for_task(*current);
		}
		else if (auto* const* held = std::get_if<mutex*>(&released))
		{
			// A real release, waiter and all: another task must be able to
			// take it while this one sleeps, which is the whole point of
			// waiting with a mutex held.
			release_mutex(**held, *current);
			hooks::mutex_stored_for_task(*current);
		}

		condition->add_waiting(*current);
		current->waiting_ = condition;

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
			current->stack_pointer_ = sp;
		}

		current_task_.store(nullptr, std::memory_order_relaxed);
		task_switch = do_switch();
		break;
	}

	}
	hooks::exit_service_call(task_switch);
}

/**
 * @brief Whether this task is sitting in the scheduler's ready list
 */
bool task_control_block::is_ready() const
{
	// next_task_ matters as much as current_task_: a task do_switch() has
	// elected is popped out of ready_ and sits in no list at all until PendSV
	// runs. Treating it as ready makes ready_.erase() a silent no-op and the
	// following insert link it a second time.
	return is_started()
		&& this != scheduler::current_task()
		&& this != scheduler::next_task()
		&& blocked_on_ == nullptr
		&& waiting_ == nullptr
		&& !wait_until_.has_value();
}

/**
 * @brief Configures storage + priority and starts the task with the given @p entry callback
 * @param stack_base Pointer to the base of the task's stack (provided by @c task<StackSize>)
 * @param stack_size Size of the stack, in @c stack_item increments
 * @param entry The @c callback the task will execute
 * @param name Optional name for the task
 * @return @c true if the task was successfully started, @c false if it was already active
 * @remark @c stack_base_, @c stack_size_ and @c priority_ are written here on
 *         first launch (see the BSS-placement note on the @c task_control_block
 *         default constructor). Sets up the initial stack so the task starts in
 *         @c task_starter, with @c scheduler::terminate_task wired as the link
 *         register so the task terminates cleanly when its entry callback
 *         returns. frame->lr points at the global label
 *         @c opsy_terminate_task_resume, exposed by the inline asm inside
 *         @c scheduler::terminate_task right before its @c SVC — no fragile
 *         "+ N" offset past the leading @c nop (which is kept solely so GDB
 *         renders the link return one instruction before the SVC).
 */
bool task_control_block::start_impl(stack_item* stack_base, std::size_t stack_size,
                                           callback<void(void)>&& entry, const char* name)
{
	assert(scheduler::is_os_callable()); // add_task mutates all_tasks_ and ready_

	if(active_.exchange(true)) // we put true in the boolean value, and were expecting false, so we return if exchange return true
		return false;

	stack_base_ = stack_base;
	stack_size_ = stack_size;
	base_priority_ = task_priority::lowest;
	priority_      = task_priority::lowest;   // no inheritance on a fresh task
	stop_requested_.store(false, std::memory_order_relaxed); // a reused slot starts clean

	entry_ = std::move(entry);
	name_ = name;

#ifndef NDEBUG
	std::fill(stack_base_, stack_base_ + stack_size_, dummy_pattern);
#endif

	stack_pointer_ = &stack_base_[stack_size_ - 1]; // this pointer is reserved to stop stack trace unwinding
	stack_base_[stack_size_ - 1] = 0; // keep this pointer to zero to stop stack trace

	// Round the top of the frame down to an 8-byte boundary before carving it
	// out. stack_base_ is 8-aligned (see stack_storage), but &stack_base_[N - 1]
	// is deliberately one word below the top, so the frame — 8 words, alignment
	// preserving — would otherwise start the task with SP at 4-mod-8. The word
	// reserved just above to stop stack-trace unwinding is left untouched.
	stack_pointer_ = reinterpret_cast<stack_item*>(
		reinterpret_cast<std::uintptr_t>(stack_pointer_) & ~std::uintptr_t{7});

	stack_pointer_ -= sizeof(stack_frame) / sizeof(stack_item);
	static_assert(sizeof(stack_frame) % 8 == 0, "the exception frame must preserve 8-byte stack alignment");
	assert(reinterpret_cast<std::uintptr_t>(stack_pointer_) % 8 == 0);
	const auto frame = reinterpret_cast<stack_frame*>(stack_pointer_);

	frame->psr = 1 << 24;
	frame->pc = reinterpret_cast<code_pointer>(task_starter);
	frame->lr = reinterpret_cast<code_pointer>(&opsy_terminate_task_resume); // address of the SVC instruction inside terminate_task — see the comment on terminate_task in scheduler.cpp for why this avoids a "+ 2" offset
	frame->r0 = reinterpret_cast<uint32_t>(this);

	stack_pointer_ -= sizeof(context) / sizeof(stack_item);
	const auto ctx = reinterpret_cast<context*>(stack_pointer_);
	ctx->control = cortex_m::control_thread_psp_privileged;
	ctx->lr = cortex_m::exc_return_thread_psp_basic;

	scheduler::add_task(*this);
	return true;
}

/**
 * @brief Terminates the task immediately
 * @return @c true if the task was running and has been signalled to terminate, @c false if it was already inactive
 * @remark Issues an @c SVC carrying @c service_call_number::terminate; the actual
 *         teardown happens in @c scheduler::service_call_handler.
 */
bool task_control_block::kill()
{
	assert(scheduler::is_os_callable()); // an ISR above OpSy must not enter the scheduler

	if(!is_started()) // can only terminate an active task
		return false;

	// "memory" clobber: see scheduler::trigger_hard_switch.
	asm volatile(
			"mov r0, %[task] \n\t"
			"svc %[immediate]"
			:
			: [immediate] "I" (scheduler::service_call_number::terminate), [task] "r" (this)
			: "r0", "memory");
	return true;
}

/**
 * @brief Updates the task priority
 * @param new_priority The new priority value
 * @remark No-op if the priority is unchanged. Otherwise delegates to
 *         @c scheduler::update_priority which may re-order ready/waiting lists
 *         and request a context switch.
 */
void task_control_block::priority(task_priority new_priority)
{
	// Setting a priority on a task that has not been started is lost: start
	// puts every task at task_priority::lowest, so the value written here
	// would be overwritten by the launch and never take effect. Nothing else
	// would report it -- the task simply runs at the wrong priority.
	assert(is_started());

	// Changing a priority re-sorts ready_, re-sorts the waiting list of a
	// condition variable, or walks the chain of mutex holders paying priority
	// inheritance -- all of it under a BASEPRI that does not mask an interrupt
	// more urgent than OpSy. Called from such a handler, it would edit those
	// lists while the scheduler is halfway through editing them itself.
	assert(scheduler::is_os_callable());

	// Compared against the requested priority, not the effective one: while
	// this task is boosted by a waiter, priority_ is the inherited value, and
	// comparing to it would silently drop a genuine change.
	if(new_priority == base_priority_)
		return;

	scheduler::update_priority(*this, new_priority);
}

void task_control_block::set_name(const char* name)
{
	// Naming a task that has not been started is lost: start takes the name
	// as an argument and writes it, so anything set beforehand is overwritten
	// by the launch. Pass the name to start instead.
	assert(is_started());

	// name_ is a plain pointer, not an atomic, and the scheduler's hooks read
	// it to label what they trace. Writing it from an interrupt handler more
	// urgent than OpSy would be a data race with any such read -- undefined
	// behaviour, whatever a given core happens to do with an aligned word.
	assert(scheduler::is_os_callable());

	name_ = name;
}

/**
 * @brief Trampoline executed at the start of every task
 * @param thisPtr Pointer to the @c task_control_block being started
 * @remark Calls the user-provided entry callback, then terminates the task
 *         via @c scheduler::terminate_task. The link register set up in
 *         @c start would also lead here, this trampoline is for the normal
 *         (non-fault) entry path.
 */
void task_control_block::task_starter(task_control_block* thisPtr)
{
	thisPtr->entry_();
	scheduler::terminate_task(thisPtr);
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
OPSY_NO_OPT void __attribute__((naked, section(".text.opsy.isr.pendsv"))) PendSV_Handler()
{
	// Note on %c[mask]: the `=expr` form of `ldr` (literal-pool load) wants
	// a bare expression, not an immediate. With the plain `%[mask]` token,
	// GCC and clang both substitute the value with the ARM immediate `#`
	// prefix, giving `ldr R1, =#64` — which GAS tolerates but clang's
	// integrated assembler rejects ("unknown token in expression"). The
	// `%c` output modifier strips the `#` and emits the bare integer,
	// producing `ldr R1, =64`, which both assemblers accept.
	asm volatile(
			"ldr R1, =%c[mask]\n\t"
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

OPSY_NO_OPT void __attribute__((naked, section(".text.opsy.isr.svc"))) SVC_Handler()
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
			// R4 rides along with LR purely for alignment: exception entry leaves
			// MSP 8-byte aligned (CCR.STKALIGN), and pushing a single register
			// would leave it 4-mod-8 across the bl below. AAPCS requires 8-byte
			// alignment at a public interface, and everything reachable from the
			// handler -- the user-supplied hooks in particular -- is compiled on
			// that assumption. R4 is callee-saved and restored right after, so
			// the choice of register is inert.
			"push {R4, LR} \n\t"
			"mov R3, LR \n\t"        // pass EXC_RETURN as 4th argument
			"bl %[handler] \n\t"
			"dsb \n\t"
			"isb \n\t"
			"pop {R4, LR} \n\t"
			"bx LR"
			:
			: [handler] "g" (opsy::scheduler::service_call_handler),
			  [psp_flag] "I" (opsy::cortex_m::exc_return_psp_flag),
			  [thread_flag] "I" (opsy::cortex_m::exc_return_thread_flag)
			: "r0", "r1", "r2", "r3");
}
}
