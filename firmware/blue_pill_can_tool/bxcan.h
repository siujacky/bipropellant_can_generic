#ifndef BXCAN_H
#define BXCAN_H
#include <stdint.h>
/* bxCAN on PB8(RX)/PB9(TX), AFIO remap, 250kbps @ 8MHz */
void bxcan_app_init(uint32_t bps);
int  bxcan_app_tx(uint32_t id, const uint8_t *data, uint8_t len, int extended);
int  bxcan_app_rx(uint32_t *id_out, uint8_t *data_out, uint8_t *len_out, int *ext_out);
int  bxcan_app_rx_available(void);
void bxcan_app_enter_normal(void);
void bxcan_app_enter_listen(void);
uint8_t bxcan_app_read_errors(void);
#endif
