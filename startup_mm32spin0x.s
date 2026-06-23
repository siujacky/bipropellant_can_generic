/**
 * startup_mm32spin0x.s — Minimal Cortex-M0 startup for MM32SPIN0x family.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  This startup is adapted from the ARM CMSIS template
 * (no GPL vendor library dependency).
 *
 * MM32SPIN0x is a Cortex-M0 (ARMv6-M).  Key differences:
 *   - Only 56 Thumb-1 instructions; no Thumb-2 32-bit encodings (except BL).
 *   - No hardware multiply beyond 32x32=32 (no UMULL/SMULL).
 *   - -mcpu=cortex-m0 toolchain flag required.
 *   - Vector table: same layout as M3 but fewer exception entries.
 *
 * Provides:
 *   - Cortex-M0 vector table (SP, Reset_Handler, fault/exception handlers).
 *   - Reset_Handler: calls SystemInit(), copies .data, clears .bss, calls main().
 *
 * Symbols required from the linker script (mm32spin0x_flash.ld):
 *   _estack    — initial stack pointer (end of SRAM).
 *   _sidata    — start of .data LMA (in Flash).
 *   _sdata     — start of .data VMA (in SRAM).
 *   _edata     — end of .data VMA.
 *   _sbss      — start of .bss.
 *   _ebss      — end of .bss.
 */

    .syntax unified
    .cpu cortex-m0
    .thumb

/* -------------------------------------------------------------------
 * Stack top: provided by linker script symbol _estack.
 * ------------------------------------------------------------------- */
    .word _estack

/* -------------------------------------------------------------------
 * Minimal Cortex-M0 vector table.
 * ------------------------------------------------------------------- */
    .section .isr_vector, "a", %progbits
    .type g_pfnVectors, %object
g_pfnVectors:
    .word _estack               /* 0: Initial Stack Pointer         */
    .word Reset_Handler         /* 1: Reset Handler                 */
    .word NMI_Handler           /* 2: NMI Handler                   */
    .word HardFault_Handler     /* 3: Hard Fault Handler            */
    .word 0                     /* 4: Reserved                      */
    .word 0                     /* 5: Reserved                      */
    .word 0                     /* 6: Reserved                      */
    .word 0                     /* 7: Reserved                      */
    .word 0                     /* 8: Reserved                      */
    .word 0                     /* 9: Reserved                      */
    .word 0                     /* 10: Reserved                     */
    .word SVC_Handler           /* 11: SVCall Handler               */
    .word 0                     /* 12: Reserved                     */
    .word 0                     /* 13: Reserved                     */
    .word PendSV_Handler        /* 14: PendSV Handler               */
    .word SysTick_Handler       /* 15: SysTick Handler              */
    /* External interrupts — all default to Default_Handler
     * MM32SPIN0x IRQ layout (RM chapter 10); exact count from RM.
     * Using a conservative 32-entry IRQ table covering all SPIN0x variants. */
    .word Default_Handler       /* IRQ0:  WWDGT                     */
    .word Default_Handler       /* IRQ1:  PVD                       */
    .word Default_Handler       /* IRQ2:  RTC                       */
    .word Default_Handler       /* IRQ3:  FLASH                     */
    .word Default_Handler       /* IRQ4:  RCC                       */
    .word Default_Handler       /* IRQ5:  EXTI0_1                   */
    .word Default_Handler       /* IRQ6:  EXTI2_3                   */
    .word Default_Handler       /* IRQ7:  EXTI4_15                  */
    .word Default_Handler       /* IRQ8:  Reserved / USB or DMA     */
    .word Default_Handler       /* IRQ9:  DMA_CH1                   */
    .word Default_Handler       /* IRQ10: DMA_CH2_3                 */
    .word Default_Handler       /* IRQ11: DMA_CH4_5                 */
    .word Default_Handler       /* IRQ12: ADC_COMP                  */
    .word Default_Handler       /* IRQ13: TIM1_BRK_UP_TRG_COM       */
    .word Default_Handler       /* IRQ14: TIM1_CC                   */
    .word Default_Handler       /* IRQ15: TIM2                      */
    .word Default_Handler       /* IRQ16: TIM3                      */
    .word Default_Handler       /* IRQ17: TIM14                     */
    .word Default_Handler       /* IRQ18: Reserved                  */
    .word Default_Handler       /* IRQ19: TIM16                     */
    .word Default_Handler       /* IRQ20: TIM17                     */
    .word Default_Handler       /* IRQ21: I2C1                      */
    .word Default_Handler       /* IRQ22: I2C2                      */
    .word Default_Handler       /* IRQ23: SPI1                      */
    .word Default_Handler       /* IRQ24: SPI2                      */
    .word Default_Handler       /* IRQ25: USART1                    */
    .word Default_Handler       /* IRQ26: USART2                    */
    .word Default_Handler       /* IRQ27: Reserved                  */
    .word Default_Handler       /* IRQ28: Reserved                  */
    .word Default_Handler       /* IRQ29: Reserved                  */
    .word Default_Handler       /* IRQ30: Reserved                  */
    .word Default_Handler       /* IRQ31: Reserved                  */

/* -------------------------------------------------------------------
 * Reset_Handler: SystemInit() → copy .data → zero .bss → main()
 *
 * Cortex-M0 only supports Thumb-1 instructions (plus BL/BLX).
 * Avoid LDR Rn, [Rn, Rm] with large offsets; use ADDS for increments.
 * ------------------------------------------------------------------- */
    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr  r0, =SystemInit
    blx  r0

    /* Copy .data from Flash to SRAM */
    ldr  r0, =_sdata
    ldr  r1, =_edata
    ldr  r2, =_sidata
    movs r3, #0
    b    .data_copy_check
.data_copy_loop:
    ldr  r4, [r2, r3]
    str  r4, [r0, r3]
    adds r3, r3, #4
.data_copy_check:
    adds r4, r0, r3
    cmp  r4, r1
    bcc  .data_copy_loop

    /* Zero .bss */
    ldr  r2, =_sbss
    ldr  r4, =_ebss
    movs r3, #0
    b    .bss_zero_check
.bss_zero_loop:
    str  r3, [r2]
    adds r2, r2, #4
.bss_zero_check:
    cmp  r2, r4
    bcc  .bss_zero_loop

    ldr  r0, =main
    blx  r0
    b    .

    .size Reset_Handler, . - Reset_Handler

/* -------------------------------------------------------------------
 * Weak default handlers
 * ------------------------------------------------------------------- */
    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler

    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler

    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler

    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler

    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

    .section .text.Default_Handler, "ax", %progbits
Default_Handler:
    b    Default_Handler
    .size Default_Handler, . - Default_Handler
