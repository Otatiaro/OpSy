# The task elected for a switch must be in no list until PendSV installs it.
#
# WHY THIS EXISTS
#
# OpSy switches tasks in two steps. First scheduler::do_switch picks who runs
# next: it puts the outgoing task back into ready_, clears current_task_,
# takes the winner out of ready_, stores it in next_task_ and raises PendSV.
# Then PendSV -- the lowest-priority exception, so it runs only once nothing
# else is pending -- saves the outgoing context, restores the elected one and
# makes it current.
#
# Between those two steps the elected task is in no list at all: not in
# ready_, not current_task_, only next_task_. Anything that reasons about
# where a task lives has to account for that window, and two invariants keep
# it safe to reason about:
#
#   at PendSV entry     current_task_ is null -- do_switch has already handed
#                       the outgoing task back to ready_, so no task is
#                       claimed to be running while none is
#   when PendSV clears  current_task_ holds what next_task_ held at entry --
#   next_task_          the elected task is accounted for again, and the
#                       window is closed
#
# A task that ended up in neither next_task_ nor a list would simply never run
# again, and nothing in a running system would report it: the scheduler would
# keep working with one task silently lost. That is what this checks for.
#
# The elected task being null at entry is not a defect: do_switch also raises
# PendSV when ready_ has emptied, to switch to the idle task, and stores no
# next_task_ for it. There is no window to close in that case, and the
# scenario counts those switches separately rather than checking them.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# current_task_ and next_task_ are single pointers in the image's data. They
# are read through casts because the scheduler's static members carry no type
# information there.
#
# The handler is followed with a watchpoint rather than by single-stepping:
# stepping a few hundred instructions per switch over the remote protocol is
# slow enough to matter, and the watchpoint stops exactly at the instant of
# interest. It watches next_task_ specifically because the handler writes
# current_task_ first and clears next_task_ last, so the clear is the moment
# the window closes -- watching current_task_ instead would stop while
# next_task_ still held the elected task and report a window that is simply
# not closed yet.
#
# The breakpoint is on scheduler::pend_sv_handler, the C++ body, and not on
# PendSV_Handler, the assembly entry point that calls it. The entry point
# raises BASEPRI before calling, and until it does, SysTick -- which is more
# urgent than PendSV -- can preempt it and run the scheduler, electing a
# different task and writing next_task_ again. That is correct behaviour, and
# it makes the few instructions before the mask a place where the invariants
# below do not hold yet. Stopping in the C++ body puts every read here after
# the mask, where they do.
#
# It matters more under a debugger than it would on hardware: the emulator's
# clock keeps running while the debugger holds the CPU stopped, so ticks
# accumulate during a stop and fire as soon as it resumes.

set confirm off
set pagination off
set height 0

target remote :1234

# Run until the image has started the scheduler, so the switches observed
# below are the ones the test suite drives, not the initial startup switch.
break opsy_test_main
continue
delete

set $current_addr = (unsigned long)&'opsy::scheduler::current_task_'
set $next_addr = (unsigned long)&'opsy::scheduler::next_task_'

set $switches = 0
set $to_idle = 0
set $running_at_entry = 0
set $window_left_open = 0
set $wrong_task_installed = 0

break opsy::scheduler::pend_sv_handler
set $handler_bp = $bpnum

while $switches < 6
  continue
  set $switches = $switches + 1

  if *(unsigned long *)$current_addr != 0
    set $running_at_entry = $running_at_entry + 1
    printf "CHECK PendSV entered with current_task_=0x%x -- a task is still claimed to be running\n", *(unsigned long *)$current_addr
  end

  set $elected = *(unsigned long *)$next_addr

  if $elected == 0
    # Switching to the idle task: no election to close out.
    set $to_idle = $to_idle + 1
  else
    # Stop at the instant the handler clears next_task_.
    #
    # The breakpoint on the handler is disabled first. Left armed, the
    # continue below can stop on the *next* switch instead of on the
    # watchpoint -- and there next_task_ holds a freshly elected task and
    # current_task_ is null, which reads exactly like the failure this
    # scenario looks for. What it would be reporting is its own confusion
    # about which stop it got.
    disable $handler_bp
    watch *(unsigned long *)$next_addr
    set $window_wp = $bpnum
    continue
    delete $window_wp
    enable $handler_bp

    if *(unsigned long *)$next_addr != 0
      set $window_left_open = $window_left_open + 1
      printf "CHECK next_task_ changed to 0x%x instead of being cleared\n", *(unsigned long *)$next_addr
    end
    if *(unsigned long *)$current_addr != $elected
      set $wrong_task_installed = $wrong_task_installed + 1
      printf "CHECK PendSV elected 0x%x but installed 0x%x\n", $elected, *(unsigned long *)$current_addr
    end
  end
end
delete

printf "CHECK context switches observed: %d (expected 6)\n", $switches
printf "INFO of those, switches to the idle task: %d\n", $to_idle
printf "CHECK switches entered with a task still running: %d (expected 0)\n", $running_at_entry
printf "CHECK switches that left next_task_ set: %d (expected 0)\n", $window_left_open
printf "CHECK switches that installed a task other than the elected one: %d (expected 0)\n", $wrong_task_installed

# A run made entirely of idle switches would check nothing about the window,
# so it is a failure too: a silent pass would hide that the scenario never
# observed what it exists to observe.
if $running_at_entry == 0 && $window_left_open == 0 && $wrong_task_installed == 0 && $switches == 6 && $to_idle < 6
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
