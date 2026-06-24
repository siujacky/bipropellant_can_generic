/*
 * stm32f1xx_hal.h — minimal host-native shim for unit tests.
 *
 * Replaces the real STM32 HAL with just enough types and stubs to compile
 * src/board_override.c and src/board_select.c under a native GCC toolchain.
 *
 * DO NOT include stm32f1xx_hal_conf.h from here (the shim conf is empty but
 * the real inc/ conf pulls in the full HAL chain).
 */
#ifndef STM32F1XX_HAL_H
#define STM32F1XX_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * GPIO_TypeDef — mirrors the STM32F1 GPIO register block layout.
 * The host test never reads these registers for correctness (they live in
 * static mock arrays); the shim just needs the struct to compile.
 * ---------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
    volatile uint32_t BRR;
    volatile uint32_t LCKR2;
} GPIO_TypeDef;

/* -------------------------------------------------------------------------
 * GPIO_InitTypeDef
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

/* -------------------------------------------------------------------------
 * GPIO mode / pull / speed constants
 * ---------------------------------------------------------------------- */
#define GPIO_MODE_OUTPUT_PP         ((uint32_t)0x01u)
#define GPIO_MODE_INPUT             ((uint32_t)0x00u)
#define GPIO_MODE_IT_RISING_FALLING ((uint32_t)0x10u)

#define GPIO_NOPULL   ((uint32_t)0x00u)
#define GPIO_PULLUP   ((uint32_t)0x01u)
#define GPIO_PULLDOWN ((uint32_t)0x02u)

#define GPIO_SPEED_FREQ_LOW    ((uint32_t)0x00u)
#define GPIO_SPEED_FREQ_MEDIUM ((uint32_t)0x01u)
#define GPIO_SPEED_FREQ_HIGH   ((uint32_t)0x02u)

/* GPIO_PIN_x: each is a bit mask 1<<x */
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
 * Mock GPIO instances — declared extern; defined once in fixtures.h which
 * is included by the test TU (the one with main()).  board_override.c and
 * board_select.c pull in this header but do NOT define the symbols — they
 * use the extern declarations to link against the definitions in the test TU.
 * ---------------------------------------------------------------------- */
extern GPIO_TypeDef _mock_GPIOA;
extern GPIO_TypeDef _mock_GPIOB;
extern GPIO_TypeDef _mock_GPIOC;
extern GPIO_TypeDef _mock_GPIOD;
extern GPIO_TypeDef _mock_GPIOE;
extern GPIO_TypeDef _mock_GPIOF;
extern GPIO_TypeDef _mock_GPIOG;
extern GPIO_TypeDef _mock_GPIOH;

#define GPIOA (&_mock_GPIOA)
#define GPIOB (&_mock_GPIOB)
#define GPIOC (&_mock_GPIOC)
#define GPIOD (&_mock_GPIOD)
#define GPIOE (&_mock_GPIOE)
#define GPIOF (&_mock_GPIOF)
#define GPIOG (&_mock_GPIOG)
#define GPIOH (&_mock_GPIOH)

/* -------------------------------------------------------------------------
 * HAL_GPIO_Init stub — records call count and last arguments.
 *
 * The counter and last-args are shared across all TUs (not static) so that
 * board_override.c's call is visible to the test TU's assertion.  The
 * definitions live in fixtures.h (in the test TU); this header provides the
 * extern declarations and the inline body.
 * ---------------------------------------------------------------------- */
extern int             hal_gpio_init_count;
extern GPIO_TypeDef   *hal_gpio_init_last_port;
extern GPIO_InitTypeDef hal_gpio_init_last_init;

static inline void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    hal_gpio_init_count++;
    hal_gpio_init_last_port = port;
    hal_gpio_init_last_init = *init;
}

/* -------------------------------------------------------------------------
 * Opaque stubs for peripheral handle types referenced by defines.h includes.
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
 * HAL_GetTick — provided by test TU (defined as a function there).
 * Declared here so phasemap.c can use it without an implicit-function warning.
 * ---------------------------------------------------------------------- */
uint32_t HAL_GetTick(void);

/* -------------------------------------------------------------------------
 * Cortex-M critical section stubs — no-ops on the host.
 * phasemap.c uses __disable_irq() / __enable_irq() for the atomic
 * complement lockout; on the host the race cannot happen so stubs suffice.
 * ---------------------------------------------------------------------- */
static inline void __disable_irq(void) {}
static inline void __enable_irq(void)  {}

/* -------------------------------------------------------------------------
 * ADC HAL stubs — used only when PHASEMAP_HOST_TEST is NOT defined.
 * phasemap.c guards its ADC calls with #ifndef PHASEMAP_HOST_TEST so
 * these are never called in the test build; we still need the types to
 * compile cleanly.
 * ---------------------------------------------------------------------- */
#ifndef HAL_OK
#define HAL_OK 0u
#endif

typedef struct {
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;

#define ADC_REGULAR_RANK_1          1u
#define ADC_SAMPLETIME_239CYCLES_5  7u

static inline uint32_t HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc,
                                              ADC_ChannelConfTypeDef *cfg)
{
    (void)hadc; (void)cfg;
    return HAL_OK;
}
static inline uint32_t HAL_ADC_Start(ADC_HandleTypeDef *hadc) { (void)hadc; return HAL_OK; }
static inline uint32_t HAL_ADC_Stop(ADC_HandleTypeDef *hadc)  { (void)hadc; return HAL_OK; }
static inline uint32_t HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t t)
{ (void)hadc; (void)t; return HAL_OK; }
static inline uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc) { (void)hadc; return 2048u; }

#endif /* STM32F1XX_HAL_H */
