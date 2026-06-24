/* uart_hdsel.c — Hardware single-wire half-duplex UART (HAL-based).
 *
 * HAL_HalfDuplex_Init() sets USART_CR3_HDSEL and configures the TX pin
 * as open-drain alternate-function — exactly what single-wire mode needs.
 * The GPIO clock and USART clock are enabled here; the application must NOT
 * call MX_USARTx_UART_Init() for the same port or the configs will conflict.
 */

#include "uart_hdsel.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal: GPIO and USART clock + pin setup for each port.
 * HAL_HalfDuplex_Init() sets HDSEL + OD on the TX pin; we only need to
 * enable the clocks and let HAL do the rest.
 * --------------------------------------------------------------------------- */
static HAL_StatusTypeDef _setup_clocks_and_instance(uart_hdsel_t *h,
                                                     uart_hdsel_port_t port,
                                                     uint32_t baud)
{
    h->port = port;
    h->baud = baud;
    memset(&h->hal, 0, sizeof(h->hal));

    h->hal.Init.BaudRate     = baud;
    h->hal.Init.WordLength   = UART_WORDLENGTH_8B;
    h->hal.Init.StopBits     = UART_STOPBITS_1;
    h->hal.Init.Parity       = UART_PARITY_NONE;
    h->hal.Init.Mode         = UART_MODE_TX_RX;
    h->hal.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    h->hal.Init.OverSampling = UART_OVERSAMPLING_16;

    switch (port) {
        case UART_HDSEL_1:
            __HAL_RCC_USART1_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            h->hal.Instance = USART1;
            break;
        case UART_HDSEL_2:
            __HAL_RCC_USART2_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            h->hal.Instance = USART2;
            break;
        case UART_HDSEL_3:
            __HAL_RCC_USART3_CLK_ENABLE();
            __HAL_RCC_GPIOB_CLK_ENABLE();
            h->hal.Instance = USART3;
            break;
        default:
            return HAL_ERROR;
    }
    return HAL_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

HAL_StatusTypeDef uart_hdsel_init(uart_hdsel_t *h, uart_hdsel_port_t port, uint32_t baud)
{
    HAL_StatusTypeDef st = _setup_clocks_and_instance(h, port, baud);
    if (st != HAL_OK) return st;

    /* HAL_HalfDuplex_Init:
     *   1. Sets USART_CR3_HDSEL.
     *   2. Configures the TX pin as GPIO_AF_OD (open-drain) — correct for
     *      a shared bus; a point-to-point link can use push-pull by patching
     *      the HAL MSP or setting GPIOA->CRL/CRH directly after this call.
     *   3. Leaves the RX pin unconfigured (the STM32 reference manual says
     *      "when HDSEL is set, the RX pin is not used"). */
    return HAL_HalfDuplex_Init(&h->hal);
}

HAL_StatusTypeDef uart_hdsel_tx(uart_hdsel_t *h, const uint8_t *buf,
                                uint16_t len, uint32_t timeout_ms)
{
    /* Cast away const: HAL_UART_Transmit() takes uint8_t* but does not write */
    return HAL_UART_Transmit(&h->hal, (uint8_t *)buf, len, timeout_ms);
}

HAL_StatusTypeDef uart_hdsel_rx(uart_hdsel_t *h, uint8_t *buf,
                                uint16_t len, uint32_t timeout_ms)
{
    return HAL_UART_Receive(&h->hal, buf, len, timeout_ms);
}

int uart_hdsel_tx_verify(uart_hdsel_t *h,
                         const uint8_t *buf, uint16_t len,
                         uint32_t tx_timeout_ms, uint32_t rx_timeout_ms)
{
    int errors = 0;
    for (uint16_t i = 0; i < len; i++) {
        /* Transmit one byte.  HAL_UART_Transmit blocks until TC (transmission
         * complete), so the byte is fully on the wire before we listen. */
        if (HAL_UART_Transmit(&h->hal, (uint8_t *)&buf[i], 1, tx_timeout_ms) != HAL_OK) {
            errors++;
            continue;
        }
        /* In HDSEL mode the hardware loops the transmitted byte back to the
         * RX shift register.  Reading it verifies the actual line state —
         * a mismatch means another node drove the line differently (collision)
         * or the wire is broken/shorted. */
        uint8_t echo;
        if (HAL_UART_Receive(&h->hal, &echo, 1, rx_timeout_ms) != HAL_OK) {
            errors++;   /* timeout — no echo (broken wire, or not in HDSEL mode) */
        } else if (echo != buf[i]) {
            errors++;   /* mismatch — line was at a different value */
        }
    }
    return errors;
}
