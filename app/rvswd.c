/*
 * WCH RVSWD (two-wire) packet layer, as measured on a CH32V307
 * (docs/wch.md §1). The clock idles high; the host changes DIO while CLK is
 * low and the target samples on the rising edge; bits go MSB first.
 *
 *   START  DIO falls while CLK is high
 *   host   addr[7] rw (1 = write), parity twice, 4 zeros
 *   write: host data[32], parity twice; target status[4]
 *   read:  target data[32], parity, 1, status[4]
 *   STOP   DIO rises while CLK is high
 *
 * Parity is even (over the header or the data). Status 0111 means done.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rvswd.h"
#include "swd_io.h"

#define STATUS_OK   0x7u
#define RETRIES     8

static uint32_t delay;

void rvswd_set_delay(uint32_t d)
{
    delay = d;
}

static inline void half(void)
{
    swd_io_delay(delay);
}

static inline uint32_t parity32(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1u;
}

static void out_bits(uint32_t v, unsigned n)
{
    while (n--) {
        swd_io_clk_lo();
        swd_io_dio_set(v >> n);
        half();
        swd_io_clk_hi();
        half();
    }
}

static uint32_t in_bits(unsigned n)
{
    uint32_t v = 0;
    while (n--) {
        swd_io_clk_lo();
        half();
        swd_io_clk_hi();
        half();
        v = (v << 1) | swd_io_dio_get();
    }
    return v;
}

static void start(void)
{
    swd_io_dio_set(1);
    swd_io_dio_output();
    swd_io_clk_hi();
    half();
    swd_io_dio_set(0);
    half();
}

static void stop(void)
{
    swd_io_clk_lo();
    swd_io_dio_set(0);
    swd_io_dio_output();
    half();
    swd_io_clk_hi();
    half();
    swd_io_dio_set(1);
    half();
}

static void header(uint8_t addr, uint32_t rw)
{
    uint32_t h = ((uint32_t)(addr & 0x7Fu) << 1) | rw;
    start();
    out_bits(h, 8);
    out_bits(parity32(h) * 3u, 2);
    out_bits(0, 4);
}

void rvswd_init(void)
{
    swd_io_clk_hi();
    swd_io_dio_set(1);
    swd_io_dio_output();
}

void rvswd_wake(void)
{
    swd_io_dio_set(1);
    swd_io_dio_output();
    for (unsigned i = 0; i < 100; i++) {
        swd_io_clk_lo();
        half();
        swd_io_clk_hi();
        half();
    }
    stop();
}

uint8_t rvswd_read(uint8_t addr, uint32_t *val)
{
    for (int i = 0; i < RETRIES; i++) {
        header(addr, 0);
        swd_io_dio_input();
        uint32_t v = in_bits(32);
        uint32_t tail = in_bits(6);     /* parity, 1, status[4] */
        stop();
        if ((tail & 0xFu) == STATUS_OK && ((tail >> 5) & 1u) == parity32(v)) {
            *val = v;
            return DMI_OK;
        }
    }
    *val = 0;
    return DMI_FAILED;
}

uint8_t rvswd_write(uint8_t addr, uint32_t val)
{
    for (int i = 0; i < RETRIES; i++) {
        header(addr, 1);
        out_bits(val, 32);
        out_bits(parity32(val) * 3u, 2);
        swd_io_dio_input();
        uint32_t st = in_bits(4);
        stop();
        if (st == STATUS_OK)
            return DMI_OK;
    }
    return DMI_FAILED;
}
