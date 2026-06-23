/*
 * hal_mock.c — shared definitions for the host-native HAL shim mock state.
 *
 * hal_gpio_init_count, hal_gpio_init_last_port, and hal_gpio_init_last_init are
 * declared extern in stm32f1xx_hal.h (shim) so that ALL translation units that
 * include the shim share one counter.  This file provides the single definition.
 *
 * Link this file alongside every test that uses the shim:
 *   gcc ... hal_mock.c test_board_override.c src/board_override.c ...
 */
#include "stm32f1xx_hal.h"

/* Shared mock state — definitions (one per program) */
int             hal_gpio_init_count     = 0;
GPIO_TypeDef   *hal_gpio_init_last_port = (GPIO_TypeDef *)0;
GPIO_InitTypeDef hal_gpio_init_last_init;

/* Mock GPIO instances (one set shared across all TUs) */
GPIO_TypeDef _mock_GPIOA;
GPIO_TypeDef _mock_GPIOB;
GPIO_TypeDef _mock_GPIOC;
GPIO_TypeDef _mock_GPIOD;
GPIO_TypeDef _mock_GPIOE;
GPIO_TypeDef _mock_GPIOF;
GPIO_TypeDef _mock_GPIOG;
GPIO_TypeDef _mock_GPIOH;
