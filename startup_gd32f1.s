/**
 * startup_gd32f1.s — Minimal Cortex-M3 startup for GD32F130 (GD32F1 family).
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  This startup is adapted from the ARM CMSIS template
 * (no GPL vendor library dependency).
 *
 * Provides:
 *   - Cortex-M3 vector table (SP, Reset_Handler, fault/exception handlers).
 *   - Reset_Handler: calls SystemInit(), copies .data, clears .bss, calls main().
 *
 * Symbols required from the linker script (gd32f1_flash.ld):
 *   _estack    — initial stack pointer (end of SRAM).
 *   _sidata    — start of .data LMA (in Flash).
 *   _sdata     — start of .data VMA (in SRAM).
 *   _edata     — end of .data VMA.
 *   _sbss      — start of .bss.
 *   _ebss      — end of .bss.
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

/* -------------------------------------------------------------------
 * Stack top (initial SP): provided by linker script symbol _estack.
 * ------------------------------------------------------------------- */
    .word _estack

/* -------------------------------------------------------------------
 * Minimal vector table.
 * Only the vectors needed for a smoke build are wired to handlers;
 * all others default to Default_Handler (infinite loop / breakpoint).
 * ------------------------------------------------------------------- */
    .section .isr_vector, "a", %progbits
    .type g_pfnVectors, %object
g_pfnVectors:
    .word _estack                   /* 0: Initial Stack Pointer         */
    .word Reset_Handler             /* 1: Reset Handler                 */
    .word NMI_Handler               /* 2: NMI Handler                   */
    .word HardFault_Handler         /* 3: Hard Fault Handler            */
    .word MemManage_Handler         /* 4: Memory Management Fault       */
    .word BusFault_Handler          /* 5: Bus Fault Handler             */
    .word UsageFault_Handler        /* 6: Usage Fault Handler           */
    .word 0                         /* 7: Reserved                      */
    .word 0                         /* 8: Reserved                      */
    .word 0                         /* 9: Reserved                      */
    .word 0                         /* 10: Reserved                     */
    .word SVC_Handler               /* 11: SVCall Handler               */
    .word DebugMon_Handler          /* 12: Debug Monitor Handler        */
    .word 0                         /* 13: Reserved                     */
    .word PendSV_Handler            /* 14: PendSV Handler               */
    .word SysTick_Handler           /* 15: SysTick Handler              */
    /* External interrupts (IRQ0..n): all default to Default_Handler. */
    .word Default_Handler           /* IRQ0:  WWDGT                     */
    .word Default_Handler           /* IRQ1:  LVD                       */
    .word Default_Handler           /* IRQ2:  RTC                       */
    .word Default_Handler           /* IRQ3:  FMC                       */
    .word Default_Handler           /* IRQ4:  RCU                       */
    .word Default_Handler           /* IRQ5:  EXTI0_1                   */
    .word Default_Handler           /* IRQ6:  EXTI2_3                   */
    .word Default_Handler           /* IRQ7:  EXTI4_15                  */
    .word Default_Handler           /* IRQ8:  TSI                       */
    .word Default_Handler           /* IRQ9:  DMA_CH0                   */
    .word Default_Handler           /* IRQ10: DMA_CH1_2                 */
    .word Default_Handler           /* IRQ11: DMA_CH3_4                 */
    .word Default_Handler           /* IRQ12: ADC_CMP                   */
    .word Default_Handler           /* IRQ13: TIMER0_BRK_UP_TRG_COM    */
    .word Default_Handler           /* IRQ14: TIMER0_Channel             */
    .word Default_Handler           /* IRQ15: TIMER1                    */
    .word Default_Handler           /* IRQ16: TIMER2                    */
    .word Default_Handler           /* IRQ17: TIMER13                   */
    .word Default_Handler           /* IRQ18: Reserved                  */
    .word Default_Handler           /* IRQ19: TIMER14                   */
    .word Default_Handler           /* IRQ20: TIMER15                   */
    .word Default_Handler           /* IRQ21: TIMER16                   */
    .word Default_Handler           /* IRQ22: I2C0_EV                   */
    .word Default_Handler           /* IRQ23: I2C1_EV                   */
    .word Default_Handler           /* IRQ24: SPI0                      */
    .word Default_Handler           /* IRQ25: SPI1                      */
    .word Default_Handler           /* IRQ26: USART0                    */
    .word Default_Handler           /* IRQ27: USART1                    */
    .word Default_Handler           /* IRQ28: Reserved                  */
    .word Default_Handler           /* IRQ29: CEC                       */
    .word Default_Handler           /* IRQ30: Reserved                  */
    .word Default_Handler           /* IRQ31: I2C0_ER                   */

/* -------------------------------------------------------------------
 * Reset_Handler: SystemInit() → copy .data → zero .bss → main()
 * ------------------------------------------------------------------- */
    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Call SystemInit() to bring the clock to a known state before
     * the C runtime initialises (required for correct flash wait-state). */
    ldr  r0, =SystemInit
    blx  r0

    /* Copy .data from Flash (LMA=_sidata) to SRAM (VMA=_sdata.._edata). */
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

    /* Zero .bss. */
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

    /* Branch to main(). */
    ldr  r0, =main
    blx  r0

    /* If main() returns, spin forever. */
    b    .

    .size Reset_Handler, . - Reset_Handler

/* -------------------------------------------------------------------
 * Weak default handlers — override in application C files as needed.
 * ------------------------------------------------------------------- */
    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler

    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler

    .weak MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler

    .weak BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler

    .weak UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler

    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler

    .weak DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler

    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler

    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

    .section .text.Default_Handler, "ax", %progbits
Default_Handler:
    b    Default_Handler
    .size Default_Handler, . - Default_Handler
