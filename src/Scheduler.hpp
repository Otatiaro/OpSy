/**
 ******************************************************************************
 * @file    Scheduler.hpp
 * @author  Thomas Legrand
 * @version V0.1
 * @date    01-March-2019
 * @brief   The OpSy scheduler.
 *
 * 			The scheduler is responsible for task switching, timeouts, and
 * 			globally makes the link between all OpSy elements.
 *
 * 			@c Task will not run until the @c Scheduler is started.
 * 			When the @c Scheduler starts, it does NOT return from @c start.
 * 			Instead it starts running the most important @c Task or goes to
 * 			idle if there is no @c Task ready to run.
 *
 * 			It also offers the @c now method, which returns the @c time_point
 * 			since the @c Scheduler started.
 *
 * 			You can iterate over all the @c Task currently handled by the
 * 			@c Scheduler with @c allTasks
 *
 ******************************************************************************
 * @copyright Copyright 2019 EZNOV SAS under the MIT License
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************
 * @see https://github.com/Otatiaro/OpSy
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <ratio>
#include <cassert>
#include <atomic>

#include "Config.hpp"
#include "Task.hpp"
#include "ConditionVariable.hpp"
#include "Hooks.hpp"

extern "C" void SysTick_Handler();
extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();

namespace opsy
{

/**
 * @brief The @c Scheduler is the OpSy manager, it handles @c Task switch, timeouts, etc.
 * @remark You can start @c Task before the @c Scheduler is started, but they will not be executed until the @c Scheduler is started
 */
class Scheduler
{
	friend void ::SysTick_Handler();
	friend void ::PendSV_Handler();
	friend void ::SVC_Handler();
	friend void sleep_for(duration t);
	friend class TaskControlBlock;
	friend class CriticalSection;
	friend class ConditionVariable;

public:

	/**
	 * @brief The Service Call @c IsrPriority, it is set to system preemption and most important sub-priority
	 */
	static constexpr auto kServiceCallPriority = IsrPriority::FromPreemptSub<kPreemptionBits>(kOpsyPreemption, 0);

	/**
	 * @brief The Systick @c IsrPriority, it is set to system preemption and least important sub-priority
	 * @remark Any interrupt service routine with priority above this one, or @c Mutex locks that locks higher priority, will NOT be able to use any of OpSy features
	 */
	static constexpr auto kSystickPriority = IsrPriority::FromPreemptSub<kPreemptionBits>(kOpsyPreemption, CortexM::minSub());

	/**
	 * @brief The PendSV @c IsrPriority, is it set to the minimum preemption and sub-priority possible
	 * @remark But as soon as it starts, it will lock anything up to the Service Call
	 */
	static constexpr auto kPendSvPriority = IsrPriority::FromPreemptSub<kPreemptionBits>(CortexM::minPreempt(), CortexM::minSub());

	/**
	 * @brief Starts the @c Scheduler
	 * @param idle The @c IdleTask to use when the system goes idle. If you don't provide one, the @c Scheduler will use a default one that uses @c WFI in a loop
	 * @return @c true if the @c Scheduler started, @c false otherwise (already started)
	 */
	static bool start(IdleTaskControlBlock& idle = DefaultIdle<>);

	/**
	 * @brief Gets a read only reference to the @c EmbeddedList of @c Task currently active
	 * @return A read only reference to the @c EmbeddedList of @c Task currently active
	 */
	static const EmbeddedList<TaskControlBlock, TaskLists::Handle>& allTasks()
	{
		assert(s_isStarted);
		return s_allTasks;
	}

	/**
	 * @brief Gets the current @c time_point
	 * @return The current @c time_point
	 */
	static inline time_point now()
	{
		assert(s_isStarted && CortexM::currentPriority().value_or(CortexM::kLowestPriority).value() >= kSystickPriority.value());
		return s_ticks;
	}

	/**
	 * @brief Try to get a valid @c CriticalSection from the @c Scheduler
	 * @return A @c CriticalSection with state @c true if possible, @c false otherwise (already in critical section)
	 * @remark Use this only for @c Task to @c Task synchronization, prefer @c Mutex for a more generic synchronization (uses @c IsrPriority to sychronize with interrupt service routines)
	 */
	static inline CriticalSection criticalSection()
	{
		if (s_criticalSection) // was already in critical section, iterative is OK but the new object is invalid, meaning the critical section is ended only when the first (the only valid) object is released
			return CriticalSection(false);
		else
		{
			Hooks::enterCriticalSection();
			return CriticalSection(s_criticalSection = true);
		}
	}

private:

	enum class ServiceCallNumber
		: uint8_t
		{
			Terminate, Sleep, Switch, Wait,
	};

	static bool s_isStarted;
	static time_point s_ticks;
	static EmbeddedList<TaskControlBlock, TaskLists::Handle> s_allTasks;
	static EmbeddedList<TaskControlBlock, TaskLists::Timeout> s_timeouts;
	static EmbeddedList<TaskControlBlock, TaskLists::Waiting> s_ready;
	static bool s_idling;
	static bool s_mayNeedSwitch;
	static volatile bool s_criticalSection;

	static IdleTaskControlBlock* s_idle;
	static TaskControlBlock* s_previousTask;
	static TaskControlBlock* s_currentTask;
	static TaskControlBlock* s_nextTask;

	static void addTask(TaskControlBlock& task)
	{
		Hooks::taskAdded(task);
		s_allTasks.push_front(task);
		s_ready.insertWhen(TaskControlBlock::priorityIsLower, task);
		if(s_isStarted)
			triggerSoftSwitch();
	}

	static void terminateTask(TaskControlBlock* task);

	static void triggerSoftSwitch()
	{
		auto previous = CortexM::setBasepri(kServiceCallPriority);
		s_mayNeedSwitch = false;
		assert(previous.value() == 0); // there is no reason to call this being in a mutex
		doSwitch(); // do the actual switch
		CortexM::setBasepri(previous); // and restore the basepri to its previous value
	}

	static __attribute__((always_inline)) void triggerHardSwitch()
	{
		asm volatile("svc %[immediate]" : : [immediate] "I" (ServiceCallNumber::Switch));
	}

	static constexpr bool wakeupAfter(const TaskControlBlock& left, const TaskControlBlock& right)
	{
		assert(left.m_waitUntil.has_value() && right.m_waitUntil.has_value());
		return left.m_waitUntil.value_or(Startup) < right.m_waitUntil.value_or(Startup);
	}

	static bool doSwitch();

	static void __attribute__((always_inline)) SystickHandler()
	{
		Hooks::enterSystick();
		s_ticks+=duration(1); // this is correct and "atomic" because nothing that has preemptive level above system should use it

		bool dirty = false;


		while(!s_timeouts.empty() && s_timeouts.front().m_waitUntil.value() <= s_ticks)
		{
			auto& task = s_timeouts.front();
			s_timeouts.pop_front();
			task.m_waitUntil = std::nullopt;

			if(task.m_waiting != nullptr)
			{
				task.m_waiting->removeWaiting(task);
				task.setReturnValue(static_cast<uint32_t>(std::cv_status::timeout)); // notify timeout to thread (write value to its R0 frame)
			}

			s_ready.insertWhen(TaskControlBlock::priorityIsLower, task);
			Hooks::taskReady(task);
			dirty = true;
		}

		if(dirty)
			Hooks::exitSystick(doSwitch());
		else
			Hooks::exitSystick(false);
	}

	static uint64_t pendSvHandler(uint32_t* psp);
	static void serviceCallHandler(StackFrame* frame, ServiceCallNumber parameter, bool isThread);
	static void wakeUp(TaskControlBlock& task, ConditionVariable& initiator);
	static void updatePriority(TaskControlBlock& task, Priority newPriority);

	static void updateName(TaskControlBlock& task)
	{
		Hooks::taskNameChanged(task);
	}

	static void criticalSectionEnd()
	{
		assert(s_criticalSection == true); // should be in critical section
		s_criticalSection = false;
		Hooks::exitCriticalSection();
		if (s_mayNeedSwitch)
			triggerSoftSwitch();
	}
};

}

