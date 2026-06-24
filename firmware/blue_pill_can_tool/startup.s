/* startup.s — Cortex-M3 minimal startup for Blue Pill CAN Tool.
 * STM32F103C6T6: 32KB flash, 10KB SRAM (0x20000000-0x20002800).
 * App lives at 0x08002000 (after the 8KB CAN+UART bootloader).
 * Stack at top of 10KB SRAM = 0x20002800.
 * No HAL, no interrupts, no FPU.
 * SCB->VTOR is set to 0x08002000 so the NVIC uses this app's vector table
 * (required when jumped to from the bootloader at 0x08000000).
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

/* -----------------------------------------------------------------------
 * Vector table — must be first in flash at 0x08000000 (.isr_vector section).
 * Only Initial_SP and Reset_Handler are real; all others are 0.
 * No IRQ handlers — application runs with interrupts disabled.
 * ----------------------------------------------------------------------- */
    .section .isr_vector, "a", %progbits
    .type  _vectors, %object
    .global _vectors
_vectors:
    .word  _estack           /* 0x00: Initial stack pointer = end of 10KB SRAM */
    .word  Reset_Handler     /* 0x04: Reset                                     */
    .word  0                 /* 0x08: NMI                                       */
    .word  0                 /* 0x0C: HardFault                                 */
    .word  0                 /* 0x10: MemManage                                 */
    .word  0                 /* 0x14: BusFault                                  */
    .word  0                 /* 0x18: UsageFault                                */
    .word  0                 /* 0x1C: Reserved                                  */
    .word  0                 /* 0x20: Reserved                                  */
    .word  0                 /* 0x24: Reserved                                  */
    .word  0                 /* 0x28: Reserved                                  */
    .word  0                 /* 0x2C: SVCall                                    */
    .word  0                 /* 0x30: DebugMon                                  */
    .word  0                 /* 0x34: Reserved                                  */
    .word  0                 /* 0x38: PendSV                                    */
    .word  0                 /* 0x3C: SysTick                                   */
    /* No peripheral IRQ vectors — application is fully polling */
    .size  _vectors, . - _vectors

/* -----------------------------------------------------------------------
 * Reset_Handler — run at power-on / reset before main()
 * Steps:
 *   1. Disable interrupts (cpsid i) — we never use them
 *   2. Zero-fill .bss
 *   3. Copy .data LMA (flash) → VMA (RAM)
 *   4. Call main()
 *   5. Spin forever if main returns (shouldn't happen)
 * ----------------------------------------------------------------------- */
    .section .text.Reset_Handler, "ax", %progbits
    .type  Reset_Handler, %function
    .global Reset_Handler
Reset_Handler:
    /* Disable all interrupts at core level */
    cpsid  i

    /* Relocate the vector table to our actual start address (0x08002000).
     * SCB->VTOR is at 0xE000ED08. The bootloader sets it to 0x08000000
     * before jumping here; we must update it so the NVIC dispatches to
     * our handlers, not the bootloader's (now-invalid) vector table.
     * This is safe to do before RAM init since it only writes to a
     * Cortex-M3 System Control Block register. */
    ldr    r0, =0xE000ED08          @ SCB_VTOR
    ldr    r1, =0x08002000          @ our app base
    str    r1, [r0]

    /* Zero .bss section in RAM */
    ldr    r0, =_sbss
    ldr    r1, =_ebss
    movs   r2, #0
bss_loop:
    cmp    r0, r1
    bge    bss_done
    str    r2, [r0], #4
    b      bss_loop
bss_done:

    /* Copy .data from flash (LMA = _ldata) to RAM (VMA = _sdata.._edata) */
    ldr    r0, =_sdata
    ldr    r1, =_edata
    ldr    r2, =_ldata
data_loop:
    cmp    r0, r1
    bge    data_done
    ldr    r3, [r2], #4
    str    r3, [r0], #4
    b      data_loop
data_done:

    /* Call main — application never returns in normal operation */
    bl     main

    /* Safety spin if main ever returns */
spin:
    b      spin

    .size  Reset_Handler, . - Reset_Handler
