/*
 * TinyUSB class driver for the WCH-Link vendor interface: commands on
 * EP 0x01 / 0x81, raw data on EP 0x02 / 0x82. One command at a time; the
 * reply goes out before the next command is taken.
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "wch.h"
#include "usb_desc.h"

static uint8_t rhport_used;
static bool opened;
static uint8_t cmd[WCH_PACKET_SIZE], resp[WCH_PACKET_SIZE];
static uint16_t cmd_len;
static bool cmd_armed, cmd_ready;
static uint8_t data[WCH_PACKET_SIZE], data_in[WCH_PACKET_SIZE];
static uint16_t data_len;
static bool data_armed, data_ready;

static void arm(void)
{
    if (!opened)
        return;
    if (!cmd_armed && !cmd_ready &&
        usbd_edpt_xfer(rhport_used, EP_WCH_CMD_OUT, cmd, sizeof cmd, false))
        cmd_armed = true;
    if (!data_armed && !data_ready && wch_data_wanted() &&
        usbd_edpt_xfer(rhport_used, EP_WCH_DATA_OUT, data, sizeof data, false))
        data_armed = true;
}

static void drv_init(void)
{
    opened = false;
}

static bool drv_deinit(void)
{
    return true;
}

static void drv_reset(uint8_t rhport)
{
    (void)rhport;
    opened = cmd_armed = cmd_ready = data_armed = data_ready = false;
}

static uint16_t drv_open(uint8_t rhport, const tusb_desc_interface_t *itf, uint16_t max_len)
{
    if (itf->bInterfaceClass != 0xFF || itf->bInterfaceNumber != ITF_WCH)
        return 0;
    uint16_t len = sizeof(tusb_desc_interface_t);
    const uint8_t *p = tu_desc_next(itf);
    for (int i = 0; i < itf->bNumEndpoints; i++) {
        while (tu_desc_type(p) != TUSB_DESC_ENDPOINT) {
            len += tu_desc_len(p);
            p = tu_desc_next(p);
        }
        if (!usbd_edpt_open(rhport, (const tusb_desc_endpoint_t *)p))
            return 0;
        len += tu_desc_len(p);
        p = tu_desc_next(p);
    }
    if (len > max_len)
        return 0;
    rhport_used = rhport;
    opened = true;
    cmd_armed = cmd_ready = data_armed = data_ready = false;
    arm();
    return len;
}

static bool drv_control(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r)
{
    (void)rhport; (void)stage; (void)r;
    return false;
}

static bool drv_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n)
{
    (void)rhport;
    if (ep == EP_WCH_CMD_OUT) {
        cmd_armed = false;
        if (result == XFER_RESULT_SUCCESS && n) {
            cmd_len = (uint16_t)n;
            cmd_ready = true;
        }
    } else if (ep == EP_WCH_DATA_OUT) {
        data_armed = false;
        if (result == XFER_RESULT_SUCCESS && n) {
            data_len = (uint16_t)n;
            data_ready = true;
        }
    }
    arm();
    return true;
}

const usbd_class_driver_t wch_driver = {
    .name = "WCH-Link",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control,
    .xfer_cb = drv_xfer,
    .sof = NULL,
};

bool wch_usb_data_in(const uint8_t *buf, uint16_t len)
{
    if (!opened || usbd_edpt_busy(rhport_used, EP_WCH_DATA_IN))
        return false;
    memcpy(data_in, buf, len);
    return usbd_edpt_xfer(rhport_used, EP_WCH_DATA_IN, data_in, len, false);
}

bool wch_usb_data_in_busy(void)
{
    return !opened || usbd_edpt_busy(rhport_used, EP_WCH_DATA_IN);
}

/* called from the main loop */
void wch_usb_task(void)
{
    if (!opened)
        return;
    if (data_ready) {
        wch_data_out(data, data_len);
        data_ready = false;
    }
    wch_task();
    if (cmd_ready && !usbd_edpt_busy(rhport_used, EP_WCH_CMD_IN)) {
        uint16_t n = wch_execute(cmd, cmd_len, resp);
        cmd_ready = false;
        if (n)
            usbd_edpt_xfer(rhport_used, EP_WCH_CMD_IN, resp, n, false);
    }
    arm();
}
