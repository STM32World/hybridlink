/*
 * Bring-up aid for WCH's two-wire RVSWD: a tiny bit-level script engine
 * reachable through hybridlink vendor requests, so the wire protocol can be
 * explored from the host without a logic analyser. Built only with
 * `make RVSWD_RAW=1`.
 *
 *   OUT HL_REQ_RVSWD_RUN  data = script (up to 256 bytes), run at once
 *   IN  HL_REQ_RVSWD_RES  result bytes of the last run
 *
 * Script ops (the clock idles high; the host changes DIO while CLK is low):
 *   01 n b..  drive DIO, send n bits MSB first (data MSB first per byte)
 *   02 n      release DIO, clock n bits in; appends ceil(n/8) bytes sampled
 *             late in CLK low, then ceil(n/8) bytes sampled late in CLK high
 *   03        START: DIO falls while CLK is high, then CLK low
 *   04        STOP: CLK low, DIO low, CLK high, DIO rises
 *   05 n      n clocks with DIO driven high
 *   06 d      delay steps per half clock
 *   07        release DIO            08  drive DIO
 *   09 v      set DIO level          0a  append the DIO level (one byte)
 *   0b        CLK high               0c  CLK low
 *
 * SPDX-License-Identifier: MIT
 */
#if RVSWD_RAW
#include <string.h>
#include "tusb.h"
#include "settings.h"
#include "swd_io.h"

static uint8_t script[256], result[256];
static uint16_t script_len, result_len;
static uint32_t dly = 40;

static void half(void) { swd_io_delay(dly); }

static void put_bit(uint8_t *base, unsigned i, uint32_t v)
{
    if (v)
        base[i >> 3] |= 0x80u >> (i & 7u);
}

static void run(void)
{
    unsigned p = 0;
    result_len = 0;
    memset(result, 0, sizeof result);
    while (p < script_len) {
        uint8_t op = script[p++];
        switch (op) {
        case 0x01: {
            unsigned n = script[p++];
            const uint8_t *b = &script[p];
            p += (n + 7u) / 8u;
            swd_io_dio_output();
            for (unsigned i = 0; i < n; i++) {
                swd_io_clk_lo();
                swd_io_dio_set((b[i >> 3] >> (7u - (i & 7u))) & 1u);
                half();
                swd_io_clk_hi();
                half();
            }
            break;
        }
        case 0x02: {
            unsigned n = script[p++], nb = (n + 7u) / 8u;
            if (result_len + 2u * nb > sizeof result)
                return;
            uint8_t *lo = &result[result_len], *hi = lo + nb;
            swd_io_dio_input();
            for (unsigned i = 0; i < n; i++) {
                swd_io_clk_lo();
                half();
                put_bit(lo, i, swd_io_dio_get());
                swd_io_clk_hi();
                half();
                put_bit(hi, i, swd_io_dio_get());
            }
            result_len += 2u * nb;
            break;
        }
        case 0x03:
            swd_io_dio_set(1);
            swd_io_dio_output();
            swd_io_clk_hi();
            half();
            swd_io_dio_set(0);
            half();
            swd_io_clk_lo();
            half();
            break;
        case 0x04:
            swd_io_clk_lo();
            swd_io_dio_set(0);
            swd_io_dio_output();
            half();
            swd_io_clk_hi();
            half();
            swd_io_dio_set(1);
            half();
            break;
        case 0x05: {
            unsigned n = script[p++];
            swd_io_dio_set(1);
            swd_io_dio_output();
            for (unsigned i = 0; i < n; i++) {
                swd_io_clk_lo();
                half();
                swd_io_clk_hi();
                half();
            }
            break;
        }
        case 0x06: dly = script[p++]; break;
        case 0x07: swd_io_dio_input(); break;
        case 0x08: swd_io_dio_output(); break;
        case 0x09: swd_io_dio_set(script[p++]); break;
        case 0x0a:
            if (result_len < sizeof result)
                result[result_len++] = (uint8_t)swd_io_dio_get();
            break;
        case 0x0b: swd_io_clk_hi(); break;
        case 0x0c: swd_io_clk_lo(); break;
        default: return;
        }
    }
}

bool rvswd_raw_request(uint8_t rhport, uint8_t stage, const void *request)
{
    const tusb_control_request_t *req = request;
    if (req->wIndex != HL_REQ_MAGIC ||
        (req->bRequest != HL_REQ_RVSWD_RUN && req->bRequest != HL_REQ_RVSWD_RES))
        return false;
    bool in = req->bmRequestType_bit.direction == TUSB_DIR_IN;
    if (req->bRequest == HL_REQ_RVSWD_RES) {
        if (!in)
            return false;
        if (stage == CONTROL_STAGE_SETUP)
            return tud_control_xfer(rhport, req, result,
                                    req->wLength < result_len ? req->wLength : result_len);
        return true;
    }
    if (in || req->wLength > sizeof script)
        return false;
    if (stage == CONTROL_STAGE_SETUP) {
        script_len = req->wLength;
        return tud_control_xfer(rhport, req, script, script_len);
    }
    if (stage == CONTROL_STAGE_DATA || (stage == CONTROL_STAGE_ACK && script_len == 0))
        run();
    return true;
}
#endif
