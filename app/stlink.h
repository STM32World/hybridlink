/*
 * ST-Link/V2-1 command interpreter (transport independent).
 * SPDX-License-Identifier: MIT
 */
#ifndef STLINK_H
#define STLINK_H

#include <stdbool.h>
#include <stdint.h>

#define STLINK_CMD_SIZE     16
#define STLINK_DATA_MAX     6144

void     stlink_init(void);
/* Memory commands stream their data in chunks of any size:
 *   stlink_out_len: OUT data bytes following a memory write command
 *   stlink_in_len:  IN reply bytes of a memory read command
 *   stlink_mem_out/in: handle one chunk at byte offset off */
uint32_t stlink_out_len(const uint8_t *cmd);
uint32_t stlink_in_len(const uint8_t *cmd);
void     stlink_mem_out(const uint8_t *cmd, uint32_t off, const uint8_t *buf, uint32_t n);
void     stlink_mem_in(const uint8_t *cmd, uint32_t off, uint8_t *buf, uint32_t n);
/* any other command: reply (up to STLINK_DATA_MAX bytes) goes to reply,
 * *rlen = 0 means no reply */
void     stlink_execute(const uint8_t *cmd, uint8_t *reply, uint32_t *rlen);
bool     stlink_debug_active(void);

/* trace (SWO) bytes not yet delivered to the host (USB layer) */
uint32_t stlink_trace_count(void);
/* trace reception (re)started or stopped; host_buf: host buffer, 0: none */
void     stlink_trace_reset(uint32_t host_buf);

/* the USB layer calls this when a bootloader switch was requested */
bool     stlink_bootloader_requested(void);

#endif
