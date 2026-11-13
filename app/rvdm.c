/*
 * RISC-V debug module helpers (debug spec 0.13), written from the spec.
 * Memory goes through the program buffer: s0 holds the address, s1 the
 * data; both are saved and restored around a series of accesses.
 * SPDX-License-Identifier: MIT
 */
#include "rvdm.h"
#include "rvswd.h"
#include "hal.h"

#define ABSTRACTCS_BUSY   (1u << 12)
#define ABSTRACTCS_CMDERR (7u << 8)

/* access register: aarsize 32 bit, transfer, [write], [postexec] */
#define CMD_REG       ((2u << 20) | (1u << 17))
#define CMD_WRITE     (1u << 16)
#define CMD_POSTEXEC  (1u << 18)
#define REG_S0        0x1008u
#define REG_S1        0x1009u

#define INSN_LW_S1_S0   0x00042483u   /* lw   s1, 0(s0) */
#define INSN_SW_S1_S0   0x00942023u   /* sw   s1, 0(s0) */
#define INSN_SH_S1_S0   0x00941023u   /* sh   s1, 0(s0) */
#define INSN_EBREAK     0x00100073u

static uint32_t saved_s0, saved_s1;
static uint32_t progbuf_op;             /* instruction now in progbuf0 */

uint8_t rvdm_read(uint8_t addr, uint32_t *val)
{
    return rvswd_read(addr, val);
}

uint8_t rvdm_write(uint8_t addr, uint32_t val)
{
    return rvswd_write(addr, val);
}

static bool wait_status(uint32_t mask)
{
    uint32_t t = hal_millis(), s;
    do {
        if (rvdm_read(DM_DMSTATUS, &s) != DMI_OK)
            return false;
        if (s & mask)
            return true;
    } while (hal_millis() - t < 100u);
    return false;
}

bool rvdm_attach(uint32_t *chip_id)
{
    rvswd_init();
    rvswd_wake();
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE);
    if (rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE) != DMI_OK)
        return false;
    progbuf_op = 0;
    uint32_t id;
    if (rvdm_read(DM_WCH_CHIPID, &id) != DMI_OK)
        return false;
    *chip_id = id;
    return true;
}

bool rvdm_is_halted(void)
{
    uint32_t s;
    return rvdm_read(DM_DMSTATUS, &s) == DMI_OK && (s & DMSTATUS_ALLHALTED);
}

bool rvdm_halt(void)
{
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE | DMCONTROL_HALTREQ);
    bool ok = wait_status(DMSTATUS_ALLHALTED);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE);
    return ok;
}

bool rvdm_resume(void)
{
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE | DMCONTROL_RESUMEREQ);
    bool ok = wait_status(DMSTATUS_ALLRESUMEACK);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE);
    return ok;
}

bool rvdm_reset(bool halt)
{
    uint32_t h = halt ? DMCONTROL_HALTREQ : 0;
    /* a DM cycle first: frees an abstract command stuck on a bus access */
    rvdm_write(DM_DMCONTROL, 0);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE | DMCONTROL_NDMRESET | h);
    hal_delay_ms(2);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE | h);
    bool ok = halt ? wait_status(DMSTATUS_ALLHALTED) : true;
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE | DMCONTROL_ACKHAVERESET);
    rvdm_write(DM_DMCONTROL, DMCONTROL_DMACTIVE);
    progbuf_op = 0;
    return ok;
}

static bool command(uint32_t cmd)
{
    uint32_t cs;
    if (rvdm_write(DM_COMMAND, cmd) != DMI_OK)
        return false;
    for (int i = 0; i < 100; i++) {
        if (rvdm_read(DM_ABSTRACTCS, &cs) != DMI_OK)
            return false;
        if (!(cs & ABSTRACTCS_BUSY))
            break;
    }
    if (cs & (ABSTRACTCS_BUSY | ABSTRACTCS_CMDERR)) {
        rvdm_write(DM_ABSTRACTCS, ABSTRACTCS_CMDERR);
        return false;
    }
    return true;
}

static bool reg_read(uint32_t reg, uint32_t *v)
{
    return command(CMD_REG | reg) && rvdm_read(DM_DATA0, v) == DMI_OK;
}

static bool reg_write(uint32_t reg, uint32_t v, bool exec)
{
    return rvdm_write(DM_DATA0, v) == DMI_OK &&
           command(CMD_REG | CMD_WRITE | (exec ? CMD_POSTEXEC : 0) | reg);
}

static bool progbuf(uint32_t insn)
{
    if (progbuf_op == insn)
        return true;
    if (rvdm_write(DM_PROGBUF0, insn) != DMI_OK ||
        rvdm_write(DM_PROGBUF0 + 1, INSN_EBREAK) != DMI_OK)
        return false;
    progbuf_op = insn;
    return true;
}

bool rvdm_begin(bool *was_running)
{
    *was_running = !rvdm_is_halted();
    if (*was_running && !rvdm_halt())
        return false;
    progbuf_op = 0;
    rvdm_write(DM_ABSTRACTCS, ABSTRACTCS_CMDERR);   /* sticky, maybe left by a host */
    if (reg_read(REG_S0, &saved_s0) && reg_read(REG_S1, &saved_s1))
        return true;
    if (*was_running)
        rvdm_resume();
    return false;
}

void rvdm_end(bool was_running)
{
    reg_write(REG_S0, saved_s0, false);
    reg_write(REG_S1, saved_s1, false);
    if (was_running)
        rvdm_resume();
}

bool rvdm_mem_read32(uint32_t addr, uint32_t *val)
{
    return progbuf(INSN_LW_S1_S0) && reg_write(REG_S0, addr, true) &&
           reg_read(REG_S1, val);
}

bool rvdm_mem_write32(uint32_t addr, uint32_t val)
{
    return progbuf(INSN_SW_S1_S0) && reg_write(REG_S1, val, false) &&
           reg_write(REG_S0, addr, true);
}

bool rvdm_mem_write16(uint32_t addr, uint16_t val)
{
    return progbuf(INSN_SH_S1_S0) && reg_write(REG_S1, val, false) &&
           reg_write(REG_S0, addr, true);
}

/* store n words from p (little endian) at addr, for flash programming.
 * The program buffer stores s1, advances s0 and then spins until the bits
 * in mask of the status register at busy_reg read 0. Per word: two DMI
 * writes plus the abstract command status (a write while the command is
 * still busy would be dropped and set a sticky error). */
bool rvdm_mem_stream32(uint32_t addr, const uint8_t *p, uint32_t n,
                       uint32_t busy_reg, uint32_t mask)
{
    if (!n)
        return true;
    uint32_t hi = (busy_reg + 0x800u) >> 12, lo = busy_reg - (hi << 12);
    const uint32_t prog[] = {
        INSN_SW_S1_S0,
        0x00440413u,                                             /* addi s0, s0, 4 */
        (hi << 12) | (9u << 7) | 0x37u,                          /* lui  s1, hi    */
        ((lo & 0xFFFu) << 20) | (9u << 15) | (2u << 12) | (9u << 7) | 0x03u, /* lw s1, lo(s1) */
        ((mask & 0x7FFu) << 20) | (9u << 15) | (7u << 12) | (9u << 7) | 0x13u, /* andi s1, s1, mask */
        0xFE049AE3u,                                             /* bnez s1, -12   */
        INSN_EBREAK,
    };
    progbuf_op = 0;
    for (unsigned i = 0; i < sizeof prog / sizeof prog[0]; i++)
        if (rvdm_write(DM_PROGBUF0 + i, prog[i]) != DMI_OK)
            return false;
    if (!reg_write(REG_S0, addr, false))
        return false;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w = p[4 * i] | (p[4 * i + 1] << 8) | (p[4 * i + 2] << 16) |
                     ((uint32_t)p[4 * i + 3] << 24);
        rvdm_write(DM_DATA0, w);
        if (!command(CMD_REG | CMD_WRITE | CMD_POSTEXEC | REG_S1))
            return false;
    }
    return true;
}
