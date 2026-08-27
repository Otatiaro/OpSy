# scheduler::add_task must not be interruptible while its two lists disagree.
#
# WHY THIS EXISTS
#
# Starting a task links it into two lists: all_tasks_, which holds every task
# that exists, and ready_, which holds the ones eligible to run. Between the
# two insertions the scheduler's view is inconsistent -- the task exists but is
# not runnable, and, worse, one of the two lists is mid-stitch, with a node
# whose links point somewhere the list header does not agree with.
#
# SysTick runs the scheduler. If it fired in that window it would walk ready_
# and insert into it, on links that are half-written: a task can be dropped
# from the list, or the links closed into a cycle that makes the next walk
# never end. add_task therefore raises BASEPRI around both insertions.
#
# Running the code cannot show this either way. The window is a handful of
# instructions and QEMU is deterministic, so a tick never lands inside it by
# chance. This scenario single-steps through add_task and checks the invariant
# directly at every instruction: whenever the two lists disagree, SysTick must
# be masked.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# On Cortex-M a lower priority number means more urgent. BASEPRI holds off
# every exception whose priority is numerically greater than or equal to it,
# and BASEPRI = 0 means no masking at all. OpSy puts SysTick at 127, so any
# BASEPRI from 1 to 127 holds it off.
#
# all_tasks_ and ready_ are embedded_list objects, laid out as { Item* first_;
# uint32_t size_; } -- so the element count sits 4 bytes into each. They are
# read through casts because the scheduler's static members carry no type
# information in the image.
#
# The measure used below is the change in each count since add_task was
# entered, not the counts themselves: the two lists are not the same length in
# general (a blocked task is in all_tasks_ but not in ready_), while a single
# add_task call must add exactly one entry to each.

set confirm off
set pagination off
set height 0

target remote :1234

# Run until the image has started the scheduler, so what follows observes a
# task being added to a running system rather than during startup.
break opsy_test_main
continue
delete

break opsy::scheduler::add_task
continue
delete

set $all_at_entry = *(unsigned int *)((unsigned long)&'opsy::scheduler::all_tasks_' + 4)
set $ready_at_entry = *(unsigned int *)((unsigned long)&'opsy::scheduler::ready_' + 4)
printf "INFO entered add_task with all_tasks_=%d ready_=%d\n", $all_at_entry, $ready_at_entry

set $unmasked_in_window = 0
set $window_instructions = 0
set $steps = 0

# Step until both lists have gained the task, which is where add_task's
# critical region ends. The cap keeps a scenario that never reaches that state
# from hanging: it stops, and the check below fails on the count it did reach.
while $steps < 600
  stepi
  set $steps = $steps + 1

  set $added_to_all = *(unsigned int *)((unsigned long)&'opsy::scheduler::all_tasks_' + 4) - $all_at_entry
  set $added_to_ready = *(unsigned int *)((unsigned long)&'opsy::scheduler::ready_' + 4) - $ready_at_entry

  if $added_to_all != $added_to_ready
    # The lists disagree: this is the window. SysTick must be masked here.
    set $window_instructions = $window_instructions + 1
    if $basepri == 0 || $basepri > 127
      set $unmasked_in_window = $unmasked_in_window + 1
      printf "CHECK lists disagree (all +%d, ready +%d) with BASEPRI=%d -- SysTick can run here\n", $added_to_all, $added_to_ready, $basepri
    end
  end

  if $added_to_all == 1 && $added_to_ready == 1
    loop_break
  end
end

printf "CHECK instructions spent with the two lists disagreeing: %d (expected at least 1)\n", $window_instructions
printf "CHECK of those, instructions with SysTick unmasked: %d (expected 0)\n", $unmasked_in_window
printf "CHECK task landed in both lists after %d instructions: %d (expected 1)\n", $steps, ($added_to_all == 1 && $added_to_ready == 1)

# A run that never opened the window proves nothing, so it is a failure too:
# it means the scenario stepped past add_task without observing what it exists
# to observe, and a silent pass would hide that.
if $unmasked_in_window == 0 && $window_instructions > 0 && $added_to_all == 1 && $added_to_ready == 1
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
