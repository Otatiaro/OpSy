# Two tasks must never both believe they hold the scheduler's critical section.
#
# WHY THIS EXISTS
#
# scheduler::try_critical_section claims a single flag, critical_section_, and
# returns an object that says whether the claim succeeded; only the successful
# claimant releases the flag when its object goes out of scope.
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
# The result is read at the instruction try_critical_section returns to -- an
# address taken from the caller's stack frame on entry. Reading anywhere
# earlier would report whatever inner call last returned, not the claim.
#
# It is read *through* r0 rather than from it. The object returned owns the
# section and releases it when destroyed, so it has a destructor, and the ARM
# calling convention returns such a type in memory: the caller passes the
# address of the storage in r0 and gets that same address back in r0. So r0
# holds where the result is, and the byte it points at is the result -- true
# when the claim was granted. Reading r0 itself would read the low bit of an
# address, which is 0 on any aligned object, and would report a refusal
# whatever the code did.
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

break opsy::scheduler::try_critical_section
continue
delete

# Where this call returns to, so its result can be read there.
up
set $caller = $pc
frame 0

set $flag_addr = (unsigned long)&'opsy::scheduler::critical_section_'

if *(unsigned char *)$flag_addr != 0
  printf "INFO the section was already held on entry, nothing to race against\n"
  printf "SCENARIO: FAIL\n"
  kill
end

printf "INFO entered try_critical_section with critical_section_ free\n"

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

# Guard against reading through something that is not the result's address:
# without it a wrong assumption about the calling convention would quietly
# read a byte of whatever r0 happened to hold, and report a pass either way.
set $result_addr = $r0
set $ram_low = ((unsigned long)&'opsy::scheduler::critical_section_') & 0xfff00000
set $addressable = ($result_addr >= $ram_low) && ($result_addr < $ram_low + 0x00100000)

if $addressable == 0
  printf "CHECK the result was not returned where the calling convention says (r0=0x%x)\n", $result_addr
  printf "SCENARIO: FAIL\n"
  kill
end

set $granted = *(unsigned char *)$result_addr
printf "CHECK claim granted after a competing write: %d (expected 0)\n", $granted
printf "CHECK critical_section_ still held by the competitor: %d (expected 1)\n", *(unsigned char *)$flag_addr

if $granted == 0 && *(unsigned char *)$flag_addr == 1
  printf "SCENARIO: PASS\n"
else
  printf "SCENARIO: FAIL\n"
end

kill
