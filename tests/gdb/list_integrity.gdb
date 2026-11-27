# The scheduler's task lists must stay coherent while the system runs.
#
# WHY THIS EXISTS
#
# OpSy keeps tasks in intrusive doubly-linked lists: the links live inside the
# task objects themselves, and each list header holds a pointer to its first
# element and a count. Nothing allocates, and nothing validates -- an insertion
# or removal that goes wrong does not fail, it leaves a list that still walks.
#
# The failure modes that follow are quiet ones. A task dropped from ready_
# never runs again and nothing reports it missing. A count that no longer
# matches the chain makes empty() lie, so the scheduler switches to the idle
# task with work outstanding, or walks off the end of one. Links stitched in
# one direction only survive every forward walk and corrupt the first removal
# from the middle. A chain closed into a cycle makes the next walk never end.
#
# None of that shows up as a test failure at the point it is caused: the tests
# keep passing until something much later behaves oddly. This scenario stops
# at tick after tick and checks the structures directly, which is where the
# damage is visible.
#
# WHAT IS CHECKED, at each stop, for the lists that hold tasks -- all_tasks_,
# which holds every task that exists, ready_, which holds the ones waiting
# for their turn to run, and the waiters of the mutex tests' two mutexes:
#
#   - walking forward from the first element reaches exactly as many elements
#     as the count says, and ends at null rather than at a cycle or at a link
#     that does not point at a task
#   - every element's back-link points at the element before it, and the
#     first element's back-link is null -- so the list is walkable in both
#     directions, which removal depends on
#   - the running task is in ready_, which holds every runnable task, unless
#     it is blocking and PendSV is pending to switch away from it: a running
#     task missing from ready_ would be lost at its next preemption
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# A list header is { Item* first_; uint32_t size_; }, so its count sits 4
# bytes in. Its elements are task_control_block objects, and each list uses a
# different pair of link fields inside them -- a task can be in several lists
# at once, so it carries one pair per list it can belong to. Which pair
# belongs to which list is a matter of layout, and the scenario states that
# layout: the lists are read through casts at fixed offsets, not by type.
#
# So rather than hard-coding offsets that a change to the task's declaration
# would silently invalidate, the scenario finds each list's pair by trying
# them: the right one is the pair whose chain has exactly the length the
# header claims and whose back-links all agree. That is only unambiguous on a
# list of at least two elements -- on a shorter one every unused pair is null
# and fits just as well -- so the scenario first waits for a tick at which
# ready_ holds two tasks. Such ticks are uncommon, since the test suite runs
# mostly one task at a time, hence the conditional breakpoint rather than a
# search loop: it lets the emulator run at full speed to the next one.

set confirm off
set pagination off
set height 0

target remote :1234

# Candidate link-field pairs inside a task, each 8 bytes: two pointers.
# Covering four is one more than the three lists a task can be in at once, so
# a pair added later is still found rather than silently missed.
set $pairs = 4

# Any chain longer than this is treated as broken rather than walked forever,
# which is what a chain closed into a cycle would otherwise cause.
set $cap = 64

# Candidate link fields are read out of task objects before it is known which
# pair is the right one, so most of what is read is not a pointer at all --
# following one would make GDB fault on an address that is not memory. Only
# values inside the image's RAM are followed; anything else means the
# candidate is wrong, or the chain is corrupt.
#
# The range is derived from a symbol known to live in RAM rather than written
# out, so it holds for any of the boards the suite runs on.
set $ram_low = ((unsigned long)&'opsy::scheduler::ready_') & 0xfff00000
set $ram_high = $ram_low + 0x00100000

# Whether $arg0 can be followed as a link to a task: a RAM address, and
# aligned, since a task object never sits on an odd address. Leaves the
# answer in $followable.
define is_followable
  set $candidate = $arg0
  set $followable = ($candidate >= $ram_low) && ($candidate < $ram_high) && (($candidate & 3) == 0)
end

# Try to identify which link-field pair $arg0's chain uses, leaving the byte
# offset in $found, or -1 if no pair fits. A pair fits when its chain is
# exactly as long as the header's count, ends at null, and has back-links that
# agree with it throughout.
define identify_links
  set $header = $arg0
  set $claimed = *(unsigned int *)($header + 4)
  set $found = -1
  set $matches = 0
  set $pair = 0
  while $pair < $pairs
    set $offset = $pair * 8
    set $node = *(unsigned long *)$header
    set $behind = 0
    set $seen = 0
    set $agrees = 1
    is_followable $node
    set $walkable = ($node == 0) || $followable
    while $node != 0 && $seen <= $cap && $walkable
      set $back = *(unsigned long *)($node + $offset)
      if $back != $behind
        set $agrees = 0
      end
      set $behind = $node
      set $node = *(unsigned long *)($node + $offset + 4)
      is_followable $node
      set $walkable = ($node == 0) || $followable
      set $seen = $seen + 1
    end
    if $walkable == 0
      set $agrees = 0
    end
    if $agrees == 1 && $node == 0 && $seen == $claimed
      set $found = $offset
      set $matches = $matches + 1
    end
    set $pair = $pair + 1
  end

  # More than one pair fitting means the answer would be a guess, so there is
  # no answer. On a list of two this is common rather than exotic: two
  # sleeping tasks are in the timeout list as well as in ready_, and in the
  # same order, so the timeout pair forms a chain of exactly the same length
  # whenever that happens. Answering only when one pair fits, and being asked
  # again at a later tick, is what makes the identification trustworthy.
  if $matches != 1
    set $found = -1
  end
end

# Walk $arg0's chain using the link offset in $arg1, leaving the number of
# elements reached in $walked, whether every back-link agreed in $linked_both,
# and whether the walk ended at null in $terminated.
define walk_list
  set $header = $arg0
  set $offset = $arg1
  set $node = *(unsigned long *)$header
  set $behind = 0
  set $walked = 0
  set $linked_both = 1
  is_followable $node
  set $walkable = ($node == 0) || $followable
  while $node != 0 && $walked <= $cap && $walkable
    if *(unsigned long *)($node + $offset) != $behind
      set $linked_both = 0
    end
    set $behind = $node
    set $node = *(unsigned long *)($node + $offset + 4)
    is_followable $node
    set $walkable = ($node == 0) || $followable
    set $walked = $walked + 1
  end
  set $terminated = ($node == 0)
end

# Whether the task in $arg1 appears in $arg0's chain, using the link offset in
# $arg2, leaving the answer in $present. The needle is passed as a variable,
# not as an expression: a macro's arguments are split on spaces, so an
# expression containing any would arrive as several arguments.
define chain_contains
  set $node = *(unsigned long *)$arg0
  set $needle = $arg1
  set $offset = $arg2
  set $present = 0
  set $seen = 0
  is_followable $node
  set $walkable = ($node == 0) || $followable
  while $node != 0 && $seen <= $cap && $walkable
    if $node == $needle
      set $present = 1
    end
    set $node = *(unsigned long *)($node + $offset + 4)
    is_followable $node
    set $walkable = ($node == 0) || $followable
    set $seen = $seen + 1
  end
end

set $ready_addr = (unsigned long)&'opsy::scheduler::ready_'
set $all_addr = (unsigned long)&'opsy::scheduler::all_tasks_'
# The scheduler keeps what a switch touches in one object, scheduler::state_;
# the offsets below follow its layout, listed in scheduler.hpp next to it.
set $current_addr = (unsigned long)&'opsy::scheduler::state_' + 0
# ICSR.PENDSVSET, bit 28 of the SCB's interrupt control and state register.
set $icsr_addr = 0xE000ED04

# Run until the image has started the scheduler, so the lists observed below
# are those of a running system rather than of the startup code.
break opsy_test_main
continue
delete

# Run on to a tick at which two tasks are queued, which is what makes the link
# fields identifiable. If the suite never reaches one the image simply runs to
# completion, and the scenario prints no verdict -- which the harness reports
# as a failure rather than reading as success.
break SysTick_Handler if *(unsigned int *)($ready_addr + 4) >= 2

set $ready_links = -1
set $all_links = -1
set $attempts = 0

# Asked again at each such tick until both answers are unambiguous. One stop
# is not always enough -- see identify_links -- and the states that make it
# ambiguous are transient.
while ($ready_links < 0 || $all_links < 0 || $ready_links == $all_links) && $attempts < 12
  continue
  set $attempts = $attempts + 1

  identify_links $ready_addr
  set $ready_links = $found
  identify_links $all_addr
  set $all_links = $found
end
delete

printf "INFO ready_ links at offset %d in a task, all_tasks_ links at offset %d\n", $ready_links, $all_links

if $ready_links < 0 || $all_links < 0 || $ready_links == $all_links
  printf "CHECK link fields identified for both lists, and distinct: 0 (expected 1)\n"
  printf "SCENARIO: FAIL\n"
  kill
end

# A mutex's waiters sit on the node ready_ uses, so their chain is checked
# with the links identified above. The two mutexes of the mutex tests are
# watched, at mutex.hpp's layout: the node of locked_mutexes_ (two pointers),
# owner_, then waiters_ (first, count).
set $mutex_a_waiters = (unsigned long)&'(anonymous namespace)::g_a' + 12
set $mutex_b_waiters = (unsigned long)&'(anonymous namespace)::g_b' + 12

set $stops = 0
set $count_mismatch = 0
set $one_way = 0
set $unterminated = 0
set $running_task_queued = 0
set $running_task_lost = 0
set $mutex_waiters_seen = 0

break SysTick_Handler
while $stops < 150
  continue
  set $stops = $stops + 1

  walk_list $ready_addr $ready_links
  if $walked != *(unsigned int *)($ready_addr + 4)
    set $count_mismatch = $count_mismatch + 1
    printf "CHECK ready_ says %d tasks, chain has %d\n", *(unsigned int *)($ready_addr + 4), $walked
  end
  if $linked_both == 0
    set $one_way = $one_way + 1
    printf "CHECK ready_ has a back-link that disagrees with the chain\n"
  end
  if $terminated == 0
    set $unterminated = $unterminated + 1
    printf "CHECK ready_ chain did not end at null: a cycle, or a link that is not a task\n"
  end

  walk_list $all_addr $all_links
  if $walked != *(unsigned int *)($all_addr + 4)
    set $count_mismatch = $count_mismatch + 1
    printf "CHECK all_tasks_ says %d tasks, chain has %d\n", *(unsigned int *)($all_addr + 4), $walked
  end
  if $linked_both == 0
    set $one_way = $one_way + 1
    printf "CHECK all_tasks_ has a back-link that disagrees with the chain\n"
  end
  if $terminated == 0
    set $unterminated = $unterminated + 1
    printf "CHECK all_tasks_ chain did not end at null: a cycle, or a link that is not a task\n"
  end

  # ready_ holds every runnable task, the running one included; only a task
  # that has just blocked is out of it, with PendSV pending to leave it.
  set $running = *(unsigned long *)$current_addr
  if $running != 0
    chain_contains $ready_addr $running $ready_links
    if $present == 0 && (*(unsigned int *)$icsr_addr & 0x10000000) == 0
      set $running_task_lost = $running_task_lost + 1
      printf "CHECK the running task 0x%x is out of ready_ with no switch pending\n", $running
    end
    if $present == 1
      set $running_task_queued = $running_task_queued + 1
    end
  end
end
delete

# The mutex tests run later than the ticks above reach, and a mutex has
# waiters only while one is contended: the waiters are looked at where they
# are read, on entry to hand_over, which unlock calls with the list intact.
set $hand_overs = 0
break opsy::scheduler::hand_over
while $hand_overs < 40
  continue
  set $hand_overs = $hand_overs + 1

  set $mutex_waiters = $mutex_a_waiters
  while $mutex_waiters != 0
    walk_list $mutex_waiters $ready_links
    if $walked != *(unsigned int *)($mutex_waiters + 4)
      set $count_mismatch = $count_mismatch + 1
      printf "CHECK a mutex says %d waiters, chain has %d\n", *(unsigned int *)($mutex_waiters + 4), $walked
    end
    if $linked_both == 0
      set $one_way = $one_way + 1
      printf "CHECK a mutex's waiters have a back-link that disagrees with the chain\n"
    end
    if $terminated == 0
      set $unterminated = $unterminated + 1
      printf "CHECK a mutex's waiters chain did not end at null: a cycle, or a link that is not a task\n"
    end
    if $walked != 0
      set $mutex_waiters_seen = $mutex_waiters_seen + 1
    end
    if $mutex_waiters == $mutex_a_waiters
      set $mutex_waiters = $mutex_b_waiters
    else
      set $mutex_waiters = 0
    end
  end
end
delete

printf "CHECK ticks inspected: %d (expected 150)\n", $stops
printf "CHECK ticks where a list's count disagreed with its chain: %d (expected 0)\n", $count_mismatch
printf "CHECK ticks where a back-link disagreed with the chain: %d (expected 0)\n", $one_way
printf "CHECK ticks where a chain did not end at null: %d (expected 0)\n", $unterminated
printf "CHECK ticks where the running task was out of ready_ with no switch pending: %d (expected 0)\n", $running_task_lost
# Zero would mean the membership check never saw the running task queued,
# and proved nothing; the same goes for the mutex chains below.
printf "CHECK ticks where the running task was found in ready_: %d (expected at least 1)\n", $running_task_queued
printf "CHECK hand-overs at which a mutex had waiters: %d (expected at least 1)\n", $mutex_waiters_seen

if $stops == 150 && $count_mismatch == 0 && $one_way == 0 && $unterminated == 0 && $running_task_lost == 0 && $running_task_queued > 0 && $mutex_waiters_seen > 0
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
