/**
 ******************************************************************************
 * @file    mutex.hpp
 * @brief   Blocking mutual exclusion between tasks, with priority inheritance.
 *
 * @copyright Copyright (c) 2026 Thomas Legrand
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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

#include "opsy_assert.hpp"
#include "embedded_list.hpp"

namespace opsy
{

class task_control_block;
class scheduler;
class condition_variable;

/**
 * @brief Mutual exclusion between tasks, with the semantics of @c std::mutex
 *
 *        A @c mutex has an owner and blocks: a task calling @ref lock while
 *        another holds it is suspended until the mutex is released, and
 *        resumes owning it. Locks may be released in any order, a task may
 *        hold several at once, and the priority of a holder is raised while a
 *        more important task waits behind it.
 *
 *        Use it to protect state shared **between tasks**. To share state with
 *        an interrupt handler, use @ref isr_lock instead: an ISR cannot be
 *        suspended, so exclusion against one can only be masking, never
 *        blocking.
 *
 * @remark Satisfies @c Lockable , so @c std::lock_guard<opsy::mutex> ,
 *         @c std::unique_lock<opsy::mutex> and @c std::scoped_lock all work.
 *
 * @warning Not recursive, like @c std::mutex : a task locking one it already
 *          holds trips an assert in debug. There is no @c recursive_mutex .
 *
 * @warning @ref lock blocks, so it must be called from a task — never from an
 *          interrupt handler, which has nothing to suspend.
 */
class mutex : private embedded_node<mutex>
{
	friend class scheduler;
	// needs owner_ to assert the caller holds it before waiting
	friend class condition_variable;
	friend class embedded_list<mutex, mutex>;
	friend class embedded_iterator<mutex, mutex>;
	friend class embedded_const_iterator<mutex, mutex>;

public:

	constexpr mutex() = default;

	/**
	 * @brief Destroys the mutex, which must not be held
	 *
	 * @warning Destroying a locked mutex leaves the scheduler with a pointer
	 *          to storage that is going away, and any task waiting on it
	 *          blocked for good. Asserted in debug; free in release.
	 */
	~mutex()
	{
		assert(owner_ == nullptr);
	}

	// Neither copyable nor movable, exactly like std::mutex. Moving a held
	// mutex would leave the scheduler's list of locked mutexes and the owner's
	// view of it pointing at the old address.
	mutex(const mutex&) = delete;
	mutex& operator=(const mutex&) = delete;
	mutex(mutex&&) = delete;
	mutex& operator=(mutex&&) = delete;

	/**
	 * @brief Acquires the mutex, blocking until it is free
	 *
	 * @warning Must be called from a task. Blocking has no meaning in an
	 *          interrupt handler, and the assert says so.
	 *
	 * @warning Not recursive: locking one this task already owns is a defect,
	 *          not a no-op, and trips an assert in debug.
	 */
	void lock();

	/**
	 * @brief Acquires the mutex if it is free, without blocking
	 * @return @c true if the mutex is now owned by the calling task
	 *
	 * @remark This is the @c try_lock of @c Lockable , so
	 *         @c std::unique_lock<opsy::mutex>{m, @c std::try_to_lock} works.
	 */
	[[nodiscard]] bool try_lock();

	/**
	 * @brief Releases the mutex and hands it to the highest-priority waiter
	 *
	 * @warning Must be called by the task that owns it. Releasing one owned by
	 *          another task is a defect and trips an assert in debug —
	 *          @c std::mutex calls the same thing undefined behaviour.
	 */
	void unlock();

	/**
	 * @brief Whether the mutex is currently held by anyone
	 * @return @c true if some task owns it
	 *
	 * @warning Informational only, and stale the instant it returns in the
	 *          presence of preemption. To take the mutex, use @ref try_lock ,
	 *          which decides and acts atomically.
	 */
	[[nodiscard]] bool is_locked() const
	{
		return owner_ != nullptr;
	}

private:

	// The single source of truth for "who holds this mutex". Everything else
	// about the relation is derived: the tasks waiting on it are the ones in
	// scheduler::all_tasks() whose blocked_on_ points here, and the mutexes a
	// task holds are the ones in scheduler::locked_mutexes_ owned by it.
	// Nothing is stored twice, so nothing can disagree.
	task_control_block* owner_ = nullptr;
};

}
