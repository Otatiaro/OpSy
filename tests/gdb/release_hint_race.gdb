# A release reads the waiting list without the lock, and the tick takes a
# waiter off it in the middle of that read.
#
# WHAT IS UNDER TEST
#
# counting_semaphore::release and queue's wake look at the condition's
# waiting list before taking any lock: a waiter can only queue itself while
# holding the lock, and the lock masks every handler that may call OpSy, so
# the read cannot meet a waiter half queued. It can meet a waiter half
# removed: the deadline of a timed wait expires in SysTick, which runs above
# the lock's level and takes the task off the list in two writes, the head
# and the count. A release running from a task, or from an interrupt handler
# below SysTick, can be preempted between its reads of those two words.
#
# empty() asserts that the head and the count agree. Read across that
# removal they do not, and a build with assertions dies on it: a HardFault
# in the middle of a 1 kHz release from an interrupt, with the waiting task
# on a 1 ms deadline, seen on an STM32H523. The hint has to be one word,
# read whole: the count.
#
# Running the code cannot show it: the window is two instructions wide and
# the tick has to land inside it. This scenario puts it there by hand: it
# stops release on its first read of the list's head, moves the clock past
# the waiter's deadline and pends SysTick. Resuming runs the tick first,
# which removes the waiter, then the rest of the read.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# The case used is try_acquire_for_takes_a_release_that_comes_in_time: one
# waiter with a 200 ms deadline, released by the runner after 5 ms. The stop
# at release is that release, from the runner.
#
# The semaphore is release's this, in r0 on entry. Its waiting list sits at
# the offsets semaphore.hpp's layout gives: the count, then the condition
# variable, which holds its notification lock and then the list, head word
# then count word. They are read through casts at
# those offsets, not by type: the scenario states the layout it relies on.
#
# rwatch stops the CPU after a read of the head. Where that read happens is
# what tells the two versions apart, and BASEPRI says it: a read without the
# lock happens with BASEPRI clear, a read under wake_one's mask with it
# raised to the service call level. The verdict does not depend on it: a
# release that reads the count first never trips the watchpoint before
# wake_one, and wake_one reads under the mask, so the pended tick waits.
#
# ticks_ is the scheduler's clock, a 64-bit count of milliseconds; adding
# 1000 puts every pending deadline in the past. ICSR.PENDSTSET (bit 26 at
# 0xE000ED04) pends SysTick.
#
# The verdict: release returns, the image is still running, the list is
# empty and the count is back to zero -- the waiter took the release rather
# than time out, which is what the tick would have made it do had it got
# there first. A build with assertions that read across the removal exits
# instead, and the exit code is the verdict.

set confirm off
set pagination off
set height 0

target remote :1234

break try_acquire_for_takes_a_release_that_comes_in_time
continue
delete
printf "INFO in the timed wait case\n"

break opsy::counting_semaphore<8>::release
continue
delete

set $semaphore = $r0
set $first_addr = $semaphore + 12
set $size_addr = $semaphore + 16
printf "INFO stopped at release; the list holds %d waiter(s)\n", *(unsigned int *)$size_addr

if *(unsigned int *)$size_addr != 1
  printf "CHECK expected one waiter queued with its deadline, found %d\n", *(unsigned int *)$size_addr
  printf "SCENARIO: FAIL\n"
  kill
end

# Where this call returns to, so the lists can be read once it is done.
up
set $caller = $pc
frame 0

# Stop on the first read of the list's head.
rwatch *(unsigned long *)$first_addr
continue
delete $bpnum
printf "INFO stopped after a read of the list's head, BASEPRI=0x%x (0: without the lock; the window)\n", $basepri

set $ticks_addr = (unsigned long)&'opsy::scheduler::ticks_'
set $ticks = *(long long *)$ticks_addr
set *(long long *)$ticks_addr = $ticks + 1000
printf "INFO moved the clock from %lld to %lld, past the waiter's deadline\n", $ticks, $ticks + 1000

set *(unsigned long *)0xE000ED04 = (1 << 26)
printf "INFO pended SysTick\n"

tbreak *$caller
continue

if $_isvoid($_exitcode) == 0
  printf "CHECK the image exited (code %d) before release returned: the read saw the list half removed\n", $_exitcode
  printf "SCENARIO: FAIL\n"
else
  set $count = *(int *)$semaphore
  set $left = *(unsigned int *)$size_addr
  printf "CHECK the semaphore's count after the tick and the release: %d (expected 0: the waiter took it)\n", $count
  printf "CHECK waiters left on the semaphore: %d (expected 0)\n", $left

  if $count == 0 && $left == 0
    printf "SCENARIO: PASS\n"
  else
    printf "SCENARIO: FAIL\n"
  end
  kill
end
