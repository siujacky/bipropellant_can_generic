#ifndef BXCAN_BL_H
#define BXCAN_BL_H
#include <stdint.h>
void bxcan_init(void);
void bxcan_tx(uint16_t id, const uint8_t *data, uint8_t len);
int  bxcan_rx(uint16_t *id_out, uint8_t *data_out, uint8_t *len_out);
#endif
