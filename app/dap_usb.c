/*
 * TinyUSB class driver for the CMSIS-DAP v2 interface (vendor class, bulk).
 *
 * Requests are queued (DAP_PACKET_COUNT packets, as reported to the host)
 * and executed in order from the main loop; every request gets exactly one
 * response packet.
 *
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "dap.h"
#include "usb_desc.h"

static uint8_t rhport_used;
static bool opened;
static uint8_t req[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
static uint16_t req_len[DAP_PACKET_COUNT];
static uint8_t head, tail, used;
static bool out_armed;
static uint8_t resp[DAP_PACKET_SIZE];

static void arm_out(void)
{
    if (opened && !out_armed && used < DAP_PACKET_COUNT &&
        usbd_edpt_xfer(rhport_used, EP_DAP_OUT, req[head], DAP_PACKET_SIZE, false))
        out_armed = true;
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
    opened = false;
    out_armed = false;
    head = tail = used = 0;
}

static uint16_t drv_open(uint8_t rhport, const tusb_desc_interface_t *itf, uint16_t max_len)
{
    if (itf->bInterfaceClass != 0xFF || itf->bInterfaceNumber != ITF_DAP)
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
    out_armed = false;
    head = tail = used = 0;
    arm_out();
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
    if (ep != EP_DAP_OUT)
        return true;
    out_armed = false;
    if (result == XFER_RESULT_SUCCESS && n) {
        req_len[head] = (uint16_t)n;
        head = (head + 1) % DAP_PACKET_COUNT;
        used++;
    }
    arm_out();
    return true;
}

const usbd_class_driver_t dap_driver = {
    .name = "CMSIS-DAP",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control,
    .xfer_cb = drv_xfer,
    .sof = NULL,
};

/* called from the main loop */
void dap_usb_task(void)
{
    if (!opened || !used || usbd_edpt_busy(rhport_used, EP_DAP_IN))
        return;
    uint32_t n = dap_execute(req[tail], req_len[tail], resp);
    tail = (tail + 1) % DAP_PACKET_COUNT;
    used--;
    arm_out();
    if (n)
        usbd_edpt_xfer(rhport_used, EP_DAP_IN, resp, (uint16_t)n, false);
}
