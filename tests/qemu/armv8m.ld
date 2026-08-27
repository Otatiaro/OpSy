/*
 * Linker script for the ARMv8-M QEMU board: mps2-an505 (Cortex-M33).
 *
 * The core comes out of reset in the Secure state, where the 0x0 / 0x2
 * addresses the ARMv7-M boards use are Non-secure aliases. Booting an image
 * linked for them locks the CPU up on the first access:
 *   "Lockup: can't escalate 3 to HardFault (current priority -1)".
 *
 * Both regions below therefore live in the Secure ZBT SRAM at 0x10000000,
 * split in two. The board's separate SRAM at 0x30000000 is NOT used: it sits
 * behind a Memory Protection Controller that comes up blocking, so reaching it
 * would mean programming the SSE-200 MPCs first — the very first stack push
 * faults otherwise, before a single instruction of the image has run.
 */

MEMORY
{
  CODE (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
  RAM  (rwx) : ORIGIN = 0x10200000, LENGTH = 2M
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > CODE

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > CODE

  /* Static constructors. The test cases register themselves through
     file-scope objects, so without KEEP + a call to __libc_init_array the
     suite boots and reports zero cases. */
  .init_array :
  {
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
    . = ALIGN(4);
  } > CODE

  .fini_array :
  {
    . = ALIGN(4);
    __fini_array_start = .;
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array))
    __fini_array_end = .;
    . = ALIGN(4);
  } > CODE

  /* .data lives in RAM but is loaded from code memory, so the LMA follows
     .text. AT> CODE lets the linker place it; _sidata must be taken after the
     section exists, or LOADADDR resolves to 0 and every section overlaps. */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> CODE

  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  . = ALIGN(8);
  _estack = ORIGIN(RAM) + LENGTH(RAM);

  /* Discarded: nothing here unwinds. -fno-exceptions means no personality
     routine, and no code in the image references the ARM unwinder.
     If a link ever fails on __exidx_start / __exidx_end, something pulled
     unwind-arm.o in — find what rather than defining the symbols here.
     std::unique_lock is one such thing: its unlock() builds a
     std::system_error on the failure path. */
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
