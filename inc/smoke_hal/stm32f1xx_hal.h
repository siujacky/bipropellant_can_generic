/*
 * inc/smoke_hal/stm32f1xx_hal.h — Minimal HAL shim for non-STM32F1 smoke builds.
 *
 * This header satisfies the `#include "stm32f1xx_hal.h"` in board_active.h
 * and board_override.c's transitive includes (via defines.h) for the GD32F1,
 * GD32E2, and MM32SPIN0X smoke builds.  It provides only the types and
 * constants needed to compile the board-select/override machinery; it does NOT
 * provide real HAL functions or register definitions.
 *
 * For the smoke build, HAL_GPIO_Init is a no-op (the motor-disabled safe
 * default row never triggers a live GPIO reinit for real pins because all
 * ACTIVE slots are sentinel {0xFF,0xFF}).
 *
 * Include this directory FIRST in the non-STM32F1 Makefile env -I flags so it
 * takes precedence over any stm32f1xx_hal.h that might appear in the search path.
 *
 * Motor-disabled smoke only.
 */
#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal GPIO_TypeDef for compilation of board_active.h macros.
 * The BSRR / BRR / IDR / ODR fields are referenced syntactically in the
 * board_active.h macros but never called at runtime on the smoke build
 * (all ACTIVE slots are sentinel, so the HAL path is never reached).
 * ---------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

/* GPIO mode / pull / speed constants */
#define GPIO_MODE_OUTPUT_PP         ((uint32_t)0x01u)
#define GPIO_MODE_INPUT             ((uint32_t)0x00u)
#define GPIO_MODE_IT_RISING_FALLING ((uint32_t)0x10u)
#define GPIO_NOPULL                 ((uint32_t)0x00u)
#define GPIO_PULLUP                 ((uint32_t)0x01u)
#define GPIO_PULLDOWN               ((uint32_t)0x02u)
#define GPIO_SPEED_FREQ_LOW         ((uint32_t)0x00u)
#define GPIO_SPEED_FREQ_MEDIUM      ((uint32_t)0x01u)
#define GPIO_SPEED_FREQ_HIGH        ((uint32_t)0x02u)

#define GPIO_PIN_0  ((uint16_t)(1u <<  0))
#define GPIO_PIN_1  ((uint16_t)(1u <<  1))
#define GPIO_PIN_2  ((uint16_t)(1u <<  2))
#define GPIO_PIN_3  ((uint16_t)(1u <<  3))
#define GPIO_PIN_4  ((uint16_t)(1u <<  4))
#define GPIO_PIN_5  ((uint16_t)(1u <<  5))
#define GPIO_PIN_6  ((uint16_t)(1u <<  6))
#define GPIO_PIN_7  ((uint16_t)(1u <<  7))
#define GPIO_PIN_8  ((uint16_t)(1u <<  8))
#define GPIO_PIN_9  ((uint16_t)(1u <<  9))
#define GPIO_PIN_10 ((uint16_t)(1u << 10))
#define GPIO_PIN_11 ((uint16_t)(1u << 11))
#define GPIO_PIN_12 ((uint16_t)(1u << 12))
#define GPIO_PIN_13 ((uint16_t)(1u << 13))
#define GPIO_PIN_14 ((uint16_t)(1u << 14))
#define GPIO_PIN_15 ((uint16_t)(1u << 15))

/* -------------------------------------------------------------------------
 * NULL GPIO instances — board_override.c's field_meta_t table references
 * LED_PORT, BUZZER_PORT, etc. as GPIO_TypeDef*.  On the safe-default row all
 * pins are sentinel and gpio_reinit_live() is never called, so these can
 * safely be NULL for the smoke build.
 * ---------------------------------------------------------------------- */
#define GPIOA  ((GPIO_TypeDef *)0)
#define GPIOB  ((GPIO_TypeDef *)0)
#define GPIOC  ((GPIO_TypeDef *)0)
#define GPIOD  ((GPIO_TypeDef *)0)
#define GPIOE  ((GPIO_TypeDef *)0)
#define GPIOF  ((GPIO_TypeDef *)0)
#define GPIOG  ((GPIO_TypeDef *)0)
#define GPIOH  ((GPIO_TypeDef *)0)

/* -------------------------------------------------------------------------
 * HAL_GPIO_Init — no-op for smoke builds.  The motor-disabled safe default
 * row uses all-sentinel pins, so this is never called at runtime.
 * ---------------------------------------------------------------------- */
static inline void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    (void)port; (void)init;
    /* NO-OP: smoke build, motor-disabled safe default. */
}

/* -------------------------------------------------------------------------
 * Opaque handle stubs referenced by defines.h peripheral macros.
 * ---------------------------------------------------------------------- */
typedef struct { uint32_t dummy; } DMA_HandleTypeDef;
typedef struct { uint32_t dummy; } TIM_HandleTypeDef;
typedef struct { uint32_t dummy; } UART_HandleTypeDef;
typedef struct { uint32_t dummy; } SPI_HandleTypeDef;
typedef struct { uint32_t dummy; } I2C_HandleTypeDef;
typedef struct { uint32_t dummy; } ADC_HandleTypeDef;

/* UART word-length constants used by config.h */
#define UART_WORDLENGTH_8B 0u
#define UART_WORDLENGTH_9B 1u

/* -------------------------------------------------------------------------
 * TIM register stub — defines.h references LEFT_TIM->BDTR etc.
 * Provide a minimal TIM_TypeDef so the board_override.c k_fields table
 * compiles (it references TIM1/TIM8 through defines.h on STM32F1).
 * ---------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SMCR;
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    volatile uint32_t CCMR1;
    volatile uint32_t CCMR2;
    volatile uint32_t CCER;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
    volatile uint32_t ARR;
    volatile uint32_t RCR;
    volatile uint32_t CCR1;
    volatile uint32_t CCR2;
    volatile uint32_t CCR3;
    volatile uint32_t CCR4;
    volatile uint32_t BDTR;
    volatile uint32_t DCR;
    volatile uint32_t DMAR;
} TIM_TypeDef;

#define TIM1  ((TIM_TypeDef *)0)
#define TIM8  ((TIM_TypeDef *)0)
#define TIM_BDTR_MOE (1u << 15)

/* ADC stub — referenced by defines.h */
typedef struct { volatile uint32_t dummy; } ADC_TypeDef;
#define ADC1 ((ADC_TypeDef *)0)
#define ADC2 ((ADC_TypeDef *)0)

/* I2C stub */
typedef struct { volatile uint32_t dummy; } I2C_TypeDef;
#define I2C2 ((I2C_TypeDef *)0)

/* USART stub */
typedef struct { volatile uint32_t dummy; } USART_TypeDef;
#define USART1 ((USART_TypeDef *)0)
#define USART2 ((USART_TypeDef *)0)
#define USART3 ((USART_TypeDef *)0)

/* SPI stub */
typedef struct { volatile uint32_t dummy; } SPI_TypeDef;
#define SPI1 ((SPI_TypeDef *)0)
#define SPI2 ((SPI_TypeDef *)0)

/* HAL status */
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;

/* Stub for HAL macros referenced by config.h / setup.h */
#define __HAL_RCC_TIM1_CLK_ENABLE()   do {} while(0)
#define __HAL_RCC_TIM8_CLK_ENABLE()   do {} while(0)
#define __HAL_RCC_ADC1_CLK_ENABLE()   do {} while(0)
#define __HAL_RCC_ADC2_CLK_ENABLE()   do {} while(0)
#define __HAL_RCC_USART2_CLK_ENABLE() do {} while(0)
#define __HAL_RCC_USART3_CLK_ENABLE() do {} while(0)
#define __HAL_RCC_GPIOA_CLK_ENABLE()  do {} while(0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()  do {} while(0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()  do {} while(0)
#define __HAL_RCC_GPIOD_CLK_ENABLE()  do {} while(0)
#define __HAL_RCC_DMA1_CLK_ENABLE()   do {} while(0)
#define __HAL_AFIO_REMAP_ADC1_ETRGREG_ENABLE() do {} while(0)
#define __HAL_TIM_ENABLE(h)            do {} while(0)

/* Stub HAL init functions — return HAL_OK */
static inline HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *h) { (void)h; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t c) { (void)h;(void)c; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *h, uint32_t c) { (void)h;(void)c; return HAL_OK; }
static inline HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }

#endif /* STM32F1XX_HAL_H */
