/*
 * RISC-V debug module (0.13) helpers on top of the DMI transport:
 * halt / resume and word memory access through the program buffer.
 * SPDX-License-Identifier: MIT
 */
#ifndef RVDM_H
#define RVDM_H

#include <stdbool.h>
#include <stdint.h>

#define DM_DATA0        0x04
#define DM_DMCONTROL    0x10
#define DM_DMSTATUS     0x11
#define DM_HARTINFO     0x12
#define DM_ABSTRACTCS   0x16
#define DM_COMMAND      0x17
#define DM_PROGBUF0     0x20
#define DM_WCH_CHIPID   0x7F

#define DMCONTROL_HALTREQ   (1u << 31)
#define DMCONTROL_RESUMEREQ (1u << 30)
#define DMCONTROL_ACKHAVERESET (1u << 28)
#define DMCONTROL_NDMRESET  (1u << 1)
#define DMCONTROL_DMACTIVE  (1u << 0)
#define DMSTATUS_ALLRESUMEACK (1u << 17)
#define DMSTATUS_ALLHAVERESET (1u << 19)
#define DMSTATUS_ALLRUNNING (1u << 11)
#define DMSTATUS_ALLHALTED  (1u << 9)

uint8_t rvdm_read(uint8_t addr, uint32_t *val);
uint8_t rvdm_write(uint8_t addr, uint32_t val);

bool rvdm_attach(uint32_t *chip_id);
bool rvdm_is_halted(void);
bool rvdm_halt(void);
bool rvdm_resume(void);
bool rvdm_reset(bool halt);

/* memory access; the hart must be halted (see rvdm_begin/rvdm_end) */
bool rvdm_begin(bool *was_running);     /* halt if needed, save s0/s1 */
void rvdm_end(bool was_running);        /* restore s0/s1, resume if it ran */
bool rvdm_mem_read32(uint32_t addr, uint32_t *val);
bool rvdm_mem_write32(uint32_t addr, uint32_t val);
bool rvdm_mem_write16(uint32_t addr, uint16_t val);
/* n words for flash programming: after each one waits until
 * (*busy_reg & mask) == 0 */
bool rvdm_mem_stream32(uint32_t addr, const uint8_t *p, uint32_t n,
                       uint32_t busy_reg, uint32_t mask);

#endif
