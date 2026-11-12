/*
 * Portable SWD / ADIv5 engine (bit-banged through swd_io.h).
 * SPDX-License-Identifier: MIT
 */
#ifndef SWD_H
#define SWD_H

#include <stdbool.h>
#include <stdint.h>

/* raw SWD acknowledge values, plus local errors */
#define SWD_ACK_OK      1
#define SWD_ACK_WAIT    2
#define SWD_ACK_FAULT   4
#define SWD_ERR_PARITY  8
#define SWD_ERR_NOACK   7

/* result of the higher level calls: an ST-Link status byte */
#include "stlink_status.h"

void     swd_init(void);
void     swd_set_delay(uint32_t half_period_loops);
void     swd_line_reset(void);
/* full connect: JTAG-to-SWD, line reset, read IDCODE, power up debug */
uint8_t  swd_connect(uint32_t *idcode);
void     swd_disconnect(void);

uint8_t  dp_read(uint8_t addr, uint32_t *val);      /* addr = bank<<4 | reg */
uint8_t  dp_write(uint8_t addr, uint32_t val);
uint8_t  ap_read(uint8_t ap, uint8_t addr, uint32_t *val);
uint8_t  ap_write(uint8_t ap, uint8_t addr, uint32_t val);
uint8_t  swd_clear_errors(void);

/* SWD clock in Hz (rounded down to what the port can do) */
void     swd_set_clock(uint32_t hz);

/* raw access for protocol layers that drive SWD themselves (CMSIS-DAP).
 * req bits: 0 APnDP, 1 RnW, 2..3 A[3:2]; returns the ACK (SWD_ACK_* or
 * SWD_ERR_*), no WAIT retries. The cached SELECT / CSW state of the
 * higher level calls is not updated: call swd_invalidate() afterwards. */
unsigned swd_raw_transfer(unsigned req, uint32_t *data);
/* DP write whose ACK is ignored and whose data phase is always driven
 * (TARGETSEL in multidrop SWD: no target answers it) */
void     swd_raw_write_noack(unsigned req, uint32_t data);
void     swd_raw_out(const uint8_t *data, unsigned bits);   /* LSB first */
void     swd_raw_in(uint8_t *data, unsigned bits);
void     swd_raw_idle(unsigned cycles);
void     swd_invalidate(void);

/* MEM-AP accesses on the selected AP */
void     mem_set_ap(uint8_t ap);
uint8_t  mem_get_ap(void);
uint8_t  mem_ap_init(uint8_t ap);
uint8_t  mem_read32(uint32_t addr, uint32_t *val);
uint8_t  mem_write32(uint32_t addr, uint32_t val);
uint8_t  mem_read(uint32_t addr, uint8_t *buf, uint32_t len, unsigned size, bool inc);
uint8_t  mem_write(uint32_t addr, const uint8_t *buf, uint32_t len, unsigned size, bool inc);
uint32_t mem_fault_addr(void);

#endif
