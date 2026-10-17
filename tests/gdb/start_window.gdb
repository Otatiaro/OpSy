# scheduler::start_impl must not be entered by OpSy while it is half-started.
#
# WHY THIS EXISTS
#
# Once is_started_ reads true, starting a task asks for a context switch:
# add_task runs do_switch, which edits ready_ and pends PendSV. Until
# start_impl has moved thread mode onto the process stack and run its own
# do_switch, neither is safe -- ready_ is being edited by start_impl itself,
# and a PendSV taken there saves a context through a PSP that points at
# nothing. An interrupt handler at OpSy's level that starts a task in that
# window would do exactly that.
#
# So from the moment is_started_ becomes true until start_impl has pended its
# first switch, every interrupt that may call into OpSy must be masked. This
# scenario single-steps start_impl and checks it at every instruction.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# On Cortex-M a lower priority number means more urgent. BASEPRI holds off
# every exception whose priority is numerically greater than or equal to it,
# and BASEPRI = 0 means no masking at all. The handlers allowed to call OpSy
# are those at or below the service call's priority, so the mask has to be
# non-zero and no greater than it. That priority is read back from SHPR2,
# where start_impl writes it, rather than written here: the emulated core
# implements fewer priority bits than the configuration names, and BASEPRI
# is truncated the same way.
#
# QEMU masks interrupts while single-stepping, so the PendSV start_impl pends
# is never taken under the scenario: it ends when ICSR reports it pending.

set confirm off
set pagination off
set height 0

target remote :1234

break opsy::scheduler::start_impl
continue
delete

set $violations = 0
set $window_instructions = 0
set $steps = 0
set $pend_sv_pending = 0

while $steps < 5000
  stepi
  set $steps = $steps + 1

  set $started = *(unsigned char *)&'opsy::scheduler::is_started_'
  set $pend_sv_pending = (*(unsigned int *)0xE000ED04 >> 28) & 1
  set $svc_priority = *(unsigned char *)0xE000ED1F

  if $started
    set $window_instructions = $window_instructions + 1
    if $basepri == 0 || $basepri > $svc_priority
      set $violations = $violations + 1
      if $violations <= 5
        printf "CHECK is_started_ true at pc=0x%x with BASEPRI=%d (service call at %d) -- an OpSy-level handler can enter here\n", $pc, $basepri, $svc_priority
      end
    end
  end

  if $pend_sv_pending
    loop_break
  end
end

printf "CHECK instructions with is_started_ true before the first switch: %d (expected at least 1)\n", $window_instructions
printf "CHECK of those, instructions with OpSy-level handlers unmasked: %d (expected 0)\n", $violations
printf "CHECK first switch pended after %d instructions: %d (expected 1)\n", $steps, $pend_sv_pending

# A run that never reached the switch, or never saw the flag set, proves
# nothing: it is a failure, not a pass.
if $violations == 0 && $window_instructions > 0 && $pend_sv_pending
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
