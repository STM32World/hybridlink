/*
 * WCH two-wire RVSWD: access to the RISC-V debug module (DMI) of
 * CH32V1xx/2xx/3xx targets, bit-banged through swd_io.h.
 * SPDX-License-Identifier: MIT
 */
#ifndef RVSWD_H
#define RVSWD_H

#include <stdbool.h>
#include <stdint.h>

/* DMI op / status codes as in the RISC-V debug spec */
#define DMI_OP_NOP      0
#define DMI_OP_READ     1
#define DMI_OP_WRITE    2
#define DMI_OK          0
#define DMI_FAILED      2

void     rvswd_init(void);
void     rvswd_set_delay(uint32_t d);   /* extra delay steps per half clock */
void     rvswd_wake(void);              /* 100 clocks with DIO high, then STOP */
uint8_t  rvswd_read(uint8_t addr, uint32_t *val);
uint8_t  rvswd_write(uint8_t addr, uint32_t val);

#endif
