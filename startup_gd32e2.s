/**
 * startup_gd32e2.s — Minimal Cortex-M23 startup for GD32E230 (GD32E2 family).
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  This startup is adapted from the ARM CMSIS template
 * (no GPL vendor library dependency).
 *
 * GD32E230 is a Cortex-M23 (ARMv8-M Baseline).  Key differences from M3:
 *   - Thumb-2 instructions are NOT available; use Thumb-1 (unified syntax still
 *     works with .cpu cortex-m23 and assembler selects legal encodings).
 *   - No hardware divide in baseline M23 (no UDIV/SDIV without the DSP extension).
 *   - -mcpu=cortex-m23 toolchain flag required (NOT cortex-m3).
 *
 * Provides:
 *   - Cortex-M23 vector table (SP, Reset_Handler, fault/exception handlers).
 *   - Reset_Handler: calls SystemInit(), copies .data, clears .bss, calls main().
 *
 * Symbols required from the linker script (gd32e2_flash.ld):
 *   _estack    — initial stack pointer (end of SRAM).
 *   _sidata    — start of .data LMA (in Flash).
 *   _sdata     — start of .data VMA (in SRAM).
 *   _edata     — end of .data VMA.
 *   _sbss      — start of .bss.
 *   _ebss      — end of .bss.
 */

    .syntax unified
    .cpu cortex-m23
    .thumb

/* -------------------------------------------------------------------
 * Stack top: provided by linker script symbol _estack.
 * ------------------------------------------------------------------- */
    .word _estack

/* -------------------------------------------------------------------
 * Minimal vector table.
 * ------------------------------------------------------------------- */
    .section .isr_vector, "a", %progbits
    .type g_pfnVectors, %object
g_pfnVectors:
    .word _estack               /* 0: Initial Stack Pointer         */
    .word Reset_Handler         /* 1: Reset Handler                 */
    .word NMI_Handler           /* 2: NMI Handler                   */
    .word HardFault_Handler     /* 3: Hard Fault Handler            */
    .word 0                     /* 4: Reserved (no MemManage on M23 baseline) */
    .word 0                     /* 5: Reserved                      */
    .word 0                     /* 6: Reserved                      */
    .word 0                     /* 7: Reserved                      */
    .word 0                     /* 8: Reserved                      */
    .word 0                     /* 9: Reserved                      */
    .word 0                     /* 10: Reserved                     */
    .word SVC_Handler           /* 11: SVCall Handler               */
    .word 0                     /* 12: Reserved (no DebugMon on baseline) */
    .word 0                     /* 13: Reserved                     */
    .word PendSV_Handler        /* 14: PendSV Handler               */
    .word SysTick_Handler       /* 15: SysTick Handler              */
    /* External interrupts — all default to Default_Handler */
    .word Default_Handler       /* IRQ0:  WWDGT                     */
    .word Default_Handler       /* IRQ1:  LVD                       */
    .word Default_Handler       /* IRQ2:  RTC                       */
    .word Default_Handler       /* IRQ3:  FMC                       */
    .word Default_Handler       /* IRQ4:  RCU                       */
    .word Default_Handler       /* IRQ5:  EXTI0_1                   */
    .word Default_Handler       /* IRQ6:  EXTI2_3                   */
    .word Default_Handler       /* IRQ7:  EXTI4_15                  */
    .word Default_Handler       /* IRQ8:  Reserved                  */
    .word Default_Handler       /* IRQ9:  DMA_CH0                   */
    .word Default_Handler       /* IRQ10: DMA_CH1_2                 */
    .word Default_Handler       /* IRQ11: DMA_CH3_4                 */
    .word Default_Handler       /* IRQ12: ADC_CMP                   */
    .word Default_Handler       /* IRQ13: TIMER0_BRK_UP_TRG_COM    */
    .word Default_Handler       /* IRQ14: TIMER0_CH                 */
    .word Default_Handler       /* IRQ15: TIMER2                    */
    .word Default_Handler       /* IRQ16: TIMER5                    */
    .word Default_Handler       /* IRQ17: TIMER13                   */
    .word Default_Handler       /* IRQ18: TIMER14                   */
    .word Default_Handler       /* IRQ19: TIMER15                   */
    .word Default_Handler       /* IRQ20: TIMER16                   */
    .word Default_Handler       /* IRQ21: I2C0_EV                   */
    .word Default_Handler       /* IRQ22: I2C1_EV                   */
    .word Default_Handler       /* IRQ23: SPI0                      */
    .word Default_Handler       /* IRQ24: SPI1                      */
    .word Default_Handler       /* IRQ25: USART0                    */
    .word Default_Handler       /* IRQ26: USART1                    */
    .word Default_Handler       /* IRQ27: I2C0_ER                   */
    .word Default_Handler       /* IRQ28: I2C1_ER                   */

/* -------------------------------------------------------------------
 * Reset_Handler: SystemInit() → copy .data → zero .bss → main()
 *
 * Cortex-M23 baseline: avoid MOVW/MOVT (available but use LDR for
 * clarity); avoid hardware divide.  Use only Thumb-1 friendly sequences.
 * ------------------------------------------------------------------- */
    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr  r0, =SystemInit
    blx  r0

    /* Copy .data: src=_sidata (Flash), dst=_sdata.._edata (SRAM) */
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
