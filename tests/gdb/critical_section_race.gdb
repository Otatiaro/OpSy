# Two tasks must never both believe they hold the scheduler's critical section.
#
# WHY THIS EXISTS
#
# scheduler::critical_section_begin claims a single flag, critical_section_,
# and returns whether the claim succeeded; only the successful claimant
# releases the flag, with critical_section_end, from isr_lock::unlock or from
# the destructor of the object try_critical_section wraps the claim in.
#
# Reading the flag and then writing it would be two separate steps, and the
# scheduler runs from SysTick, which can land between them. Task A reads 0,
# SysTick switches to task B, B claims the section for itself and writes 1,
# then A -- still holding the 0 it read -- writes 1 as well and reports
# success. Both now believe they hold it, and the first release clears it for
# both, so the section stops excluding anything.
#
# The flag is therefore claimed with a single read-modify-write
# (std::atomic::exchange), which on Cortex-M compiles to an LDREX/STREX pair:
# the store fails if anything wrote the location since the load, and the code
# retries with the new value.
#
# Running the code cannot show the difference. The window is two instructions
# wide, QEMU is deterministic, and both versions behave identically unless a
# write lands exactly inside it. This scenario creates that write by hand: it
# stops the CPU at the instant the flag is read, sets it as a competing task
# would, and checks that the claim comes back refused rather than granted.
#
# WHAT YOU NEED TO KNOW TO READ IT
#
# critical_section_ is a one-byte flag in the image's data, read through a
# cast because the scheduler's static members carry no type information there.
#
# rwatch stops the CPU on a read of that byte -- so, on the load half of the
# read-modify-write, before the store half has run. That is exactly the window
# the race needs.
#
# QEMU's LDREX/STREX bookkeeping notices the value at the watched address
# changing, so the store half fails and the code retries, which is the
# behaviour under test. A version that merely loaded and stored would have
# nothing to notice, would overwrite the competing claim, and would report
# success -- which is the failure this scenario reports.
#
# The result is read at the instruction critical_section_begin returns to --
# an address taken from the caller's stack frame on entry. Reading anywhere
# earlier would report whatever inner call last returned, not the claim.
#
# It is a bool, so the ARM calling convention returns it in r0: 1 when the
# claim was granted. Anything but 0 or 1 there means the convention was not
# what this scenario assumes, and the check says so rather than read on.
#
# This scenario leaves critical_section_ set with nobody owning it, so it is
# the last thing it does -- the image is killed rather than resumed.

set confirm off
set pagination off
set height 0

target remote :1234

# Run until the image has started the scheduler, so the claim observed below
# happens in a running system.
break opsy_test_main
continue
delete

break opsy::scheduler::critical_section_begin
continue
delete

# Where this call returns to, so its result can be read there.
up
set $caller = $pc
frame 0

# The scheduler keeps what a switch touches in one object, scheduler::state_;
# the offsets below follow its layout, listed in scheduler.hpp next to it.
set $flag_addr = ((unsigned long)&'opsy::scheduler::state_' + 19)

if *(unsigned char *)$flag_addr != 0
  printf "INFO the section was already held on entry, nothing to race against\n"
  printf "SCENARIO: FAIL\n"
  kill
end

printf "INFO entered critical_section_begin with critical_section_ free\n"

# Stop on the load half of the read-modify-write.
rwatch *(unsigned char *)$flag_addr
continue
delete $bpnum

printf "INFO stopped on the read of critical_section_\n"

# Stand in for a task that claims the section in the window between the read
# and the write.
set *(unsigned char *)$flag_addr = 1
printf "INFO wrote critical_section_=1, as a competing claimant would\n"

tbreak *$caller
continue

# Guard against reading something that is not the result: a wrong assumption
# about the calling convention would otherwise read whatever r0 happened to
# hold, and report a pass or a failure either way.
set $granted = $r0

if $granted != 0 && $granted != 1
  printf "CHECK the result was not returned where the calling convention says (r0=0x%x)\n", $granted
  printf "SCENARIO: FAIL\n"
  kill
end

printf "CHECK claim granted after a competing write: %d (expected 0)\n", $granted
printf "CHECK critical_section_ still held by the competitor: %d (expected 1)\n", *(unsigned char *)$flag_addr

if $granted == 0 && *(unsigned char *)$flag_addr == 1
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
