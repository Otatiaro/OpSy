/*
 * Linker script for the bench image on a NUCLEO-H563ZI: 2 MB of flash at
 * 0x08000000, 640 KB of SRAM at 0x20000000 (SRAM1 to SRAM3, contiguous).
 *
 * The same layout as the QEMU scripts next to it, with the board's memories:
 * the startup code is tests/qemu/startup.cpp unchanged, which copies .data
 * from flash, relocates the vector table to RAM and points VTOR at it.
 */

MEMORY
{
  CODE (rx)  : ORIGIN = 0x08000000, LENGTH = 2M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 640K
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

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
