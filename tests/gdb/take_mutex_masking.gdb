# Checks that scheduler::take_mutex is never entered with SysTick unmasked.
#
# WHY THIS EXISTS
#
# take_mutex writes mutex::owner_ and links the mutex into
# scheduler::locked_mutexes_. Two paths reach it: a task calling mutex::lock,
# and the SysTick handler, which walks expired timeouts and can hand a mutex
# to a task whose wait just ran out
# (systick_handler -> resume_waiter -> take_mutex).
#
# Were a task to run take_mutex without masking SysTick, the two paths could
# overlap: the task reads owner_ == nullptr, SysTick fires and gives the mutex
# to the timed-out waiter, then the task claims it as well. The mutex ends up
# with two owners and linked into locked_mutexes_ twice.
#
# The scheduler's own critical section does NOT prevent this. It only makes
# do_switch() decline to change tasks; it masks no interrupt, so SysTick still
# runs. Raising BASEPRI is what closes the window.
#
# Running the code cannot show this either way: the window is two instructions
# wide and QEMU is deterministic, so an interrupt never lands in it by chance.
# This scenario stops the CPU inside take_mutex, reads BASEPRI directly, and
# then forces a SysTick to confirm it really is held off.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# On Cortex-M a lower priority number means more urgent. BASEPRI holds off
# every exception whose priority is numerically greater than or equal to it,
# and BASEPRI = 0 means no masking at all. OpSy puts SysTick at 127 and the
# service call at 64, so any BASEPRI from 1 to 127 holds SysTick off.
#
# ICSR, the Interrupt Control and State Register, sits at 0xE000ED04. Writing
# bit 26 (PENDSTSET) makes SysTick pending by hand.
#
# Note this checks take_mutex at *entry*, because it is a function that must
# be called with the masking already in place. Functions that raise BASEPRI
# themselves — add_task, for one — read 0 at entry, which is correct for them.

set confirm off
set pagination off
set height 0

target remote :1234

set $failures = 0

# Run until the image has started the scheduler, so what follows observes the
# running system rather than the startup code.
break opsy_test_main
continue
delete

# Stop on the first mutex acquisition the suite performs.
break opsy::scheduler::take_mutex
continue

printf "INFO stopped at entry to take_mutex\n"

if $basepri == 0 || $basepri > 127
  printf "CHECK take_mutex entered with BASEPRI=%d -- SysTick is NOT masked, so it can re-enter here\n", $basepri
  set $failures = $failures + 1
else
  printf "CHECK take_mutex entered with BASEPRI=%d, which holds SysTick (127) off\n", $basepri
end

# Make SysTick pending while the CPU sits inside take_mutex. Masked, it must
# stay pending; unmasked it would run at once and re-enter the scheduler. The
# tick counter is the witness: SysTick increments it, so if it moves while a
# single instruction is stepped, the handler ran when it should not have.
set *(unsigned int *)0xE000ED04 = (1 << 26)
set $ticks_before = *(unsigned long long *)&'opsy::scheduler::ticks_'
stepi

if *(unsigned long long *)&'opsy::scheduler::ticks_' != $ticks_before
  printf "CHECK SysTick ran inside take_mutex -- the region is not masked\n"
  set $failures = $failures + 1
else
  printf "CHECK SysTick stayed pending inside take_mutex\n"
end

delete

printf "CHECK unmasked entries: %d (expected 0)\n", $failures

if $failures == 0
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
