/*
 * Virtual COM port: TinyUSB CDC ACM <-> target UART.
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "hal.h"

void vcp_init(void)
{
    hal_vcp_config(115200, 8, 0, 0);
}

void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *lc)
{
    (void)itf;
    hal_vcp_config(lc->bit_rate, lc->data_bits, lc->parity, lc->stop_bits);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t ms)
{
    (void)itf;
    hal_vcp_set_break(ms != 0);
}

void vcp_task(void)
{
    uint8_t buf[64];

    /* USB -> UART */
    while (tud_cdc_available()) {
        size_t space = hal_vcp_tx_space();
        if (space == 0)
            break;
        if (space > sizeof(buf))
            space = sizeof(buf);
        uint32_t n = tud_cdc_read(buf, space);
        hal_vcp_tx(buf, n);
    }

    /* UART -> USB */
    if (!tud_cdc_connected()) {
        /* drop data nobody listens to */
        hal_vcp_rx(buf, sizeof(buf));
        return;
    }
    uint32_t space = tud_cdc_write_available();
    if (space > sizeof(buf))
        space = sizeof(buf);
    size_t n = space ? hal_vcp_rx(buf, space) : 0;
    if (n)
        tud_cdc_write(buf, n);
    tud_cdc_write_flush();
}
