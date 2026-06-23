/*
 * gd32e2_flash.ld — Linker script for GD32E230C8T6 (GD32E2 family, Cortex-M23).
 *
 * Motor-disabled smoke only.  Flash / SRAM sizes are for GD32E230C8T6 (64KB
 * Flash, 8KB SRAM).  Adjust LENGTH values for other GD32E23x variants.
 *
 * GD32E230C8T6 memory:
 *   Flash: 64KB at 0x08000000 - 2KB reserved for NVRAM at end
 *   SRAM:   8KB at 0x20000000
 *
 * NOTE: For Cortex-M23 (ARMv8-M Baseline) the toolchain is -mcpu=cortex-m23.
 * The linker script structure is identical to the Cortex-M3 version; only the
 * memory sizes and CPU flags differ.
 */

ENTRY(Reset_Handler)

_estack = 0x20002000;       /* end of 8KB SRAM */
_Min_Heap_Size  = 0x100;
_Min_Stack_Size = 0x200;
_Reserved_Space = 0x800;

MEMORY
{
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K - _Reserved_Space
    RESERVED_FLASH (rw) : ORIGIN = 0x08000000 + 64K - _Reserved_Space, LENGTH = _Reserved_Space
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP (*(.init))
        KEEP (*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH

    .myFlashSection :
    {
        . = ALIGN(2048);
        *(.myFlashSection)
        . = ALIGN(2048);
    } >RESERVED_FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    . = ALIGN(4);
    .bss :
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE (end = .);
        PROVIDE (_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
