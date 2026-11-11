/*
 * Probe settings (reported version, mode) and the hybridlink vendor
 * control requests.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "tusb.h"
#include "hal.h"
#include "settings.h"

static uint8_t page[CFG_SIZE];
static enum probe_mode mode;
static struct { bool set; uint32_t addr; uint16_t val; } pending;
static enum settings_restart restart, restart_after_store;

bool settings_store16(uint32_t addr, uint16_t v)
{
    uint8_t be[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    uint16_t cur = cfg_read16(addr);

    if (cur == v)
        return true;
    if (cur == 0xFFFF)                      /* blank: program in place */
        return hal_flash_write(addr, be, 2);

    memcpy(page, (const void *)CFG_BASE, CFG_SIZE);
    page[addr - CFG_BASE] = be[0];
    page[addr - CFG_BASE + 1] = be[1];
    uint32_t ps = hal_flash_page_size();
    for (uint32_t a = CFG_BASE; a < CFG_BASE + CFG_SIZE; a += ps)
        if (!hal_flash_erase_page(a))
            return false;
    return hal_flash_write(CFG_BASE, page, CFG_SIZE);
}

void settings_init(void)
{
    mode = mode_stored();
}

enum probe_mode settings_mode(void)
{
    return mode;
}

enum settings_restart settings_restart_requested(void)
{
    return restart;
}

static bool version_valid(uint16_t v)
{
    return v == 0xFFFF || ((v >> 12) == 2 && ((v >> 6) & 0x3F) != 0);
}

bool settings_vendor_request(uint8_t rhport, uint8_t stage, const void *request)
{
    const tusb_control_request_t *req = request;
    static uint8_t buf[9];
    bool in = req->bmRequestType_bit.direction == TUSB_DIR_IN;

    if (req->bmRequestType_bit.recipient != TUSB_REQ_RCPT_DEVICE ||
        req->wIndex != HL_REQ_MAGIC)
        return false;

    if (stage == CONTROL_STAGE_SETUP) {
        uint16_t r, s, d, n = 0;
        switch (req->bRequest) {
        case HL_REQ_GET_VERSION:
            if (!in)
                return false;
            r = version_reported(); s = version_stored(); d = STLINK_VERSION_DEFAULT;
            buf[0] = r >> 8; buf[1] = r;
            buf[2] = s >> 8; buf[3] = s;
            buf[4] = d >> 8; buf[5] = d;
            buf[6] = HYBRIDLINK_VER_MAJOR;
            buf[7] = HYBRIDLINK_VER_MINOR;
            buf[8] = HYBRIDLINK_VER_PATCH;     /* not sent before 0.5.1 */
            n = 9;
            break;
        case HL_REQ_GET_MODE:
            if (!in)
                return false;
            buf[0] = mode;
            buf[1] = (uint8_t)cfg_read16(CFG_MODE_ADDR);
            n = 2;
            break;
        case HL_REQ_SET_VERSION:
            if (in || !version_valid(req->wValue))
                return false;
            return tud_control_status(rhport, req);
        case HL_REQ_SET_MODE:
            if (in || (req->wValue >= MODE_COUNT && req->wValue != 0xFFFF))
                return false;
            return tud_control_status(rhport, req);
        case HL_REQ_ENTER_DFU:
            if (in)
                return false;
            return tud_control_status(rhport, req);
        default:
            return false;
        }
        return tud_control_xfer(rhport, req, buf, req->wLength < n ? req->wLength : n);
    }

    if (stage == CONTROL_STAGE_ACK && !in) {
        /* flash work and restarts happen after the status stage */
        switch (req->bRequest) {
        case HL_REQ_SET_VERSION:
            pending.addr = CFG_VERSION_ADDR;
            pending.val = req->wValue;
            pending.set = true;
            restart_after_store = RESTART_NONE;
            break;
        case HL_REQ_SET_MODE:
            pending.addr = CFG_MODE_ADDR;
            pending.val = req->wValue;
            pending.set = true;
            restart_after_store = RESTART_APP;
            break;
        case HL_REQ_ENTER_DFU:
            restart = RESTART_DFU;
            break;
        }
    }
    return true;
}

void settings_task(void)
{
    if (pending.set) {
        pending.set = false;
        settings_store16(pending.addr, pending.val);
        if (restart_after_store != RESTART_NONE)
            restart = restart_after_store;
    }
}
