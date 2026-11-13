/*
 * WCH-Link compatible command set (as observed on WCH-Link firmware 2.12,
 * docs/wch.md §3), on top of RVSWD and the RISC-V debug module.
 *
 * Request  81 CMD LEN payload     reply  82 CMD LEN payload
 * Integers are big endian. Unknown commands get 82 81 01 ff.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "wch.h"
#include "rvswd.h"
#include "rvdm.h"
#include "wch_flash.h"
#include "hal.h"

#define CMD_SET_WRITE_REGION 0x01
#define CMD_PROGRAM          0x02
#define CMD_SET_READ_REGION  0x03
#define CMD_PROTECT          0x06
#define CMD_DMI              0x08
#define CMD_IAP_CHECK        0x0a
#define CMD_RESET            0x0b
#define CMD_SPEED            0x0c
#define CMD_CONTROL          0x0d
#define CMD_CHIP_INFO        0x11

#define FW_MAJOR 2
#define FW_MINOR 12
#define PROBE_TYPE_CH549 1

/* ESIG and flash option status on CH32V2xx/3xx */
#define ESIG_FLASH_SIZE 0x1FFFF7E0u
#define ESIG_UID        0x1FFFF7E8u
#define FLASH_OBR       0x4002201Cu
#define FLASH_OBR_RDPRT (1u << 1)

static uint8_t family = 0x06;
static uint32_t chip_id;
static uint32_t wr_addr, wr_len, rd_addr, rd_len;

/* reading memory out on EP 0x82 (02 0c) */
static uint32_t rd_pos;
static bool reading;

static uint16_t reply(uint8_t *r, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    r[0] = 0x82;
    r[1] = cmd;
    r[2] = n;
    memcpy(&r[3], p, n);
    return 3u + n;
}

static uint16_t reply1(uint8_t *r, uint8_t cmd, uint8_t v)
{
    return reply(r, cmd, &v, 1);
}

/* error replies start with 81 (community notes; not seen on fw 2.12) */
static uint16_t error_reply(uint8_t *r, uint8_t reason)
{
    r[0] = 0x81; r[1] = reason; r[2] = 0x01; r[3] = 0x01;
    return 4;
}

static uint16_t unknown(uint8_t *r)
{
    return reply1(r, 0x81, 0xFF);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static uint8_t family_of(uint32_t id)
{
    switch (id >> 20) {
    case 0x003: return 0x09;                        /* CH32V003 */
    case 0x103: return 0x01;                        /* CH32V103 */
    case 0x203: case 0x208: return 0x05;            /* CH32V20x */
    case 0x303: case 0x305: case 0x307: case 0x317: return 0x06;   /* CH32V30x */
    default: return family;
    }
}

/* speed codes: 1 high, 2 medium, 3 low (as wlink names them) */
static void set_speed(uint8_t s)
{
    static const uint8_t delay[] = { 0, 0, 3, 40 };
    rvswd_set_delay(s < sizeof delay ? delay[s] : 0);
}

void wch_init(void)
{
    hal_target_init();
    rvswd_init();
    set_speed(1);
    wch_flash_init();
}

static int protected(void)
{
    bool run;
    uint32_t obr;
    if (!rvdm_begin(&run))
        return -1;
    bool ok = rvdm_mem_read32(FLASH_OBR, &obr);
    rvdm_end(run);
    return ok ? (obr & FLASH_OBR_RDPRT) != 0 : -1;
}

static uint16_t chip_info(uint8_t *r)
{
    bool run;
    uint32_t w[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    if (rvdm_begin(&run)) {
        rvdm_mem_read32(ESIG_FLASH_SIZE, &w[0]);
        rvdm_mem_read32(ESIG_UID, &w[1]);
        rvdm_mem_read32(ESIG_UID + 4, &w[2]);
        rvdm_mem_read32(ESIG_UID + 8, &w[3]);
        rvdm_end(run);
    }
    for (int i = 0; i < 4; i++)
        put_be32(&r[4 * i], w[i]);
    put_be32(&r[16], chip_id);
    return 20;                                  /* this reply has no header */
}

static uint16_t control(uint8_t sub, uint8_t *r)
{
    uint8_t p[5];
    switch (sub) {
    case 0x01:
        p[0] = FW_MAJOR; p[1] = FW_MINOR; p[2] = PROBE_TYPE_CH549; p[3] = 0;
        return reply(r, CMD_CONTROL, p, 4);
    case 0x02:
        if (!rvdm_attach(&chip_id))
            return error_reply(r, 0x55);         /* cannot connect */
        family = family_of(chip_id);
        p[0] = family;
        put_be32(&p[1], chip_id);
        return reply(r, CMD_CONTROL, p, 5);
    case 0x04: return reply1(r, CMD_CONTROL, 0x02);
    case 0x17: return reply1(r, CMD_CONTROL, 0x05);
    case 0x06: return reply1(r, CMD_CONTROL, 0x00);
    case 0x03: case 0x05: case 0x07: case 0x10: case 0x18: case 0xFF:
        return reply1(r, CMD_CONTROL, sub);
    default:
        return reply1(r, CMD_CONTROL, 0xFF);
    }
}

static uint16_t dmi(const uint8_t *p, uint8_t *r)
{
    uint8_t out[6];
    uint32_t v = be32(&p[1]);
    uint8_t st = DMI_OK;
    switch (p[5]) {
    case DMI_OP_READ:  st = rvdm_read(p[0], &v); break;
    case DMI_OP_WRITE: st = rvdm_write(p[0], v); break;
    default: v = 0; break;
    }
    out[0] = p[0];
    put_be32(&out[1], v);
    out[5] = st;
    return reply(r, CMD_DMI, out, 6);
}

static uint16_t protect(uint8_t sub, uint8_t *r)
{
    int prot;
    switch (sub) {
    case 0x01:
        prot = protected();
        return reply1(r, CMD_PROTECT, prot == 1 ? 0x01 : 0x02);
    case 0x04:
        prot = protected();
        return reply1(r, CMD_PROTECT, prot == 1 ? 0x01 : 0x00);
    case 0x02:
        wch_flash_unprotect();
        return reply1(r, CMD_PROTECT, sub);
    case 0x03:
        wch_flash_protect();
        return reply1(r, CMD_PROTECT, sub);
    default:
        return unknown(r);
    }
}

static uint16_t program(uint8_t sub, uint8_t *r)
{
    switch (sub) {
    case 0x01:                                  /* full erase */
        wch_flash_mass_erase();
        break;
    case 0x05:                                  /* flash routine follows */
        wch_flash_routine_upload();
        break;
    case 0x02: case 0x04: case 0x07:            /* start writing */
        wch_flash_write_start(wr_addr, wr_len);
        break;
    case 0x08:                                  /* end */
        wch_flash_write_end();
        break;
    case 0x0c:                                  /* read region out on EP 0x82 */
        rd_pos = 0;
        reading = rd_len != 0;
        break;
    case 0x06:
        break;
    default:
        return unknown(r);
    }
    return reply1(r, CMD_PROGRAM, sub);
}

uint16_t wch_execute(const uint8_t *q, uint16_t len, uint8_t *r)
{
    if (len < 3 || q[0] != 0x81)
        return unknown(r);
    const uint8_t *p = &q[3];
    uint16_t n = len - 3u;
    switch (q[1]) {
    case CMD_CONTROL:
        return n >= 1 ? control(p[0], r) : unknown(r);
    case CMD_SPEED:
        if (n < 2)
            return unknown(r);
        family = p[0];
        set_speed(p[1]);
        return reply1(r, CMD_SPEED, 0x01);
    case CMD_DMI:
        return n >= 6 ? dmi(p, r) : unknown(r);
    case CMD_RESET:
        if (n < 1)
            return unknown(r);
        /* 01 (wlink, MounRiver after download): reset and run;
         * 02 (OpenOCD after program): reset and halt, it verifies next */
        rvdm_reset(p[0] == 0x02);
        return reply1(r, CMD_RESET, p[0]);
    case CMD_CHIP_INFO:
        return chip_info(r);
    case CMD_PROTECT:
        return n >= 1 ? protect(p[0], r) : unknown(r);
    case CMD_SET_WRITE_REGION:
        if (n >= 8) {
            wr_addr = be32(p);
            wr_len = be32(p + 4);
        }
        return reply1(r, CMD_SET_WRITE_REGION, 0x01);
    case CMD_SET_READ_REGION:
        if (n < 8)
            return unknown(r);
        rd_addr = be32(p);
        rd_len = be32(p + 4);
        return reply1(r, CMD_SET_READ_REGION, 0x01);
    case CMD_PROGRAM:
        return n >= 1 ? program(p[0], r) : unknown(r);
    case CMD_IAP_CHECK:
        return reply1(r, CMD_IAP_CHECK, 0x01);
    default:
        return unknown(r);
    }
}

bool wch_data_wanted(void)
{
    return wch_flash_wants_data();
}

void wch_data_out(const uint8_t *buf, uint16_t len)
{
    wch_flash_data(buf, len);
}

void wch_task(void)
{
    uint8_t ack[4];
    if (wch_flash_ack_pending(ack) && wch_usb_data_in(ack, 4))
        wch_flash_ack_sent();
    if (!reading || wch_usb_data_in_busy())
        return;
    uint8_t buf[WCH_PACKET_SIZE];
    uint32_t n = rd_len - rd_pos;
    if (n > sizeof buf)
        n = sizeof buf;
    bool run;
    if (!rvdm_begin(&run)) {
        reading = false;
        return;
    }
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        rvdm_mem_read32(rd_addr + rd_pos + i, &w);
        memcpy(&buf[i], &w, 4);                 /* little endian, as in memory */
    }
    rvdm_end(run);
    wch_usb_data_in(buf, (uint16_t)n);
    rd_pos += n;
    if (rd_pos >= rd_len)
        reading = false;
}
