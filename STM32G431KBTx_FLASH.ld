ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
}

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >FLASH

  .text :
  {
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    _etext = .;
  } >FLASH

  .rodata :
  {
    *(.rodata)
    *(.rodata*)
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    _sdata = .;
    *(.data)
    *(.data*)
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + 0x400;
    . = . + 0x800;
    . = ALIGN(8);
  } >RAM
}
