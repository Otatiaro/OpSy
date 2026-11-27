# PendSV installs the task do_switch elected.
#
# WHY THIS EXISTS
#
# OpSy switches tasks in two steps. First scheduler::do_switch picks who runs
# next: the front of ready_, which holds every runnable task, the running one
# included. It stores the winner in next_task_ and raises PendSV, leaving
# current_task_ on the outgoing task, whose context is still on the core.
# Then PendSV -- the lowest-priority exception, so it runs only once nothing
# else is pending -- saves the outgoing context, restores the elected one and
# makes it current.
#
# Interrupts between the two steps may elect again. An election that lands
# on the running task cancels the pending switch (scheduler::stay) and
# leaves that task in next_task_: a PendSV that had already started, before
# its mask, then restores the context it saves rather than going idle. The
# scenario counts those switches to self; it does not fail on them.
#
# What it checks: when PendSV clears next_task_, current_task_ holds what
# next_task_ held at entry -- the elected task is the one that runs.
#
# The elected task being null at entry is not a defect: do_switch also raises
# PendSV when ready_ has emptied, to switch to the idle task, and stores no
# next_task_ for it. There is no installation to check in that case, and the
# scenario counts those switches separately rather than checking them.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# current_task_ and next_task_ are single pointers in the image's data, read
# through casts at their addresses, not by type, like every scheduler static
# here.
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

# The scheduler keeps what a switch touches in one object, scheduler::state_;
# the offsets below follow its layout, listed in scheduler.hpp next to it.
set $current_addr = (unsigned long)&'opsy::scheduler::state_' + 0
set $next_addr = (unsigned long)&'opsy::scheduler::state_' + 4

set $switches = 0
set $to_idle = 0
set $switch_to_self = 0
set $window_left_open = 0
set $wrong_task_installed = 0

break opsy::scheduler::pend_sv_handler
set $handler_bp = $bpnum

while $switches < 6
  continue
  set $switches = $switches + 1

  set $elected = *(unsigned long *)$next_addr

  if $elected != 0 && $elected == *(unsigned long *)$current_addr
    set $switch_to_self = $switch_to_self + 1
  end

  if $elected == 0
    # Switching to the idle task: no election to close out.
    set $to_idle = $to_idle + 1
  else
    # Stop at the instant the handler clears next_task_.
    #
    # The breakpoint on the handler is disabled first. Left armed, the
    # continue below can stop on the *next* switch instead of on the
    # watchpoint -- and there next_task_ holds a freshly elected task that
    # current_task_ does not, which reads exactly like the failure this
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
printf "INFO of those, switches of a task to itself: %d\n", $switch_to_self
printf "CHECK switches that left next_task_ set: %d (expected 0)\n", $window_left_open
printf "CHECK switches that installed a task other than the elected one: %d (expected 0)\n", $wrong_task_installed

# A run made entirely of idle switches would check nothing about the window,
# so it is a failure too: a silent pass would hide that the scenario never
# observed what it exists to observe.
if $window_left_open == 0 && $wrong_task_installed == 0 && $switches == 6 && $to_idle < 6
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
