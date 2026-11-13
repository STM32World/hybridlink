/*
 * Portable SWD / ADIv5 engine.
 *
 * Bit timing: the host changes SWDIO while SWCLK is low and the target
 * samples on the rising edge; the target drives SWDIO after the rising
 * edge and the host samples while SWCLK is low.
 *
 * SPDX-License-Identifier: MIT
 */
#include "swd.h"
#include "swd_io.h"
#include "hal.h"

#define DP_IDCODE   0x00   /* read  */
#define DP_ABORT    0x00   /* write */
#define DP_CTRLSTAT 0x04
#define DP_SELECT   0x08
#define DP_RDBUFF   0x0C

#define CTRL_CSYSPWRUPACK (1u << 31)
#define CTRL_CSYSPWRUPREQ (1u << 30)
#define CTRL_CDBGPWRUPACK (1u << 29)
#define CTRL_CDBGPWRUPREQ (1u << 28)
#define CTRL_STICKYERR    (1u << 5)
#define CTRL_STICKYCMP    (1u << 4)
#define CTRL_STICKYORUN   (1u << 1)
#define CTRL_WDATAERR     (1u << 7)

#define AP_CSW      0x00
#define AP_TAR      0x04
#define AP_DRW      0x0C

#define CSW_SIZE8   0u
#define CSW_SIZE16  1u
#define CSW_SIZE32  2u
#define CSW_INC     (1u << 4)
#define CSW_DEFAULT 0xA2000000u   /* DbgSwEnable, MasterDebug, HPROT1 */

#define WAIT_RETRIES 200

static uint32_t delay;
static uint32_t select_cache;
static bool select_valid;
static uint8_t cur_ap;
static uint32_t csw_cache;
static bool csw_valid;
static uint32_t fault_addr;

void swd_set_delay(uint32_t d)
{
    delay = d;
}

void swd_set_clock(uint32_t hz)
{
    delay = swd_io_delay_for(hz);
}

void swd_init(void)
{
    swd_io_clk_hi();
    swd_io_dio_set(1);
    swd_io_dio_output();
    select_valid = false;
    csw_valid = false;
}

/* ---- bit level -------------------------------------------------------- */

/* delay = extra delay steps per bit, split over the two clock halves */
static void write_bits(uint32_t data, unsigned n)
{
    uint32_t d1 = delay >> 1, d2 = delay - d1;
    if (!delay) {                       /* full speed */
        while (n--) {
            swd_io_dio_set(data);
            swd_io_clk_lo();
            data >>= 1;
            swd_io_clk_hi();
        }
        return;
    }
    while (n--) {
        swd_io_dio_set(data);
        swd_io_clk_lo();
        swd_io_delay(d2);
        data >>= 1;
        swd_io_clk_hi();
        swd_io_delay(d1);
    }
}

static uint32_t read_bits(unsigned n)
{
    uint32_t d1 = delay >> 1, d2 = delay - d1, v = 0;
    if (!delay) {
        for (unsigned i = 0; i < n; i++) {
            swd_io_clk_lo();
            __asm volatile ("nop");
            v |= swd_io_dio_get() << i;
            swd_io_clk_hi();
        }
        return v;
    }
    for (unsigned i = 0; i < n; i++) {
        swd_io_clk_lo();
        swd_io_delay(d2);
        v |= swd_io_dio_get() << i;
        swd_io_clk_hi();
        swd_io_delay(d1);
    }
    return v;
}

static void turnaround(void)
{
    uint32_t d1 = delay >> 1, d2 = delay - d1;
    swd_io_clk_lo();
    swd_io_delay(d2);
    swd_io_clk_hi();
    swd_io_delay(d1);
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

/* one raw transfer; req bits: 0 APnDP, 1 RnW, 2..3 A[3:2] (= address bits) */
static unsigned transfer(unsigned req, uint32_t *data)
{
    unsigned r = 0x81u | ((req & 0xFu) << 1) | (parity32(req & 0xFu) << 5);

    write_bits(r, 8);
    swd_io_dio_input();
    turnaround();
    unsigned ack = read_bits(3);

    if (ack == SWD_ACK_OK) {
        if (req & 2u) {
            uint32_t v = read_bits(32);
            uint32_t p = read_bits(1);
            turnaround();
            swd_io_dio_output();
            write_bits(0, 2);
            if (p != parity32(v))
                return SWD_ERR_PARITY;
            if (data)
                *data = v;
        } else {
            turnaround();
            swd_io_dio_output();
            write_bits(*data, 32);
            write_bits(parity32(*data), 1);
            write_bits(0, 2);
        }
        return ack;
    }
    if (ack == SWD_ACK_WAIT || ack == SWD_ACK_FAULT) {
        turnaround();
        swd_io_dio_output();
        return ack;
    }
    /* protocol error: let a possible data phase pass, then resync */
    read_bits(33);
    swd_io_dio_output();
    write_bits(0, 2);
    return SWD_ERR_NOACK;
}

static unsigned transfer_retry(unsigned req, uint32_t *data)
{
    unsigned ack;
    int n = WAIT_RETRIES;
    do {
        ack = transfer(req, data);
    } while (ack == SWD_ACK_WAIT && --n);
    return ack;
}

static uint8_t ack_to_status(unsigned ack, bool ap)
{
    switch (ack) {
    case SWD_ACK_OK:     return STLINK_OK;
    case SWD_ACK_WAIT:   return ap ? STLINK_SWD_AP_WAIT : STLINK_SWD_DP_WAIT;
    case SWD_ACK_FAULT:  return ap ? STLINK_SWD_AP_FAULT : STLINK_SWD_DP_FAULT;
    case SWD_ERR_PARITY: return ap ? STLINK_SWD_AP_PARITY : STLINK_SWD_DP_PARITY;
    default:             return ap ? STLINK_SWD_AP_ERROR : STLINK_SWD_DP_ERROR;
    }
}

static void line_ones(void)
{
    swd_io_dio_output();
    write_bits(0xFFFFFFFFu, 32);
    write_bits(0xFFFFFFFFu, 24);
}

/* line reset followed by idle cycles */
void swd_line_reset(void)
{
    line_ones();
    write_bits(0, 4);
}

#ifdef SWD_CALIBRATE
/* measurement build: CPU cycles for 256 written / read bits per delay */
#define DEMCR       (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004u)
static const uint16_t cal_delay[] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 32, 64, 128, 256, 1024 };
volatile uint32_t swd_cal[3][sizeof(cal_delay) / sizeof(cal_delay[0])];

void swd_calibrate(void)
{
    DEMCR |= 1u << 24;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
    uint32_t saved = delay;
    swd_io_dio_output();
    for (unsigned i = 0; i < sizeof(cal_delay) / sizeof(cal_delay[0]); i++) {
        delay = cal_delay[i];
        __asm volatile ("cpsid i");
        uint32_t t0 = DWT_CYCCNT;
        for (int k = 0; k < 8; k++)
            write_bits(0xA5A5A5A5u, 32);
        uint32_t t1 = DWT_CYCCNT;
        swd_io_dio_input();
        for (int k = 0; k < 8; k++)
            (void)read_bits(32);
        uint32_t t2 = DWT_CYCCNT;
        swd_io_dio_output();
        __asm volatile ("cpsie i");
        swd_cal[0][i] = delay;
        swd_cal[1][i] = t1 - t0;
        swd_cal[2][i] = t2 - t1;
    }
    delay = saved;
}
#endif

/* ---- raw access ------------------------------------------------------ */

unsigned swd_raw_transfer(unsigned req, uint32_t *data)
{
    swd_io_dio_output();
    return transfer(req, data);
}

void swd_raw_write_noack(unsigned req, uint32_t data)
{
    unsigned r = 0x81u | ((req & 0xFu) << 1) | (parity32(req & 0xFu) << 5);

    swd_io_dio_output();
    write_bits(r, 8);
    swd_io_dio_input();
    turnaround();
    read_bits(3);
    turnaround();
    swd_io_dio_output();
    write_bits(data, 32);
    write_bits(parity32(data), 1);
    write_bits(0, 2);
}

void swd_raw_out(const uint8_t *data, unsigned bits)
{
    swd_io_dio_output();
    while (bits) {
        unsigned n = bits > 8 ? 8 : bits;
        write_bits(*data++, n);
        bits -= n;
    }
}

void swd_raw_in(uint8_t *data, unsigned bits)
{
    swd_io_dio_input();
    while (bits) {
        unsigned n = bits > 8 ? 8 : bits;
        *data++ = (uint8_t)read_bits(n);
        bits -= n;
    }
}

void swd_raw_idle(unsigned cycles)
{
    swd_io_dio_output();
    while (cycles) {
        unsigned n = cycles > 32 ? 32 : cycles;
        write_bits(0, n);
        cycles -= n;
    }
}

void swd_invalidate(void)
{
    select_valid = false;
    csw_valid = false;
}

/* ---- DP / AP ------------------------------------------------------------ */

static uint8_t select_bank(uint32_t sel)
{
    if (select_valid && sel == select_cache)
        return STLINK_OK;
    unsigned ack = transfer_retry(DP_SELECT, &sel);
    if (ack != SWD_ACK_OK) {
        select_valid = false;
        return ack_to_status(ack, false);
    }
    select_cache = sel;
    select_valid = true;
    return STLINK_OK;
}

uint8_t dp_read(uint8_t addr, uint32_t *val)
{
    uint8_t reg = addr & 0x0Cu;
    if (reg == DP_CTRLSTAT || (addr & 0xF0u)) {
        uint8_t st = select_bank((select_valid ? (select_cache & ~0xFu) : 0) | ((addr >> 4) & 0xFu));
        if (st != STLINK_OK)
            return st;
    }
    unsigned ack = transfer_retry(0x2u | reg, val);
    return ack_to_status(ack, false);
}

uint8_t dp_write(uint8_t addr, uint32_t val)
{
    uint8_t reg = addr & 0x0Cu;
    if (reg == DP_SELECT) {
        uint8_t st = ack_to_status(transfer_retry(reg, &val), false);
        select_valid = st == STLINK_OK;
        select_cache = val;
        return st;
    }
    if (reg == DP_CTRLSTAT) {
        uint8_t st = select_bank((select_valid ? (select_cache & ~0xFu) : 0) | ((addr >> 4) & 0xFu));
        if (st != STLINK_OK)
            return st;
    }
    return ack_to_status(transfer_retry(reg, &val), false);
}

static uint8_t ap_select(uint8_t ap, uint8_t addr)
{
    uint32_t dpbank = select_valid ? (select_cache & 0xFu) : 0;
    return select_bank(((uint32_t)ap << 24) | (addr & 0xF0u) | dpbank);
}

uint8_t ap_write(uint8_t ap, uint8_t addr, uint32_t val)
{
    uint8_t st = ap_select(ap, addr);
    if (st != STLINK_OK)
        return st;
    if (ap == cur_ap && (addr & 0xFCu) == AP_CSW)
        csw_valid = false;
    return ack_to_status(transfer_retry(0x1u | (addr & 0xCu), &val), true);
}

uint8_t ap_read(uint8_t ap, uint8_t addr, uint32_t *val)
{
    uint8_t st = ap_select(ap, addr);
    if (st != STLINK_OK)
        return st;
    unsigned ack = transfer_retry(0x3u | (addr & 0xCu), NULL);
    if (ack != SWD_ACK_OK)
        return ack_to_status(ack, true);
    return ack_to_status(transfer_retry(0x2u | DP_RDBUFF, val), true);
}

uint8_t swd_clear_errors(void)
{
    uint32_t abort = 0x1E;   /* ORUNERRCLR | WDERRCLR | STKERRCLR | STKCMPCLR */
    return ack_to_status(transfer_retry(DP_ABORT, &abort), false);
}

uint8_t swd_connect(uint32_t *idcode)
{
    swd_init();
    line_ones();
    write_bits(0xE79Eu, 16);            /* JTAG-to-SWD, directly after the ones */
    swd_line_reset();
    select_valid = false;
    csw_valid = false;

    unsigned ack = transfer(0x2u | DP_IDCODE, idcode);
    if (ack != SWD_ACK_OK) {
        /* second chance: plain line reset */
        swd_line_reset();
        ack = transfer(0x2u | DP_IDCODE, idcode);
        if (ack != SWD_ACK_OK)
            return ack_to_status(ack, false);
    }
    swd_clear_errors();
    uint8_t st = dp_write(DP_SELECT, 0);
    if (st != STLINK_OK)
        return st;
    st = dp_write(DP_CTRLSTAT, CTRL_CSYSPWRUPREQ | CTRL_CDBGPWRUPREQ);
    if (st != STLINK_OK)
        return st;
    uint32_t cs = 0;
    for (int i = 0; i < 100; i++) {
        st = dp_read(DP_CTRLSTAT, &cs);
        if (st == STLINK_OK && (cs & (CTRL_CSYSPWRUPACK | CTRL_CDBGPWRUPACK)) ==
                               (CTRL_CSYSPWRUPACK | CTRL_CDBGPWRUPACK))
            return STLINK_OK;
        hal_delay_us(100);
    }
    return st == STLINK_OK ? STLINK_SWD_DP_ERROR : st;
}

void swd_disconnect(void)
{
    swd_io_dio_input();
    select_valid = false;
    csw_valid = false;
}

/* ---- MEM-AP --------------------------------------------------------- */

void mem_set_ap(uint8_t ap)
{
    if (ap != cur_ap)
        csw_valid = false;
    cur_ap = ap;
}

uint8_t mem_get_ap(void)
{
    return cur_ap;
}

uint8_t mem_ap_init(uint8_t ap)
{
    mem_set_ap(ap);
    csw_valid = false;
    uint32_t idr;
    return ap_read(ap, 0xFC, &idr);
}

static uint8_t set_csw(uint32_t csw)
{
    if (csw_valid && csw == csw_cache)
        return STLINK_OK;
    uint8_t st = ap_write(cur_ap, AP_CSW, csw);
    if (st == STLINK_OK) {
        csw_cache = csw;
        csw_valid = true;
    }
    return st;
}

static uint8_t mem_error(uint8_t st, uint32_t addr)
{
    if (st != STLINK_OK) {
        fault_addr = addr;
        uint32_t cs = 0;
        if (dp_read(DP_CTRLSTAT, &cs) == STLINK_OK && (cs & CTRL_STICKYERR))
            st = STLINK_SWD_AP_FAULT;
        swd_clear_errors();
    }
    return st;
}

uint32_t mem_fault_addr(void)
{
    return fault_addr;
}

uint8_t mem_read32(uint32_t addr, uint32_t *val)
{
    return mem_read(addr, (uint8_t *)val, 4, 4, true);
}

uint8_t mem_write32(uint32_t addr, uint32_t val)
{
    return mem_write(addr, (const uint8_t *)&val, 4, 4, true);
}

static uint32_t csw_high = CSW_DEFAULT;

void mem_set_csw_high(uint32_t high)
{
    csw_high = high ? (high & 0xFFFFFF00u) : CSW_DEFAULT;
}

static uint32_t csw_for(unsigned size, bool inc)
{
    uint32_t s = size == 1 ? CSW_SIZE8 : (size == 2 ? CSW_SIZE16 : CSW_SIZE32);
    return csw_high | s | (inc ? CSW_INC : 0);
}

uint8_t mem_read(uint32_t addr, uint8_t *buf, uint32_t len, unsigned size, bool inc)
{
    uint8_t st = set_csw(csw_for(size, inc));
    if (st != STLINK_OK)
        return mem_error(st, addr);
    st = ap_select(cur_ap, AP_DRW);
    if (st != STLINK_OK)
        return mem_error(st, addr);

    while (len) {
        /* TAR auto increment is only guaranteed within 1 KiB */
        uint32_t chunk = inc ? 1024u - (addr & 1023u) : len;
        if (chunk > len)
            chunk = len;
        uint32_t tar = addr;
        unsigned ack = transfer_retry(0x1u | AP_TAR, &tar);
        if (ack != SWD_ACK_OK)
            return mem_error(ack_to_status(ack, true), addr);
        /* first DRW read is posted */
        ack = transfer_retry(0x3u | AP_DRW, NULL);
        if (ack != SWD_ACK_OK)
            return mem_error(ack_to_status(ack, true), addr);
        uint32_t done = 0;
        while (done < chunk) {
            uint32_t v;
            bool last = done + size >= chunk;
            ack = transfer_retry(last ? (0x2u | DP_RDBUFF) : (0x3u | AP_DRW), &v);
            if (ack != SWD_ACK_OK)
                return mem_error(ack_to_status(ack, true), addr + done);
            uint32_t a = addr + done;
            if (size == 4) {
                buf[0] = v; buf[1] = v >> 8; buf[2] = v >> 16; buf[3] = v >> 24;
            } else if (size == 2) {
                v >>= 8 * (a & 2u);
                buf[0] = v; buf[1] = v >> 8;
            } else {
                buf[0] = v >> (8 * (a & 3u));
            }
            buf += size;
            done += size;
        }
        addr += inc ? chunk : 0;
        len -= chunk;
    }
    return STLINK_OK;
}

uint8_t mem_write(uint32_t addr, const uint8_t *buf, uint32_t len, unsigned size, bool inc)
{
    uint8_t st = set_csw(csw_for(size, inc));
    if (st != STLINK_OK)
        return mem_error(st, addr);
    st = ap_select(cur_ap, AP_DRW);
    if (st != STLINK_OK)
        return mem_error(st, addr);

    while (len) {
        uint32_t chunk = inc ? 1024u - (addr & 1023u) : len;
        if (chunk > len)
            chunk = len;
        uint32_t tar = addr;
        unsigned ack = transfer_retry(0x1u | AP_TAR, &tar);
        if (ack != SWD_ACK_OK)
            return mem_error(ack_to_status(ack, true), addr);
        for (uint32_t done = 0; done < chunk; done += size) {
            uint32_t a = addr + done, v;
            if (size == 4)
                v = buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
            else if (size == 2)
                v = (uint32_t)(buf[0] | (buf[1] << 8)) << (8 * (a & 2u));
            else
                v = (uint32_t)buf[0] << (8 * (a & 3u));
            ack = transfer_retry(0x1u | AP_DRW, &v);
            if (ack != SWD_ACK_OK)
                return mem_error(ack_to_status(ack, true), a);
            buf += size;
        }
        addr += inc ? chunk : 0;
        len -= chunk;
    }
    /* make sure the last write completed */
    uint32_t dummy;
    unsigned ack = transfer_retry(0x2u | DP_RDBUFF, &dummy);
    return mem_error(ack_to_status(ack, true), addr);
}
