/* linker.ld — STM32F103C6T6 (32KB flash, 10KB SRAM).
 * Conservative layout fits both C6T6 (32KB/10KB) and C8T6 (64KB/20KB).
 *
 * Flash: 0x08000000 - 0x08007FFF (32KB)
 * RAM:   0x20000000 - 0x200027FF (10KB = 0x2800 bytes)
 * Stack: grows down from 0x20002800 (top of 10KB RAM).
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 10K
}

/* End of RAM = top of stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 0x20002800 */

SECTIONS
{
    /* ---- Code and read-only data ---- */
    .text :
    {
        /* Vector table must be first in flash */
        KEEP(*(.isr_vector))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ---- Initialised data: stored in flash, copied to RAM at startup ---- */
    .data :
    {
        _sdata = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* LMA (load address) of .data — where it lives in flash */
    _ldata = LOADADDR(.data);

    /* ---- Zero-initialised data (zeroed by startup code) ---- */
    .bss (NOLOAD) :
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* ---- Discard unwanted output sections ---- */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.eh_frame*)
        *(.comment)
        *(.note*)
    }
}
