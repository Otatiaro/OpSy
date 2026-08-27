# now() must stay monotonic across the tick counter's low-word wrap.
#
# ticks_ is a 64-bit time_point, so reading it from task context is two ldr on
# Cortex-M. The high word only changes when the low word wraps — once every
# 2^32 ms, about 49.7 days — and that is the single window where a read
# interrupted by SysTick could return a torn value. Unreachable in a test that
# just runs; two instructions away from here.
#
# now() masks SysTick around the read, so this must hold.

set confirm off
set pagination off
set height 0

target remote :1234

# Let the scheduler come up and the suite start running.
break opsy_test_main
continue
delete

# Two ticks before the low word wraps.
set variable *(unsigned long long *)&'opsy::scheduler::ticks_' = 0xFFFFFFFE
printf "INFO ticks_ armed at 0x%llx, two ticks from the wrap\n", *(unsigned long long *)&'opsy::scheduler::ticks_'

# Stop on every tick across the boundary and check the counter never goes
# backwards. The write side is a single += on a 64-bit value; what this
# watches for is a high word that fails to carry, or a low word that wraps
# without it.
set $previous = *(unsigned long long *)&'opsy::scheduler::ticks_'
set $crossed = 0
set $regressions = 0
set $steps = 0

break SysTick_Handler
while $steps < 6
  continue
  set $now = *(unsigned long long *)&'opsy::scheduler::ticks_'
  if $now < $previous
    set $regressions = $regressions + 1
    printf "CHECK ticks_ went backwards: 0x%llx -> 0x%llx\n", $previous, $now
  end
  if ($previous & 0xFFFFFFFF) > ($now & 0xFFFFFFFF)
    set $crossed = 1
    printf "INFO low word wrapped: 0x%llx -> 0x%llx\n", $previous, $now
  end
  set $previous = $now
  set $steps = $steps + 1
end
delete

printf "CHECK crossed the wrap: %d (expected 1)\n", $crossed
printf "CHECK backwards steps: %d (expected 0)\n", $regressions

# The high word must have carried: past the wrap the counter is above 2^32.
set $final = *(unsigned long long *)&'opsy::scheduler::ticks_'
printf "CHECK ticks_ now 0x%llx, above 2^32: %d (expected 1)\n", $final, ($final > 0xFFFFFFFF)

if $regressions == 0 && $crossed == 1 && $final > 0xFFFFFFFF
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
