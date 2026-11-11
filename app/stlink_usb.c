/*
 * TinyUSB class driver for the ST-Link debug interface (IF0).
 *
 * The protocol is strictly request/response: a 16 byte command on the
 * OUT endpoint, an optional OUT data phase, an optional IN reply. Trace
 * (SWO) data goes out on its own IN endpoint when the host asks for it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "stlink.h"
#include "hal.h"
#include "usb_desc.h"

enum state { ST_CMD, ST_DATA_OUT, ST_EXEC, ST_DATA_IN };

static uint8_t rhport_used;
static uint8_t cmd[64];
/* OUT data and IN replies share one buffer: the protocol is strictly
 * sequential and no command has both a data phase and a reply */
static uint8_t data[STLINK_DATA_MAX + 16] __attribute__((aligned(4)));
static volatile enum state state;
static volatile bool opened;
static volatile bool cmd_ready;     /* command (ST_EXEC) or chunk (ST_DATA_OUT) */
static uint32_t xfer_total, xfer_done, chunk_len;

static void arm_cmd(void)
{
    state = ST_CMD;
    usbd_edpt_xfer(rhport_used, EP_STLINK_OUT, cmd, sizeof(cmd), false);
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
    cmd_ready = false;
}

static uint16_t drv_open(uint8_t rhport, const tusb_desc_interface_t *itf, uint16_t max_len)
{
    if (itf->bInterfaceClass != 0xFF || itf->bInterfaceNumber != ITF_STLINK)
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
    cmd_ready = false;
    arm_cmd();
    return len;
}

static bool drv_control(uint8_t rhport, uint8_t stage, const tusb_control_request_t *req)
{
    (void)rhport; (void)stage; (void)req;
    return false;
}

static void arm_out_chunk(void)
{
    uint32_t n = xfer_total - xfer_done;
    if (n > STLINK_DATA_MAX)
        n = STLINK_DATA_MAX;
    usbd_edpt_xfer(rhport_used, EP_STLINK_OUT, data, (uint16_t)n, false);
}

static bool drv_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n)
{
    (void)rhport;
    (void)result;
    if (ep != EP_STLINK_OUT)
        return true;
    if (state == ST_CMD) {
        if (n < 2) {
            arm_cmd();
            return true;
        }
        xfer_total = stlink_out_len(cmd);
        xfer_done = 0;
        if (xfer_total) {
            state = ST_DATA_OUT;
            arm_out_chunk();
        } else {
            state = ST_EXEC;
            cmd_ready = true;
        }
    } else if (state == ST_DATA_OUT) {
        chunk_len = n;
        cmd_ready = true;
    }
    return true;
}

const usbd_class_driver_t stlink_driver = {
    .name = "STLINK",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control,
    .xfer_cb = drv_xfer,
    .sof = NULL,
};

static void send_in_chunk(void)
{
    uint32_t n = xfer_total - xfer_done;
    if (n > STLINK_DATA_MAX)
        n = STLINK_DATA_MAX;
    stlink_mem_in(cmd, xfer_done, data, n);
    usbd_edpt_xfer(rhport_used, EP_STLINK_IN, data, (uint16_t)n, false);
    xfer_done += n;
    if (xfer_done >= xfer_total)
        arm_cmd();
}

/* ---- trace endpoint ---------------------------------------------------
 * The trace endpoint is a stream: full packets are fed from the SWO buffer
 * as soon as the host takes the previous one, a short packet only once the
 * line has been quiet for a while. GET_TRACE_NB reports everything not yet
 * delivered, so the host may read any amount per poll. */

#define TRACE_PKT       32u
#define TRACE_QUIET_MS  5u

static uint8_t trace_pkt[TRACE_PKT];
static uint32_t trace_inflight;
static uint32_t trace_seen, trace_change;

static uint32_t trace_fill(uint32_t want)
{
    uint32_t n = 0;
    while (n < want) {
        const uint8_t *p;
        uint32_t c = hal_swo_peek(&p);
        if (!c)
            break;
        if (c > want - n)
            c = want - n;
        memcpy(trace_pkt + n, p, c);
        hal_swo_consume(c);
        n += c;
    }
    return n;
}

static bool trace_quiet(uint32_t avail)
{
    uint32_t now = hal_millis();
    if (avail != trace_seen) {
        trace_seen = avail;
        trace_change = now;
    }
    return now - trace_change >= TRACE_QUIET_MS;
}

static void trace_task(void)
{
    if (!usbd_edpt_busy(rhport_used, EP_TRACE_IN))
        trace_inflight = 0;
    uint32_t avail = hal_swo_available();
    bool quiet = trace_quiet(avail);
    if (trace_inflight || !avail)
        return;
    if (avail < TRACE_PKT && !quiet)
        return;
    uint32_t n = trace_fill(avail < TRACE_PKT ? avail : TRACE_PKT);
    trace_seen = hal_swo_available();
    if (n && usbd_edpt_xfer(rhport_used, EP_TRACE_IN, trace_pkt, (uint16_t)n, false))
        trace_inflight = n;
}

uint32_t stlink_trace_count(void)
{
    if (!opened)
        return 0;
    if (!usbd_edpt_busy(rhport_used, EP_TRACE_IN))
        trace_inflight = 0;
    uint32_t avail = hal_swo_available();
    if (!trace_quiet(avail))
        avail -= avail % TRACE_PKT;      /* partial packet not sent yet */
    return trace_inflight + avail;
}

/* called from the main loop */
void stlink_usb_task(void)
{
    if (!opened)
        return;
    trace_task();

    if (state == ST_DATA_IN) {
        if (!usbd_edpt_busy(rhport_used, EP_STLINK_IN))
            send_in_chunk();
        return;
    }
    if (!cmd_ready)
        return;

    if (state == ST_DATA_OUT) {
        cmd_ready = false;
        stlink_mem_out(cmd, xfer_done, data, chunk_len);
        xfer_done += chunk_len;
        if (xfer_done >= xfer_total || chunk_len == 0 || (chunk_len % 64u))
            arm_cmd();
        else
            arm_out_chunk();
        return;
    }

    /* ST_EXEC: the previous reply must be gone before the buffer is reused */
    if (usbd_edpt_busy(rhport_used, EP_STLINK_IN))
        return;
    cmd_ready = false;

    xfer_total = stlink_in_len(cmd);
    if (xfer_total) {
        xfer_done = 0;
        state = ST_DATA_IN;
        send_in_chunk();
        return;
    }

    uint32_t rlen = 0;
    stlink_execute(cmd, data, &rlen);
    if (rlen)
        usbd_edpt_xfer(rhport_used, EP_STLINK_IN, data, (uint16_t)rlen, false);

    arm_cmd();
}
