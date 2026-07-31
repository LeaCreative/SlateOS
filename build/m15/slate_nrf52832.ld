/* nRF52832 application linker script.
 *
 * FLASH origin:
 *   - 0x00000000 when BOOTLOADER_PRESENT=OFF for standalone bring-up
 *   - 0x00008000 when BOOTLOADER_PRESENT=ON to sit above a 32KB bootloader
 *
 * RAM layout:
 *   .data -> copied from flash to RAM by Reset_Handler
 *   .bss  -> zeroed by Reset_Handler
 *   heap  -> fixed reserve after .bss
 *   stack -> fixed reserve at top of RAM
 */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00008000, LENGTH = 0x00050000
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x00010000
}

ENTRY(Reset_Handler)

_stack_size = 0x00001000;
_heap_size  = 0x00000800;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(256);
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text.Reset_Handler)
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    __preinit_array_start = .;
    KEEP(*(.preinit_array*))
    __preinit_array_end = .;
  } > FLASH

  .init_array :
  {
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __init_array_end = .;
  } > FLASH

  _etext = .;

  _sidata = LOADADDR(.data);
  .data : AT(_etext)
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __heap_start__ = .;
    . += _heap_size;
    . = ALIGN(8);
    __heap_end__ = .;
  } > RAM

  .stack_dummy (NOLOAD) :
  {
    . = ALIGN(8);
    . += _stack_size;
    . = ALIGN(8);
  } > RAM

  __StackTop = ORIGIN(RAM) + LENGTH(RAM);
  __StackLimit = __StackTop - _stack_size;
  PROVIDE(__stack = __StackTop);

  ASSERT(__StackLimit >= __heap_end__, "RAM overflow: heap collides with stack")
}
