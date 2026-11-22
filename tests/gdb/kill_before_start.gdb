# task_control_block::kill refuses to run before scheduler::start.
#
# WHY THIS EXISTS
#
# kill issues a service call, and the service call handler is OpSy's only
# once the scheduler has started: before that the SVC lands in whatever the
# vector table holds, a HardFault on a bare image. A program that starts its
# tasks, kills one from main on some condition, and only then starts the
# scheduler would die there in release, with nothing to say why. kill
# therefore asserts scheduler::is_started() first, and this scenario calls it
# at the one moment the assertion must fire.
#
# WHAT IT DOES
#
# No test case can call kill before start, since the suite runs from a task.
# The scenario arranges the call itself: it stops the image on its way into
# scheduler::start_impl, where the runner task is started and the scheduler is
# not, and redirects the CPU into kill as if main had called it -- r0 the
# task, lr the place to come back to, pc the function. Where the CPU stops
# next is the verdict: in opsy::trap, called from kill, is the assertion;
# back at the return address is a kill that went through, which on an image
# that has not started means the SVC was issued into the void.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# Setting pc to a Thumb function's address and lr to an odd return address is
# how a call looks from the registers; the breakpoint we return to is the one
# we came from. opsy::trap is its own function in a build with no -O, which
# is the only kind the scenarios run against. $_hit_bpnum is the breakpoint
# the last stop hit.

set confirm off
set pagination off
set height 0

target remote :1234

break opsy::scheduler::start_impl
continue

set $return_site = (unsigned long)$pc
set $started = *(unsigned char *)((unsigned long)&'opsy::scheduler::state_' + 16)
printf "CHECK scheduler not started on entry to start_impl: %d (expected 0)\n", $started

# The call main could have made: g_runner.kill(), with the task started.
set $r0 = (unsigned long)(opsy::task_control_block *)&'(anonymous namespace)::g_runner'
set $lr = $return_site | 1
set $pc = (unsigned long)&'opsy::task_control_block::kill'

break opsy::trap
set $trap_bp = $bpnum
continue

set $trapped = ($_hit_bpnum == $trap_bp)
set $returned = ((unsigned long)$pc == $return_site)

# At trap's first instruction lr still says who called it.
set $kill_start = (unsigned long)&'opsy::task_control_block::kill'
set $caller = (unsigned long)$lr & ~1
set $from_kill = $trapped && ($caller >= $kill_start) && ($caller < $kill_start + 256)

printf "CHECK kill stopped in opsy::trap: %d (expected 1)\n", $trapped
printf "CHECK the trap was called from kill itself, not from a handler the SVC fell into: %d (expected 1)\n", $from_kill
printf "CHECK kill returned to its caller: %d (expected 0)\n", $returned

if $started == 0 && $trapped && $from_kill && !$returned
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
