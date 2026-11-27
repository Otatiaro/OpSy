# A notification and the timeout of the same wait, at the same instant.
#
# WHAT IS UNDER TEST
#
# A task waits on a condition variable with a deadline: it is in the
# condition's waiting list and in the scheduler's timeout list at once. Two
# things can end the wait, and they run at different levels. The notifier
# holds the condition's isr_lock, which masks the handlers that may call OpSy
# but not OpSy's own level: SysTick, where the deadline expires, can still
# preempt it. If it does between the notifier taking the task off the waiting
# list and scheduler::wake_one masking SysTick out, the tick sees a task whose
# deadline passed and makes it ready; then the notifier, resuming, makes it ready
# a second time. A node linked into a list twice points at itself, and the
# next insertion into that list walks it forever. Seen on an STM32H523 with
# an interrupt releasing a semaphore at 1 kHz, aligned on the millisecond
# the deadline fell on.
#
# Running the code cannot show it: the window is a few instructions wide and
# the tick has to land inside it. This scenario puts it there by hand: it
# stops at scheduler::wake_one, before its first instruction raises the mask,
# moves the clock past the waiter's deadline and pends SysTick. Resuming runs
# SysTick first, which expires the wait, then the notifier on the same task.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# The case used is try_acquire_for_takes_a_release_that_comes_in_time: one
# waiter with a 200 ms deadline, released by the runner after 5 ms. The stop
# at wake_one is that release.
#
# ticks_ is the scheduler's clock, a 64-bit count of milliseconds; the
# waiter's deadline is 200 ticks after it started waiting, so adding 1000
# puts every pending deadline in the past. ICSR.PENDSTSET (bit 26 at
# 0xE000ED04) pends SysTick, and on resume it is taken at once: nothing that
# runs before wake_one's first instruction masks it.
#
# The verdict is the number of tasks in ready_, read from the list's size
# field once wake_one is done with the lists: on entry to PendSV, which the
# wake pends for the waiter and which runs as soon as wake_one lowers its
# mask, or when wake_one returns if nothing was pended. ready_ holds every
# runnable task, so the runner is there, and the waiter belongs there exactly
# once: two tasks. The field is read through a cast at its offset, not by
# type: the size follows the head pointer.
#
# The image is killed rather than resumed: a list with a node linked twice is
# not worth running on.

set confirm off
set pagination off
set height 0

target remote :1234

break try_acquire_for_takes_a_release_that_comes_in_time
continue
delete
printf "INFO in the timed wait case\n"

break opsy::scheduler::wake_one
continue
delete
printf "INFO stopped at wake_one for the release, before it masks SysTick\n"

# Where this call returns to, so the lists can be read once it is done.
up
set $caller = $pc
frame 0

set $ticks_addr = (unsigned long)&'opsy::scheduler::ticks_'
set $ticks = *(long long *)$ticks_addr
set *(long long *)$ticks_addr = $ticks + 1000
printf "INFO moved the clock from %lld to %lld, past the waiter's deadline\n", $ticks, $ticks + 1000

set *(unsigned long *)0xE000ED04 = (1 << 26)
printf "INFO pended SysTick\n"

break PendSV_Handler
break *$caller
continue
delete

# A build with assertions dies here instead: the wake finds a task the tick
# already took off the condition. The same race, caught early.
if $_isvoid($_exitcode) == 0
  printf "CHECK the image exited (code %d) before wake_one was done: an assertion caught the tick and the notification on the same task\n", $_exitcode
  printf "SCENARIO: FAIL\n"
else
  set $ready_size = *(unsigned int *)((unsigned long)&'opsy::scheduler::ready_' + 4)
  printf "CHECK tasks in ready_ after the tick and the notification: %d (expected 2)\n", $ready_size

  if $ready_size == 2
    printf "SCENARIO: PASS\n"
  else
    printf "SCENARIO: FAIL\n"
  end
  kill
end
