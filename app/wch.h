/*
 * WCH-Link (firmware 2.12) compatible command set, docs/wch.md §3.
 * SPDX-License-Identifier: MIT
 */
#ifndef WCH_H
#define WCH_H

#include <stdbool.h>
#include <stdint.h>

#define WCH_PACKET_SIZE 64

void     wch_init(void);
/* one request from EP 0x01 -> reply for EP 0x81 (length, 0 = none) */
uint16_t wch_execute(const uint8_t *req, uint16_t len, uint8_t *resp);
/* data stream: packets from EP 0x02; true while more are wanted */
bool     wch_data_wanted(void);
void     wch_data_out(const uint8_t *buf, uint16_t len);
void     wch_task(void);

/* provided by wch_usb.c */
bool     wch_usb_data_in(const uint8_t *buf, uint16_t len);  /* false: busy */
bool     wch_usb_data_in_busy(void);

#endif
