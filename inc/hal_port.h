/*
 * hal_port.h — family-agnostic GPIO port accessor wrappers (M5 scaffold).
 *
 * Provides gpio_port_ptr(port_idx) returning a volatile uint32_t* pointing at
 * the port base address so that board_active.h macros can resolve a gpio_pin_t
 * to a real port address independent of HAL, on any supported family.
 *
 * ADDRESS MAPPING POLICY
 * ----------------------
 * STM32F1 (Cortex-M3): GPIOA base = 0x40010800; stride 0x400 per port.
 *   GPIOA=0x40010800, GPIOB=0x40010C00, … GPIOG=0x40011C00.
 *   The mapping matches the CMSIS device headers in
 *   Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xe.h.
 *
 * GD32F1 / GD32F130 (Cortex-M3): GPIO base = 0x48000000; stride 0x400.
 *   GPIOA=0x48000000, GPIOB=0x48000400, … — compatible with GD32F1x0
 *   CMSIS device headers.
 *
 * GD32E2 / GD32E230 (Cortex-M23): same AHB GPIO layout as GD32F1x0.
 *   GPIOA=0x48000000, stride 0x400.
 *
 * MM32SPIN0X (Cortex-M0): GPIO base = 0x48000000; stride 0x400.
 *   Matches MM32SPIN0x RM GPIO chapter.
 *
 * STUB POLICY
 * -----------
 * When the full CMSIS device headers are not yet present (the non-STM32F1
 * families in the M5 scaffold), this header provides a stub that returns NULL
 * for any port index >= 8 AND for the non-STM32F1 families unless the caller
 * defines BOARD_FAMILY_GD32F1, BOARD_FAMILY_GD32E2, or BOARD_FAMILY_MM32SPIN0X.
 * The stub compiles cleanly on the host (no HAL dependency in the header).
 *
 * USAGE
 * -----
 * This header is standalone: it has no dependency on stm32f1xx_hal.h or any
 * vendor HAL.  It only uses <stdint.h>.  board_active.h uses BOARD_PORT_PTR()
 * (which calls through HAL's GPIOA/B/… macros on STM32F1); hal_port.h provides
 * an alternative raw-address path for families where no HAL is yet present.
 *
 *   volatile uint32_t *base = gpio_port_ptr(gp.port);
 *   if (base) { base[5] |= (1u << gp.pin); }  // e.g. ODR offset = 5 words
 *
 * NOTE: the register OFFSET within the port block differs between families.
 * Callers that need to touch specific registers must use the family-specific
 * register layout documented in the RM.  This wrapper only resolves the BASE.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.
 */
#pragma once

#include <stdint.h>

/* Maximum valid port index.  port_idx 0=A .. 7=H; >=8 is sentinel/invalid. */
#define HAL_PORT_MAX_IDX  8u

/* ---------------------------------------------------------------------------
 * gpio_port_ptr — resolve a 0-based port index to the GPIO base address.
 *
 * Returns a volatile uint32_t* to the port base register block, or NULL if:
 *   - port_idx >= HAL_PORT_MAX_IDX (sentinel / out of range), OR
 *   - the family base addresses are not yet known (CMSIS headers absent).
 *
 * The returned pointer is the PORT BASE; callers must add the register offset
 * for the specific register they want (IDR, ODR, BSRR, BRR, etc.).
 * ------------------------------------------------------------------------- */
static inline volatile uint32_t *gpio_port_ptr(uint8_t port_idx)
{
    if (port_idx >= HAL_PORT_MAX_IDX) {
        return (volatile uint32_t *)0;  /* sentinel / out of range */
    }

#if defined(BUILD_FAMILY_STM32F1)
    /*
     * STM32F103: GPIOA base = 0x40010800; stride = 0x400.
     * Matches stm32f103xe.h: GPIOA_BASE + port_idx * 0x400.
     */
    return (volatile uint32_t *)(0x40010800u + (uint32_t)port_idx * 0x400u);

#elif defined(BUILD_FAMILY_GD32F1)
    /*
     * GD32F130 (Cortex-M3, AHB GPIO): GPIOA base = 0x48000000; stride = 0x400.
     * Source: GD32F1x0 User Manual, Table 2-2 Memory Map.
     *
     * Motor-disabled smoke only — peripheral register layout must be verified
     * against GD32F1x0 RM before enabling GPIO drive.
     */
    return (volatile uint32_t *)(0x48000000u + (uint32_t)port_idx * 0x400u);

#elif defined(BUILD_FAMILY_GD32E2)
    /*
     * GD32E230 (Cortex-M23, AHB GPIO): GPIOA base = 0x48000000; stride = 0x400.
     * Source: GD32E23x User Manual, Table 2-1 Memory Map.
     *
     * Motor-disabled smoke only — peripheral register layout must be verified
     * against GD32E23x RM before enabling GPIO drive.
     */
    return (volatile uint32_t *)(0x48000000u + (uint32_t)port_idx * 0x400u);

#elif defined(BUILD_FAMILY_MM32SPIN0X)
    /*
     * MM32SPIN0x (Cortex-M0, AHB GPIO): GPIOA base = 0x48000000; stride = 0x400.
     * Source: MM32SPIN0x Reference Manual, GPIO chapter.
     *
     * Motor-disabled smoke only — peripheral register layout must be verified
     * against MM32SPIN0x RM before enabling GPIO drive.
     */
    return (volatile uint32_t *)(0x48000000u + (uint32_t)port_idx * 0x400u);

#else
    /*
     * STUB: CMSIS device headers not yet present for this family, or
     * BOARD_FAMILY_* is not defined.  Return NULL so callers can guard with:
     *   if (gpio_port_ptr(port_idx)) { ... }
     *
     * This compiles cleanly on the host (no HAL dep) and on any family
     * for which the base addresses have not yet been confirmed on silicon.
     */
    (void)port_idx;
    return (volatile uint32_t *)0;
#endif
}

/* ---------------------------------------------------------------------------
 * Convenience macro: resolve a gpio_pin_t to its port base + pin mask.
 * Callers still need to know the register offset for the specific operation.
 * Matches the sentinel policy in board_active.h (port==0xFF = unset).
 * ------------------------------------------------------------------------- */
#define HAL_PORT_BASE(gp) \
    (((gp).port == 0xFFu) ? (volatile uint32_t *)0 : gpio_port_ptr((gp).port))

#define HAL_PIN_MASK(gp) \
    (((gp).pin < 16u) ? (1u << (gp).pin) : 0u)
