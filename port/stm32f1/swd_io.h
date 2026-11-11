/*
 * Fast SWD pin primitives for the STM32F1 port (bit-banged GPIO).
 * Included by the portable SWD engine; must provide:
 *   swd_io_clk_hi/lo, swd_io_dio_set, swd_io_dio_get,
 *   swd_io_dio_output, swd_io_dio_input, swd_io_delay(n)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SWD_IO_H
#define SWD_IO_H

#include <stdint.h>
#include "stm32f1.h"
#include "board.h"

#define SWD_CR(port, pin) (*(volatile uint32_t *)((port) + ((pin) >= 8 ? 4u : 0u)))
#define SWD_SHIFT(pin)    (((pin) & 7u) * 4u)

static inline __attribute__((always_inline)) void swd_io_clk_hi(void)
{
    GPIO_BSRR(SWCLK_PORT) = 1u << SWCLK_PIN;
}

static inline __attribute__((always_inline)) void swd_io_clk_lo(void)
{
    GPIO_BRR(SWCLK_PORT) = 1u << SWCLK_PIN;
}

static inline __attribute__((always_inline)) void swd_io_dio_set(uint32_t bit)
{
    GPIO_BSRR(SWDIO_PORT) = (bit & 1u) ? (1u << SWDIO_PIN) : (1u << (SWDIO_PIN + 16));
}

static inline __attribute__((always_inline)) uint32_t swd_io_dio_get(void)
{
    return (GPIO_IDR(SWDIO_IN_PORT) >> SWDIO_IN_PIN) & 1u;
}

static inline __attribute__((always_inline)) void swd_io_dio_output(void)
{
    volatile uint32_t *cr = &SWD_CR(SWDIO_PORT, SWDIO_PIN);
    *cr = (*cr & ~(0xFu << SWD_SHIFT(SWDIO_PIN))) | (GPIO_OUT_PP_50 << SWD_SHIFT(SWDIO_PIN));
}

static inline __attribute__((always_inline)) void swd_io_dio_input(void)
{
    volatile uint32_t *cr = &SWD_CR(SWDIO_PORT, SWDIO_PIN);
    *cr = (*cr & ~(0xFu << SWD_SHIFT(SWDIO_PIN))) | (GPIO_IN_FLOAT << SWD_SHIFT(SWDIO_PIN));
}

/* fixed length delay loop: aligned so its timing does not depend on where
 * the compiler places it (flash wait states / prefetch) */
static inline __attribute__((always_inline)) void swd_io_delay(uint32_t n)
{
    if (n)
        __asm volatile (
            ".p2align 3\n"
            "1: subs %0, %0, #1\n"
            "   bne 1b\n"
            : "+r" (n) : : "cc");
}

/* Delay steps (see swd.c: extra loop iterations per bit, split over the
 * two clock halves) for a SWD clock of at most hz. Measured on a 72 MHz
 * STM32F103 with the SWD_CALIBRATE build (CPU cycles per bit, the faster of
 * read and write, rounded down): steps 0..4 from a table, beyond that 6
 * cycles per step. Step 0 is about 3.7 MHz reading, 2.8 MHz writing. */
#define SWD_IO_CPU_HZ   72000000u

static inline uint32_t swd_io_delay_for(uint32_t hz)
{
    static const uint8_t period[] = { 19, 27, 30, 35, 38 };
    if (hz == 0)
        hz = 1;
    uint32_t target = (SWD_IO_CPU_HZ + hz - 1u) / hz;   /* min cycles per bit */
    for (uint32_t d = 0; d < sizeof(period); d++)
        if (period[d] >= target)
            return d;
    uint32_t d = (target - 14u + 5u) / 6u;
    return d < sizeof(period) ? sizeof(period) : d;
}

#endif
