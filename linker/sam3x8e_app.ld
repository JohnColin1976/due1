/* GNU ld script for ATSAM3X8E application
 * FLASH: 480K @ 0x00088000 (after 32K bootloader)
 * RAM  :  96K @ 0x20070000
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00088000, LENGTH = 480K
  RAM   (rwx) : ORIGIN = 0x20070000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    KEEP(*(.init))
    KEEP(*(.fini))
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx*)
  } > FLASH

  .ramfunc : AT (ADDR(.text) + SIZEOF(.text) + SIZEOF(.ARM.exidx))
  {
    _sramfunc = .;
    *(.ramfunc*)
    _eramfunc = .;
  } > RAM

  _siramfunc = LOADADDR(.ramfunc);

  .data : AT (LOADADDR(.ramfunc) + SIZEOF(.ramfunc))
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM

  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM

  /DISCARD/ :
  {
    *(.note*)
    *(.comment*)
  }
}
