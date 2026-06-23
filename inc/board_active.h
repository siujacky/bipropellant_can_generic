/*
 * board_active.h — the ACTIVE-profile contract.
 *
 * Part of the bipropellant_can_generic universal-firmware refactor (M2).
 * This header is THE CONTRACT that every downstream pin consumer uses to
 * source its port/pin from the boot-resolved board profile instead of the
 * compile-time defines.h macros.
 *
 * ACTIVE is a MUTABLE RAM copy of the selected board_profile_t. board_select.c
 * populates it (board_select_init() copies g_board_table[g_default_board_index]
 * — the motor-disabled safe row — into it at boot); later milestones add the
 * full resolver and live overrides that mutate ACTIVE in place.
 *
 * SAFE-DEFAULT / byte-for-byte equivalence rule
 * ---------------------------------------------
 * The default-safe row is all-sentinel ({0xFF,0xFF}) for every pin slot. Every
 * accessor below FALLS BACK to a caller-supplied compile-time default
 * (the existing defines.h port/pin macros) whenever the ACTIVE slot is the
 * sentinel. So for the default profile the firmware resolves to exactly the
 * historical STM32F1 pins and stays byte-for-byte equivalent to the baseline.
 * Once a real profile is selected (M3) the ACTIVE values take over.
 */
#pragma once

#include "stm32f1xx_hal.h"
#include "board_table.h"

/* ---------------------------------------------------------------------------
 * The active (mutable) board profile. Defined in board_select.c.
 * ------------------------------------------------------------------------- */
extern board_profile_t ACTIVE;

/* Initialise ACTIVE from the motor-disabled safe default row. Must be called
 * EARLY in main() (before peripheral init) but AFTER the board-independent
 * self-hold latch. The full resolver (stored-index/serial/narrowing) lands in
 * a later task; for now this is the stub that guarantees a known-safe ACTIVE. */
void board_select_init(void);

/* ---------------------------------------------------------------------------
 * Sentinel test.
 *   A gpio_pin_t is "unset" when port==0xFF (pin is also 0xFF by convention).
 * ------------------------------------------------------------------------- */
#define BOARD_PIN_IS_UNSET(gp)   ((gp).port == 0xFFu)

/* ---------------------------------------------------------------------------
 * Port-index -> GPIO_TypeDef* mapping.  port: 0=A .. 7=H.
 *   BOARD_PORT_PTR(port_idx) returns the GPIO_TypeDef* for a raw 0..7 index,
 *   or NULL if the index is the sentinel / out of range.
 * ------------------------------------------------------------------------- */
static inline GPIO_TypeDef *board_port_ptr(uint8_t port_idx) {
    switch (port_idx) {
        case 0:  return GPIOA;
        case 1:  return GPIOB;
        case 2:  return GPIOC;
        case 3:  return GPIOD;
        case 4:  return GPIOE;
        case 5:  return GPIOF;
        case 6:  return GPIOG;
#if defined(GPIOH)
        case 7:  return GPIOH;
#endif
        default: return (GPIO_TypeDef *)0;
    }
}
#define BOARD_PORT_PTR(port_idx)  board_port_ptr((uint8_t)(port_idx))

/* Pin number (0..15) -> HAL pin mask (1<<pin). */
#define BOARD_PIN_MASK(pin)       ((uint16_t)(1u << (pin)))

/* ---------------------------------------------------------------------------
 * gpio_pin_t accessors WITH compile-time fallback.
 *
 * Each takes a gpio_pin_t slot plus the historical defines.h default port
 * pointer and default pin MASK. When the slot is the sentinel, the default is
 * used — preserving baseline behaviour for the all-sentinel safe row.
 *
 *   BOARD_GP_PORT(gp, def_port)  -> GPIO_TypeDef*  (port for HAL_GPIO_* calls)
 *   BOARD_GP_MASK(gp, def_mask)  -> uint16_t       (pin mask for HAL/->BSRR/->BRR)
 *
 * def_port is a GPIO_TypeDef* (e.g. OFF_PORT); def_mask is a HAL pin macro
 * (e.g. OFF_PIN, which is already a 1<<n mask).
 * ------------------------------------------------------------------------- */
#define BOARD_GP_PORT(gp, def_port) \
    (BOARD_PIN_IS_UNSET(gp) ? (def_port) : BOARD_PORT_PTR((gp).port))
#define BOARD_GP_MASK(gp, def_mask) \
    (BOARD_PIN_IS_UNSET(gp) ? (uint16_t)(def_mask) : BOARD_PIN_MASK((gp).pin))

/* ---------------------------------------------------------------------------
 * Convenience for HAL_GPIO_Init: initialise a GPIO_InitTypeDef's .Pin field
 * and run HAL_GPIO_Init on the resolved (port, mask).  The caller fills the
 * other GPIO_InitStruct fields (Mode/Pull/Speed) first, exactly as today.
 *
 *   BOARD_GPIO_INIT(&gpioInit, ACTIVE.led_green, LED_PORT, LED_PIN);
 * ------------------------------------------------------------------------- */
#define BOARD_GPIO_INIT(pInit, gp, def_port, def_mask)            \
    do {                                                          \
        (pInit)->Pin = BOARD_GP_MASK((gp), (def_mask));           \
        HAL_GPIO_Init(BOARD_GP_PORT((gp), (def_port)), (pInit));  \
    } while (0)

/* ---------------------------------------------------------------------------
 * Direct register-site helpers (->BSRR / ->BRR / ->IDR / ->ODR).
 *
 * These resolve the ACTIVE-sourced (port, mask) and touch the register
 * directly, matching the existing direct-register pin sites that bypass the
 * HAL (charger read, halls, software-SPI, sensor read). Converting those
 * sites to these helpers is what makes a live override actually change
 * behaviour (a plain macro would keep reading the compile-time pin).
 *
 *   BOARD_GPIO_SET(gp, def_port, def_mask)   -> ->BSRR = mask          (drive HIGH)
 *   BOARD_GPIO_RESET(gp, def_port, def_mask) -> ->BRR  = mask          (drive LOW)
 *   BOARD_GPIO_READ(gp, def_port, def_mask)  -> (port->IDR & mask)!=0  (input read)
 *   BOARD_GPIO_READ_OUT(gp, def_port, def_mask) -> (port->ODR & mask)!=0 (output latch read)
 * ------------------------------------------------------------------------- */
#define BOARD_GPIO_SET(gp, def_port, def_mask) \
    (BOARD_GP_PORT((gp), (def_port))->BSRR = BOARD_GP_MASK((gp), (def_mask)))
#define BOARD_GPIO_RESET(gp, def_port, def_mask) \
    (BOARD_GP_PORT((gp), (def_port))->BRR = BOARD_GP_MASK((gp), (def_mask)))
#define BOARD_GPIO_READ(gp, def_port, def_mask) \
    ((BOARD_GP_PORT((gp), (def_port))->IDR & BOARD_GP_MASK((gp), (def_mask))) != 0u)
#define BOARD_GPIO_READ_OUT(gp, def_port, def_mask) \
    ((BOARD_GP_PORT((gp), (def_port))->ODR & BOARD_GP_MASK((gp), (def_mask))) != 0u)

/* ---------------------------------------------------------------------------
 * Override-result enum (the API contract for board_override.c, M4).
 * Aliased to the generated board_override_result_t so the values stay in
 * lock-step with the host/CLI side.
 * ------------------------------------------------------------------------- */
typedef enum {
    APPLIED_LIVE      = BOARD_OVERRIDE_APPLIED_LIVE,
    PENDING_REBOOT    = BOARD_OVERRIDE_PENDING_REBOOT,
    REJECTED_SILICON  = BOARD_OVERRIDE_REJECTED_SILICON,
    REJECTED_CONFLICT = BOARD_OVERRIDE_REJECTED_CONFLICT,
    REJECTED_EXTI     = BOARD_OVERRIDE_REJECTED_EXTI,
} override_result_t;
