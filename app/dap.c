/*
 * CMSIS-DAP command processor, SWD only.
 *
 * Written from the published CMSIS-DAP protocol description (version 2.1);
 * it uses the portable SWD engine (swd.c) for the wire protocol and the
 * HAL for reset, LED and SWO.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "dap.h"
#include "swd.h"
#include "swd_io.h"
#include "hal.h"
#include "usb_ids.h"
#include "usb_serial.h"

/* command IDs */
#define ID_INFO             0x00
#define ID_HOST_STATUS      0x01
#define ID_CONNECT          0x02
#define ID_DISCONNECT       0x03
#define ID_TRANSFER_CONFIG  0x04
#define ID_TRANSFER         0x05
#define ID_TRANSFER_BLOCK   0x06
#define ID_TRANSFER_ABORT   0x07
#define ID_WRITE_ABORT      0x08
#define ID_DELAY            0x09
#define ID_RESET_TARGET     0x0A
#define ID_SWJ_PINS         0x10
#define ID_SWJ_CLOCK        0x11
#define ID_SWJ_SEQUENCE     0x12
#define ID_SWD_CONFIGURE    0x13
#define ID_JTAG_SEQUENCE    0x14
#define ID_JTAG_CONFIGURE   0x15
#define ID_JTAG_IDCODE      0x16
#define ID_SWO_TRANSPORT    0x17
#define ID_SWO_MODE         0x18
#define ID_SWO_BAUDRATE     0x19
#define ID_SWO_CONTROL      0x1A
#define ID_SWO_STATUS       0x1B
#define ID_SWO_DATA         0x1C
#define ID_SWD_SEQUENCE     0x1D
#define ID_SWO_EXT_STATUS   0x1E
#define ID_QUEUE_COMMANDS   0x7E
#define ID_EXECUTE_COMMANDS 0x7F
#define ID_INVALID          0xFF

#define DAP_OK              0x00
#define DAP_ERROR           0xFF

/* transfer request bits */
#define XFER_APnDP          (1u << 0)
#define XFER_RnW            (1u << 1)
#define XFER_MATCH_VALUE    (1u << 4)
#define XFER_MATCH_MASK     (1u << 5)
#define XFER_TIMESTAMP      (1u << 7)
#define XFER_MISMATCH       (1u << 4)       /* response bit */
#define XFER_ERROR          (1u << 3)       /* response bit: protocol error */

#define DP_ABORT_REQ        0x00            /* DP write, A = 0x0 */
#define DP_RDBUFF_REQ       (XFER_RnW | 0x0C)
#define DP_TARGETSEL_REQ    0x0C            /* DP write, A = 0xC */

#define SWO_BUFFER_SIZE     4096u

static struct {
    uint8_t  idle;
    uint16_t wait_retry;
    uint16_t match_retry;
    uint32_t match_mask;
    uint8_t  pins;                          /* last SWJ_Pins output */
    bool     swo_on;
    uint8_t  swo_transport, swo_mode;
    uint32_t swo_baud;
} dap;

static inline uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

void dap_init(void)
{
    memset(&dap, 0, sizeof(dap));
    dap.wait_retry = 100;
    dap.match_retry = 0;
    dap.pins = 0xFF;
    hal_target_init();
    swd_init();
    swd_set_clock(1000000);
}

/* ---- SWD transfer helpers --------------------------------------------- */

static unsigned xfer(unsigned req, uint32_t *data)
{
    unsigned ack;
    uint32_t n = dap.wait_retry;

    if ((req & (XFER_APnDP | XFER_RnW | 0x0C)) == DP_TARGETSEL_REQ) {
        swd_raw_write_noack(req, *data);
        return SWD_ACK_OK;
    }
    for (;;) {
        ack = swd_raw_transfer(req, data);
        if (dap.idle)
            swd_raw_idle(dap.idle);
        if (ack != SWD_ACK_WAIT || n-- == 0)
            return ack;
    }
}

/* response value for an ACK */
static uint8_t ack_resp(unsigned ack)
{
    if (ack == SWD_ERR_PARITY)
        return XFER_ERROR;
    if (ack == SWD_ACK_OK || ack == SWD_ACK_WAIT || ack == SWD_ACK_FAULT)
        return ack;
    return 0x07;
}

/* DAP_Transfer: returns (request bytes consumed) << 16 | response bytes */
static uint32_t cmd_transfer(const uint8_t *q, uint32_t qlen, uint8_t *r, uint32_t rmax)
{
    const uint8_t *p = q + 3, *end = q + qlen;
    uint8_t *out = r + 3, *out_end = r + rmax;
    unsigned count = qlen >= 3 ? q[2] : 0, done = 0, ack = SWD_ACK_OK;
    bool post = false, check_write = false;
    uint32_t v;

    for (; done < count; done++) {
        if (p >= end)
            break;
        uint8_t rq = *p++;
        if (rq & XFER_RnW) {
            if (post) {
                if ((rq & (XFER_APnDP | XFER_MATCH_VALUE)) == XFER_APnDP) {
                    /* previous AP data, next AP read posted */
                    ack = xfer(rq, &v);
                } else {
                    ack = xfer(DP_RDBUFF_REQ, &v);
                    post = false;
                }
                if (ack != SWD_ACK_OK)
                    break;
                if (out + 4 > out_end) { ack = 0x07; break; }
                put32(out, v); out += 4;
            }
            if (rq & XFER_MATCH_VALUE) {
                if (p + 4 > end) break;
                uint32_t match = get32(p); p += 4;
                if (rq & XFER_APnDP) {
                    ack = xfer(rq, &v);     /* post the first read */
                    if (ack != SWD_ACK_OK)
                        break;
                }
                uint32_t n = dap.match_retry;
                do {
                    ack = xfer(rq, &v);
                    if (ack != SWD_ACK_OK)
                        break;
                } while ((v & dap.match_mask) != match && n-- != 0);
                if (ack != SWD_ACK_OK)
                    break;
                if ((v & dap.match_mask) != match) {
                    ack |= XFER_MISMATCH;
                    break;
                }
            } else if (!post) {
                if (rq & XFER_APnDP) {
                    ack = xfer(rq, &v);     /* posted, data comes later */
                    if (ack != SWD_ACK_OK)
                        break;
                    post = true;
                } else {
                    ack = xfer(rq, &v);
                    if (ack != SWD_ACK_OK)
                        break;
                    if (out + 4 > out_end) { ack = 0x07; break; }
                    put32(out, v); out += 4;
                }
            }
            check_write = false;
        } else {
            if (post) {
                ack = xfer(DP_RDBUFF_REQ, &v);
                if (ack != SWD_ACK_OK)
                    break;
                if (out + 4 > out_end) { ack = 0x07; break; }
                put32(out, v); out += 4;
                post = false;
            }
            if (p + 4 > end) break;
            v = get32(p); p += 4;
            if (rq & XFER_MATCH_MASK) {
                dap.match_mask = v;
                ack = SWD_ACK_OK;
            } else {
                ack = xfer(rq, &v);
                if (ack != SWD_ACK_OK)
                    break;
                check_write = true;
            }
        }
    }

    if ((ack & 7u) == SWD_ACK_OK && !(ack & XFER_MISMATCH)) {
        if (post) {
            ack = xfer(DP_RDBUFF_REQ, &v);
            if (ack == SWD_ACK_OK && out + 4 <= out_end) {
                put32(out, v); out += 4;
            }
        } else if (check_write) {
            ack = xfer(DP_RDBUFF_REQ, &v);
        }
    }

    swd_invalidate();
    r[0] = ID_TRANSFER;
    r[1] = (uint8_t)done;
    r[2] = (ack & XFER_MISMATCH) ? (XFER_MISMATCH | SWD_ACK_OK) : ack_resp(ack);
    /* request length, independent of where execution stopped */
    p = q + 3;
    for (unsigned i = 0; i < count && p < end; i++) {
        uint8_t rq = *p++;
        if (!(rq & XFER_RnW) || (rq & XFER_MATCH_VALUE))
            p += 4;
    }
    if (p > end)
        p = end;
    return ((uint32_t)(p - q) << 16) | (uint32_t)(out - r);
}

/* DAP_TransferBlock */
static uint32_t cmd_transfer_block(const uint8_t *q, uint32_t qlen, uint8_t *r, uint32_t rmax)
{
    const uint8_t *p = q + 5, *end = q + qlen;
    uint8_t *out = r + 4, *out_end = r + rmax;
    unsigned count = get16(q + 2), done = 0, ack = SWD_ACK_OK;
    uint8_t rq = q[4];
    uint32_t v;

    if (qlen < 5) {
        count = 0;
        p = end;
    }
    if (rq & XFER_RnW) {
        if (count && (rq & XFER_APnDP)) {
            ack = xfer(rq, &v);             /* posted */
        }
        while (ack == SWD_ACK_OK && done < count) {
            if (rq & XFER_APnDP)
                ack = xfer(done + 1 == count ? DP_RDBUFF_REQ : rq, &v);
            else
                ack = xfer(rq, &v);
            if (ack != SWD_ACK_OK)
                break;
            if (out + 4 > out_end) { ack = 0x07; break; }
            put32(out, v); out += 4;
            done++;
        }
    } else {
        while (done < count && p + 4 <= end) {
            v = get32(p); p += 4;
            ack = xfer(rq, &v);
            if (ack != SWD_ACK_OK)
                break;
            done++;
        }
        if (ack == SWD_ACK_OK && done)
            ack = xfer(DP_RDBUFF_REQ, &v);
        p = q + 5 + 4u * count;
    }
    swd_invalidate();
    r[0] = ID_TRANSFER_BLOCK;
    put16(r + 1, (uint16_t)done);
    r[3] = ack_resp(ack);
    if (p > end)
        p = end;
    return ((uint32_t)(p - q) << 16) | (uint32_t)(out - r);
}

/* ---- info ------------------------------------------------------------- */

static uint32_t info_string(uint8_t *r, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0) {
        r[1] = 0;
        return 2;
    }
    if (n > DAP_PACKET_SIZE - 3)
        n = DAP_PACKET_SIZE - 3;
    memcpy(r + 2, s, n);
    r[2 + n] = 0;
    r[1] = (uint8_t)(n + 1);
    return 3 + n;
}

static uint32_t cmd_info(uint8_t id, uint8_t *r)
{
    char sn[25];

    switch (id) {
    case 0x01: return info_string(r, DAP_USB_MANUFACTURER);
    case 0x02: return info_string(r, DAP_USB_PRODUCT);
    case 0x03: usb_serial_string(sn); return info_string(r, sn);
    case 0x04: return info_string(r, "2.1.1");
    case 0x09: return info_string(r, HYBRIDLINK_VERSION);
    case 0xF0:                              /* capabilities */
        r[1] = 1;
        r[2] = (1u << 0)                    /* SWD */
             | (1u << 2)                    /* SWO UART */
             | (1u << 4);                   /* atomic commands */
        return 3;
    case 0xFD:                              /* SWO trace buffer size */
        r[1] = 4;
        put32(r + 2, SWO_BUFFER_SIZE);
        return 6;
    case 0xFE:
        r[1] = 1;
        r[2] = DAP_PACKET_COUNT;
        return 3;
    case 0xFF:
        r[1] = 2;
        put16(r + 2, DAP_PACKET_SIZE);
        return 4;
    default:                                /* target / board info etc. */
        r[1] = 0;
        return 2;
    }
}

/* ---- SWO -------------------------------------------------------------- */

static uint8_t swo_status(void)
{
    return dap.swo_on ? 0x01 : 0x00;
}

/* ---- one command ------------------------------------------------------ */

#define RESULT(consumed, produced) (((uint32_t)(consumed) << 16) | (uint32_t)(produced))

static uint32_t execute_one(const uint8_t *q, uint32_t qlen, uint8_t *r, uint32_t rmax)
{
    uint32_t v;

    r[0] = q[0];
    switch (q[0]) {
    case ID_INFO:
        return RESULT(2, cmd_info(q[1], r));

    case ID_HOST_STATUS:
        if (q[1] == 0)
            hal_led(q[2] ? LED_ACTIVE : LED_IDLE);
        r[1] = DAP_OK;
        return RESULT(3, 2);

    case ID_CONNECT:
        if (q[1] == 0 || q[1] == 1) {
            swd_init();
            swd_invalidate();
            r[1] = 1;                       /* SWD */
        } else {
            r[1] = 0;
        }
        return RESULT(2, 2);

    case ID_DISCONNECT:
        swd_disconnect();
        hal_led(LED_IDLE);
        r[1] = DAP_OK;
        return RESULT(1, 2);

    case ID_TRANSFER_CONFIG:
        dap.idle = q[1];
        dap.wait_retry = get16(q + 2);
        dap.match_retry = get16(q + 4);
        r[1] = DAP_OK;
        return RESULT(6, 2);

    case ID_TRANSFER:
        return cmd_transfer(q, qlen, r, rmax);

    case ID_TRANSFER_BLOCK:
        return cmd_transfer_block(q, qlen, r, rmax);

    case ID_TRANSFER_ABORT:                 /* transfers run to completion */
        return RESULT(1, 0);

    case ID_WRITE_ABORT:
        v = get32(q + 2);
        r[1] = xfer(DP_ABORT_REQ, &v) == SWD_ACK_OK ? DAP_OK : DAP_ERROR;
        swd_invalidate();
        return RESULT(6, 2);

    case ID_DELAY:
        hal_delay_us(get16(q + 1));
        r[1] = DAP_OK;
        return RESULT(3, 2);

    case ID_RESET_TARGET:
        r[1] = DAP_OK;
        r[2] = 0;                           /* no device specific reset */
        return RESULT(1, 3);

    case ID_SWJ_PINS: {
        uint8_t out = q[1], sel = q[2];
        uint32_t wait = get32(q + 3);
        if (sel & 0x01) {
            if (out & 0x01)
                swd_io_clk_hi();
            else
                swd_io_clk_lo();
        }
        if (sel & 0x02) {
            swd_io_dio_set(out & 0x02 ? 1 : 0);
            swd_io_dio_output();
        }
        if (sel & 0x80) hal_target_nrst(!(out & 0x80));
        dap.pins = (dap.pins & ~sel) | (out & sel);
        if (wait) {
            uint32_t t = wait > 3000000u ? 3000000u : wait;
            if ((sel & 0x80) && !(out & 0x80))
                hal_delay_us(t > 1000u ? 1000u : t);
        }
        r[1] = (dap.pins & 0x81) | (swd_io_dio_get() ? 0x02 : 0);
        return RESULT(7, 2);
    }

    case ID_SWJ_CLOCK:
        v = get32(q + 1);
        if (v) {
            swd_set_clock(v);
            r[1] = DAP_OK;
        } else {
            r[1] = DAP_ERROR;
        }
        return RESULT(5, 2);

    case ID_SWJ_SEQUENCE: {
        unsigned bits = q[1] ? q[1] : 256;
        swd_raw_out(q + 2, bits);
        r[1] = DAP_OK;
        return RESULT(2 + (bits + 7) / 8, 2);
    }

    case ID_SWD_CONFIGURE:
        /* only turnaround = 1 and no data phase on WAIT / FAULT */
        r[1] = (q[1] & 0x07) == 0 ? DAP_OK : DAP_ERROR;
        return RESULT(2, 2);

    case ID_SWD_SEQUENCE: {
        unsigned n = q[1];
        const uint8_t *p = q + 2;
        uint8_t *o = r + 2;
        r[1] = DAP_OK;
        while (n--) {
            uint8_t info = *p++;
            unsigned bits = (info & 0x3F) ? (info & 0x3F) : 64;
            unsigned bytes = (bits + 7) / 8;
            if (info & 0x80) {
                if (o + bytes > r + rmax) { r[1] = DAP_ERROR; break; }
                swd_raw_in(o, bits);
                o += bytes;
            } else {
                swd_raw_out(p, bits);
                p += bytes;
            }
        }
        swd_invalidate();
        return RESULT(p - q, o - r);
    }

    case ID_SWO_TRANSPORT:
        r[1] = q[1] <= 1 ? DAP_OK : DAP_ERROR;
        if (q[1] <= 1)
            dap.swo_transport = q[1];
        return RESULT(2, 2);

    case ID_SWO_MODE:
        r[1] = q[1] <= 1 ? DAP_OK : DAP_ERROR;
        if (q[1] <= 1)
            dap.swo_mode = q[1];
        return RESULT(2, 2);

    case ID_SWO_BAUDRATE:
        dap.swo_baud = get32(q + 1);
        put32(r + 1, dap.swo_baud);
        return RESULT(5, 5);

    case ID_SWO_CONTROL:
        if (q[1] && dap.swo_mode == 1 && dap.swo_baud) {
            dap.swo_on = hal_swo_start(dap.swo_baud);
            r[1] = dap.swo_on ? DAP_OK : DAP_ERROR;
        } else if (!q[1]) {
            hal_swo_stop();
            dap.swo_on = false;
            r[1] = DAP_OK;
        } else {
            r[1] = DAP_ERROR;
        }
        return RESULT(2, 2);

    case ID_SWO_STATUS:
        r[1] = swo_status();
        put32(r + 2, (uint32_t)hal_swo_available());
        return RESULT(1, 6);

    case ID_SWO_EXT_STATUS:
        r[1] = swo_status();
        put32(r + 2, (uint32_t)hal_swo_available());
        return RESULT(2, 6);

    case ID_SWO_DATA: {
        uint32_t max = get16(q + 1), n = 0;
        if (max > rmax - 4)
            max = rmax - 4;
        while (n < max) {
            const uint8_t *p;
            uint32_t c = hal_swo_peek(&p);
            if (!c)
                break;
            if (c > max - n)
                c = max - n;
            memcpy(r + 4 + n, p, c);
            hal_swo_consume(c);
            n += c;
        }
        r[1] = swo_status();
        put16(r + 2, (uint16_t)n);
        return RESULT(3, 4 + n);
    }

    case ID_JTAG_SEQUENCE:
    case ID_JTAG_CONFIGURE:
    case ID_JTAG_IDCODE:
    default:
        r[0] = ID_INVALID;
        return RESULT(qlen, 1);
    }
}

uint32_t dap_execute(const uint8_t *req, uint32_t req_len, uint8_t *resp)
{
    if (req_len == 0)
        return 0;
    if (req[0] == ID_EXECUTE_COMMANDS || req[0] == ID_QUEUE_COMMANDS) {
        unsigned n = req[1];
        const uint8_t *q = req + 2;
        uint32_t left = req_len - 2, out = 2;
        resp[0] = ID_EXECUTE_COMMANDS;
        resp[1] = 0;
        while (n-- && left) {
            uint32_t res = execute_one(q, left, resp + out, DAP_PACKET_SIZE - out);
            uint32_t used = res >> 16, made = res & 0xFFFF;
            if (used > left)
                used = left;
            q += used;
            left -= used;
            out += made;
            resp[1]++;
            if (resp[out - made] == ID_INVALID)
                break;
        }
        return out;
    }
    return execute_one(req, req_len, resp, DAP_PACKET_SIZE) & 0xFFFF;
}
