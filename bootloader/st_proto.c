/*
 * ST-Link bootloader update protocol.
 *
 * The genuine bootloader tunnels a DfuSe-like state machine through the
 * ST-Link bulk endpoints as "F3 <request>" commands:
 *
 *   F3 01 blk cks len  DNLOAD, followed by len data bytes
 *                      block 0: 0x21 addr = set pointer, 0x41 addr = erase page
 *                      block >= 2: AES encrypted data written at the pointer
 *   F3 03              GETSTATUS (6 bytes: status, poll timeout, state, 0)
 *   F3 04 / 05 / 06    CLRSTATUS / GETSTATE / ABORT
 *   F3 07              leave and start the application
 *   F3 08              device info (raw 0x1FFFF7E0..F3, byte 4 = hw type)
 *   F3 09 n            n bytes of board info from the configuration page
 *   F3 0A              bootloader version
 *   F1 / F5            version and mode, like the application
 *
 * Data blocks are AES-128 encrypted with a per-device key derived from the
 * chip's flash size word and unique ID. The checksum in the DNLOAD command
 * is the 16-bit sum of the plain text.
 *
 * Interoperability note: the device key is derived with ST's master key.
 * Build with ST_COMPAT=0 to leave this protocol out.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "aes128.h"
#include "hal.h"
#include "image.h"
#include "layout.h"
#include "st_proto.h"
#include "usb_ids.h"
#include "settings.h"

#define EP_ST_IN    0x81
#define EP_ST_OUT   0x02
#define ITF_ST      0

#define CFG_PAGE    CFG_BASE
#define CFG_HWTYPE  (CFG_PAGE + 0x20)
#define CFG_INFO    (CFG_PAGE + 0x30)

/* A blank configuration page (hybridlink installed over SWD, never touched
 * by ST's updater) reads as firmware type 0xFF, which ST's updater treats
 * as "blank": its GUI then shows V0J0S0 and refuses to upgrade. While the
 * page is blank, report what the updater itself records for an ST-Link/V2-1
 * running "STM32 Debug+VCP": firmware type 'B' in the device info (F3 08,
 * byte 4) and the "DV" marker at 0x46 in the board info (F3 09). Without
 * "DV" it picks the Debug+Mass storage+VCP variant instead. */
#ifndef ST_DEFAULT_HWTYPE
#define ST_DEFAULT_HWTYPE 'B'
#endif

static bool cfg_blank(void)
{
    return *(const uint8_t *)CFG_HWTYPE == 0xFF;
}

static uint8_t default_info(uint32_t addr)
{
    if (addr == CFG_PAGE + 0x46) return 'D';
    if (addr == CFG_PAGE + 0x47) return 'V';
    return 0xFF;
}

enum { S_IDLE = 2, S_DNSYNC = 3, S_DNBUSY = 4, S_DNIDLE = 5, S_MANSYNC = 6, S_ERROR = 10 };
enum { E_OK = 0, E_ERASE = 4, E_PROG = 6, E_VERIFY = 7, E_ADDRESS = 8,
       E_UNKNOWN = 14, E_STALLED = 15 };

static uint8_t rhport_used;
static bool opened;
static uint8_t cmd[64];
static uint8_t data[1024 + 16] __attribute__((aligned(4)));
static uint8_t reply[64];
static enum { RX_CMD, RX_DATA } rx;
static bool cmd_ready, data_ready;

static uint8_t state = S_IDLE, status = E_OK;
static uint8_t op;                  /* 0x21 set pointer, 0x41 erase, 0x51 data */
static uint32_t pointer;
static uint16_t blk, cks, dlen;
static bool op_pending, op_done;
static bool exit_req;

static struct aes128 aes;
static bool key_ready;

/* ---- crypto ------------------------------------------------------------ */

static void swap32(uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i += 4) {
        uint8_t t0 = b[i], t1 = b[i + 1];
        b[i] = b[i + 3]; b[i + 1] = b[i + 2]; b[i + 2] = t1; b[i + 3] = t0;
    }
}

/* ST runs AES on per-word byte swapped key and data */
static void st_aes_init(const uint8_t key[16])
{
    uint8_t k[16];
    memcpy(k, key, 16);
    swap32(k, 16);
    aes128_init(&aes, k);
}

static void derive_key(void)
{
#if ST_COMPAT
    static const uint8_t master[16] = "I am key, wawawa";
    uint8_t m[16];
    memcpy(m, (const void *)0x1FFFF7E0u, 4);
    memcpy(m + 4, (const void *)0x1FFFF7E8u, 12);
    st_aes_init(master);
    swap32(m, 16);
    aes128_encrypt(&aes, m);
    swap32(m, 16);
    st_aes_init(m);
#endif
    key_ready = true;
}

static void decrypt(uint8_t *b, unsigned n)
{
    if (!key_ready)
        derive_key();
    swap32(b, n);
    for (unsigned i = 0; i < n; i += 16)
        aes128_decrypt(&aes, b + i);
    swap32(b, n);
}

/* ---- operations ------------------------------------------------------- */

static bool range_ok(uint32_t a, uint32_t n)
{
    return a >= CFG_PAGE && a + n <= image_flash_end() && a + n >= a;
}

static void fail(uint8_t code)
{
    state = S_ERROR;
    status = code;
}

static void run_op(void)
{
    uint16_t sum = 0;

    if (blk == 0) {
        for (unsigned i = 0; i < dlen; i++)
            sum += data[i];
        if (sum != cks || dlen != 5)
            return fail(E_VERIFY);
        uint32_t a = data[1] | (data[2] << 8) | (data[3] << 16) | ((uint32_t)data[4] << 24);
        if (data[0] == 0x21) {
            pointer = a;
            op = 0x51;
        } else if (data[0] == 0x41) {
            if ((a & (hal_flash_page_size() - 1)) || !range_ok(a, 1))
                return fail(E_ADDRESS);
            if (!image_erase(a))
                return fail(E_ERASE);
        } else {
            return fail(E_UNKNOWN);
        }
        return;
    }
    if (op != 0x51 || (dlen & 15u))
        return fail(E_UNKNOWN);
    decrypt(data, dlen);
    for (unsigned i = 0; i < dlen; i++)
        sum += data[i];
    if (sum != cks)
        return fail(E_VERIFY);
    if (!range_ok(pointer, dlen))
        return fail(E_ADDRESS);
    if (!image_write(pointer, data, dlen))
        return fail(E_PROG);
}

/* ---- command handling ---------------------------------------------------- */

static void send(uint32_t n)
{
    usbd_edpt_xfer(rhport_used, EP_ST_IN, reply, (uint16_t)n, false);
}

static void arm_cmd(void)
{
    rx = RX_CMD;
    usbd_edpt_xfer(rhport_used, EP_ST_OUT, cmd, sizeof(cmd), false);
}

static void st_version(void)
{
    uint16_t ver = version_reported();
    reply[0] = ver >> 8;
    reply[1] = ver;
    reply[2] = DFU_USB_VID & 0xFF;
    reply[3] = DFU_USB_VID >> 8;
    reply[4] = DFU_USB_PID & 0xFF;
    reply[5] = DFU_USB_PID >> 8;
    send(6);
}

static void getstatus(void)
{
    uint8_t poll = 0;
    if (state == S_DNSYNC) {
        if (op_done) {
            state = S_DNIDLE;
        } else {
            state = S_DNBUSY;
            op_pending = true;
            if (blk != 0 || data[0] == 0x41)
                poll = 0x50;
        }
    } else if (state == S_DNBUSY && op_done && state != S_ERROR) {
        state = S_DNIDLE;
    }
    reply[0] = status;
    reply[1] = poll;
    reply[2] = 0;
    reply[3] = 0;
    reply[4] = state;
    reply[5] = 0;
    send(6);
}

/* returns true when a data phase follows */
static bool command(void)
{
    if (cmd[0] == 0xF1) {
        st_version();
        return false;
    }
    if (cmd[0] == 0xF5) {
        reply[0] = 0;
        reply[1] = 2;
        send(2);
        return false;
    }
    if (cmd[0] != 0xF3)
        return false;

    switch (cmd[1]) {
    case 0x01:                                  /* DNLOAD */
        if (state != S_IDLE && state != S_DNIDLE) {
            fail(E_STALLED);
            return false;
        }
        blk = cmd[2] | (cmd[3] << 8);
        cks = cmd[4] | (cmd[5] << 8);
        dlen = cmd[6] | (cmd[7] << 8);
        if (dlen == 0) {
            state = S_MANSYNC;
            return false;
        }
        if (dlen > 1024) {
            fail(E_STALLED);
            return false;
        }
        rx = RX_DATA;
        usbd_edpt_xfer(rhport_used, EP_ST_OUT, data, dlen, false);
        return true;
    case 0x03:
        getstatus();
        break;
    case 0x04:                                  /* CLRSTATUS */
        if (state == S_ERROR) {
            state = S_IDLE;
            status = E_OK;
        } else {
            fail(E_UNKNOWN);
        }
        break;
    case 0x05:                                  /* GETSTATE */
        reply[0] = status;
        reply[1] = state;
        send(2);
        break;
    case 0x06:                                  /* ABORT */
        state = S_IDLE;
        status = E_OK;
        op = 0;
        break;
    case 0x07:                                  /* leave */
        exit_req = true;
        break;
    case 0x08:                                  /* device info */
        memcpy(reply, (const void *)0x1FFFF7E0u, 20);
        reply[4] = cfg_blank() ? ST_DEFAULT_HWTYPE : *(const uint8_t *)CFG_HWTYPE;
        send(20);
        break;
    case 0x09: {                                /* board info */
        uint8_t n = cmd[2] > 0x40 ? 0x40 : cmd[2];
        if (cfg_blank())
            for (uint8_t i = 0; i < n; i++)
                reply[i] = default_info(CFG_INFO + i);
        else
            memcpy(reply, (const void *)CFG_INFO, n);
        send(n);
        break;
    }
    case 0x0A:                                  /* bootloader version */
        memset(reply, 0, 16);
        reply[3] = 0x21;
        send(16);
        break;
    default:
        break;
    }
    return false;
}

void st_proto_task(void)
{
    if (!opened)
        return;
    if (op_pending) {
        op_pending = false;
        run_op();
        op_done = true;
    }
    if (data_ready) {
        data_ready = false;
        state = S_DNSYNC;
        op_done = false;
        arm_cmd();
    }
    if (cmd_ready && !usbd_edpt_busy(rhport_used, EP_ST_IN)) {
        cmd_ready = false;
        if (!command())
            arm_cmd();
    }
}

bool st_proto_exit_requested(void)
{
    return exit_req;
}

/* ---- TinyUSB class driver ------------------------------------------------ */

static void drv_init(void) { opened = false; }
static bool drv_deinit(void) { return true; }
static void drv_reset(uint8_t rhport) { (void)rhport; opened = false; }

static uint16_t drv_open(uint8_t rhport, const tusb_desc_interface_t *itf, uint16_t max_len)
{
    if (itf->bInterfaceClass != 0xFF || itf->bInterfaceNumber != ITF_ST)
        return 0;
    uint16_t len = sizeof(tusb_desc_interface_t);
    const uint8_t *p = tu_desc_next(itf);
    for (int i = 0; i < itf->bNumEndpoints; i++) {
        if (!usbd_edpt_open(rhport, (const tusb_desc_endpoint_t *)p))
            return 0;
        len += tu_desc_len(p);
        p = tu_desc_next(p);
    }
    if (len > max_len)
        return 0;
    rhport_used = rhport;
    opened = true;
    cmd_ready = data_ready = false;
    arm_cmd();
    return len;
}

static bool drv_control(uint8_t rhport, uint8_t stage, const tusb_control_request_t *req)
{
    (void)rhport; (void)stage; (void)req;
    return false;
}

static bool drv_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t n)
{
    (void)rhport; (void)result;
    if (ep != EP_ST_OUT)
        return true;
    if (rx == RX_DATA) {
        dlen = (uint16_t)n;
        data_ready = true;
    } else if (n >= 2) {
        cmd_ready = true;
    } else {
        arm_cmd();
    }
    return true;
}

static const usbd_class_driver_t st_driver = {
    .name = "STBOOT",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control,
    .xfer_cb = drv_xfer,
};

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count)
{
    *count = 1;
    return &st_driver;
}
