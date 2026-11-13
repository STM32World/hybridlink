/*
 * Target flash programming for the WCH-Link command set: done by the probe
 * itself through the debug module (the flash routine hosts upload is
 * received and discarded).
 * SPDX-License-Identifier: MIT
 */
#ifndef WCH_FLASH_H
#define WCH_FLASH_H

#include <stdbool.h>
#include <stdint.h>

void wch_flash_init(void);
bool wch_flash_mass_erase(void);
bool wch_flash_protect(void);
bool wch_flash_unprotect(void);
void wch_flash_routine_upload(void);            /* swallow the next 512 B */
/* check: read every page back (02 04, write with verify) */
void wch_flash_write_start(uint32_t addr, uint32_t len, bool check);
void wch_flash_write_end(void);
bool wch_flash_wants_data(void);
void wch_flash_data(const uint8_t *buf, uint16_t len);
bool wch_flash_ack_pending(uint8_t ack[4]);
void wch_flash_ack_sent(void);

#endif
