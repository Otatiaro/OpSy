/*
 * Linker script for the ARMv7-M QEMU boards: mps2-an385 (Cortex-M3),
 * mps2-an386 (Cortex-M4) and mps2-an500 (Cortex-M7).
 *
 * All three expose ZBT SRAM for code at 0x00000000 and SRAM at 0x20000000.
 * The vector table is relocated to RAM at startup (see startup.cpp), so the
 * table placed here only has to be good enough to fetch the initial MSP and
 * the reset handler.
 */

MEMORY
{
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
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

  /* Kept, not discarded: libgcc's unwinder references __exidx_start and
     __exidx_end unconditionally, even with -fno-exceptions, as soon as
     anything drags unwind-arm.o in. Providing an empty table satisfies it
     without pulling in any actual unwinding. */
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > CODE

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > CODE
}
