/*
 * ST-Link/V2-1 command interpreter.
 *
 * Implements the debug protocol used by STM32CubeIDE / STM32CubeProgrammer,
 * OpenOCD, pyOCD and the stlink tools. Command semantics follow the public
 * host implementations and USB captures of a genuine ST-Link/V2-1.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "stlink.h"
#include "stlink_status.h"
#include "swd.h"
#include "hal.h"
#include "usb_ids.h"
#include "settings.h"

#define DHCSR   0xE000EDF0u
#define DCRSR   0xE000EDF4u
#define DCRDR   0xE000EDF8u
#define AIRCR   0xE000ED0Cu
#define MVFR0   0xE000EF40u

#define DBGKEY      0xA05F0000u
#define C_DEBUGEN   (1u << 0)
#define C_HALT      (1u << 1)
#define C_STEP      (1u << 2)
#define S_REGRDY    (1u << 16)
#define S_HALT      (1u << 17)

static uint8_t mode = STLINK_MODE_MASS;
static uint8_t last_status = STLINK_OK;
static uint32_t idcode;
static bool trace_on;
static uint32_t trace_max;
static bool want_bootloader;

/* ---- little endian helpers ------------------------------------------ */

static inline uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* ---- SWD clock --------------------------------------------------------- */

/* divisor values used by host tools (ST-Link SWD_SET_FREQ table):
 * 0 = 4 MHz, 1 = 1.8 MHz, 2 = 1.2 MHz, 3 = 950 kHz ... 798 = 5 kHz,
 * i.e. about 3.6 MHz / (div + 1) */
static void set_swd_divisor(uint16_t div)
{
    swd_set_clock(div == 0 ? 4000000u : 3600000u / (div + 1u));
}

/* ---- Cortex-M helpers ------------------------------------------------- */

static uint8_t core_read_reg(uint8_t regsel, uint32_t *val)
{
    uint8_t st = mem_write32(DCRSR, regsel);
    if (st != STLINK_OK)
        return st;
    for (int i = 0; i < 100; i++) {
        uint32_t dhcsr;
        st = mem_read32(DHCSR, &dhcsr);
        if (st != STLINK_OK)
            return st;
        if (dhcsr & S_REGRDY)
            return mem_read32(DCRDR, val);
    }
    return STLINK_FALSE;
}

static uint8_t core_write_reg(uint8_t regsel, uint32_t val)
{
    uint8_t st = mem_write32(DCRDR, val);
    if (st == STLINK_OK)
        st = mem_write32(DCRSR, (1u << 16) | regsel);
    for (int i = 0; st == STLINK_OK && i < 100; i++) {
        uint32_t dhcsr;
        st = mem_read32(DHCSR, &dhcsr);
        if (st == STLINK_OK && (dhcsr & S_REGRDY))
            return STLINK_OK;
    }
    return st == STLINK_OK ? STLINK_FALSE : st;
}

static uint8_t core_dhcsr(uint32_t bits)
{
    return mem_write32(DHCSR, DBGKEY | bits);
}

/* ---- reply helpers ----------------------------------------------------- */

static uint32_t reply_status(uint8_t *r, uint8_t st)
{
    r[0] = st;
    r[1] = 0;
    return 2;
}

static uint32_t reply_status_val(uint8_t *r, uint8_t st, uint32_t val)
{
    memset(r, 0, 4);
    r[0] = st;
    put32(r + 4, val);
    return 8;
}

/* ---- trace ------------------------------------------------------------- */

bool stlink_bootloader_requested(void)
{
    return want_bootloader;
}

bool stlink_debug_active(void)
{
    return mode == STLINK_MODE_DEBUG;
}

void stlink_init(void)
{
    hal_target_init();
    swd_init();
    set_swd_divisor(1);
}

/* access size of a memory command, 0 if it is none */
static unsigned mem_size(const uint8_t *c, bool *write, bool *inc)
{
    *inc = true;
    *write = false;
    if (c[0] != STLINK_DEBUG_COMMAND)
        return 0;
    switch (c[1]) {
    case DBG_WRITEMEM_32BIT:        *write = true; return 4;
    case DBG_READMEM_32BIT:         return 4;
    case DBG_WRITEMEM_32BIT_NOINC:  *write = true; *inc = false; return 4;
    case DBG_READMEM_32BIT_NOINC:   *inc = false; return 4;
    case DBG_APIV2_WRITEMEM_16BIT:  *write = true; return 2;
    case DBG_APIV2_READMEM_16BIT:   return 2;
    case DBG_WRITEMEM_8BIT:         *write = true; return 1;
    case DBG_READMEM_8BIT:          return 1;
    default:                        return 0;
    }
}

static bool mem_aligned(const uint8_t *c, unsigned size)
{
    uint32_t a = get32(c + 2) | get16(c + 6);
    return size == 1 || !(a & (size - 1u));
}

uint32_t stlink_out_len(const uint8_t *c)
{
    bool wr, inc;
    return mem_size(c, &wr, &inc) && wr ? get16(c + 6) : 0;
}

uint32_t stlink_in_len(const uint8_t *c)
{
    bool wr, inc;
    unsigned size = mem_size(c, &wr, &inc);
    if (!size || wr)
        return 0;
    uint32_t len = get16(c + 6);
    /* single byte reads are padded to two bytes */
    return (size == 1 && len == 1) ? 2 : len;
}

void stlink_mem_in(const uint8_t *c, uint32_t off, uint8_t *buf, uint32_t n)
{
    bool wr, inc;
    unsigned size = mem_size(c, &wr, &inc);
    uint32_t len = get16(c + 6);

    memset(buf, 0, n);
    if (off == 0)
        last_status = STLINK_OK;
    if (off >= len || last_status != STLINK_OK)
        return;
    if (n > len - off)
        n = len - off;
    if (!mem_aligned(c, size)) {
        last_status = STLINK_SWD_AP_ERROR;
        return;
    }
    last_status = mem_read(get32(c + 2) + (inc ? off : 0), buf, n, size, inc);
}

void stlink_mem_out(const uint8_t *c, uint32_t off, const uint8_t *buf, uint32_t n)
{
    bool wr, inc;
    unsigned size = mem_size(c, &wr, &inc);

    if (off == 0)
        last_status = STLINK_OK;
    if (last_status != STLINK_OK)
        return;
    if (!mem_aligned(c, size)) {
        last_status = STLINK_SWD_AP_ERROR;
        return;
    }
    n &= ~(uint32_t)(size - 1u);
    if (n)
        last_status = mem_write(get32(c + 2) + (inc ? off : 0), buf, n, size, inc);
}

/* ---- debug command set ------------------------------------------------- */

static uint32_t debug_command(const uint8_t *c, uint8_t *r)
{
    uint8_t st;
    uint32_t v, addr = get32(c + 2);

    switch (c[1]) {
    case DBG_APIV1_ENTER:
    case DBG_APIV2_ENTER:
        if (c[2] != DBG_ENTER_SWD)
            return reply_status(r, STLINK_NO_DEVICE);   /* SWD only */
        mode = STLINK_MODE_DEBUG;
        hal_led(LED_ACTIVE);
        st = swd_connect(&idcode);
        if (st == STLINK_OK)
            st = mem_ap_init(0);
        if (st != STLINK_OK)
            idcode = 0;
        /* a genuine ST-Link reports success here even without a target;
         * the following IDCODE read tells the host whether one is present */
        return reply_status(r, STLINK_OK);

    case DBG_EXIT:
        swd_disconnect();
        mode = STLINK_MODE_MASS;
        hal_led(LED_IDLE);
        return 0;

    case DBG_READCOREID:
        put32(r, idcode);
        return 4;

    case DBG_APIV2_READ_IDCODES:
        st = idcode ? STLINK_OK : STLINK_SWD_DP_ERROR;
        if (!idcode && swd_connect(&idcode) == STLINK_OK && mem_ap_init(0) == STLINK_OK)
            st = STLINK_OK;
        memset(r, 0, 12);
        r[0] = st;
        put32(r + 4, idcode);
        return 12;

    case DBG_GETSTATUS:
        st = mem_read32(DHCSR, &v);
        return reply_status(r, st != STLINK_OK ? st :
                            (v & S_HALT) ? STLINK_CORE_HALTED : STLINK_CORE_RUNNING);

    case DBG_FORCEDEBUG:
        return reply_status(r, core_dhcsr(C_HALT | C_DEBUGEN));

    case DBG_RUNCORE:
        return reply_status(r, core_dhcsr(C_DEBUGEN));

    case DBG_STEPCORE:
        return reply_status(r, core_dhcsr(C_STEP | C_DEBUGEN));

    case DBG_APIV1_RESETSYS:
    case DBG_APIV2_RESETSYS:
        mem_write32(AIRCR, 0x05FA0004u);
        hal_delay_ms(2);
        swd_clear_errors();
        mem_read32(DHCSR, &v);          /* clear S_RESET_ST */
        return reply_status(r, STLINK_OK);

    case DBG_APIV2_DRIVE_NRST:
        if (c[2] == 2) {
            hal_target_nrst(true);
            hal_delay_ms(10);
            hal_target_nrst(false);
        } else {
            hal_target_nrst(c[2] == 0);
        }
        return reply_status(r, STLINK_OK);

    case DBG_APIV1_READREG:
        st = core_read_reg(c[2], &v);
        put32(r, v);
        return 4;

    case DBG_APIV2_READREG:
        v = 0;
        st = core_read_reg(c[2], &v);
        return reply_status_val(r, st, v);

    case DBG_APIV1_WRITEREG:
    case DBG_APIV2_WRITEREG:
        return reply_status(r, core_write_reg(c[2], get32(c + 3)));

    case DBG_APIV1_READALLREGS:
    case DBG_APIV2_READALLREGS: {
        /* r0-r15, xPSR, MSP, PSP, CONTROL/FAULTMASK/BASEPRI/PRIMASK */
        static const uint8_t sel[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                       14, 15, 16, 17, 18, 20 };
        uint32_t off = c[1] == DBG_APIV2_READALLREGS ? 4 : 0;
        memset(r, 0, off + 21 * 4);
        st = STLINK_OK;
        for (unsigned i = 0; i < sizeof(sel) && st == STLINK_OK; i++) {
            st = core_read_reg(sel[i], &v);
            put32(r + off + 4 * i, v);
        }
        if (off)
            r[0] = st;
        return off + 21 * 4;
    }

    case DBG_APIV2_READALLREGS_FP:
        memset(r, 0, 4 + 33 * 4);
        st = mem_read32(MVFR0, &v);
        if (st == STLINK_OK && v) {
            for (unsigned i = 0; i < 32 && st == STLINK_OK; i++) {
                uint32_t s = 0;
                st = core_read_reg(0x40 + i, &s);
                put32(r + 4 + 4 * i, s);
            }
            if (st == STLINK_OK) {
                st = core_read_reg(0x21, &v);
                put32(r + 4 + 32 * 4, v);
            }
        }
        r[0] = st;
        return 4 + 33 * 4;

    case DBG_APIV1_WRITEDEBUGREG:
    case DBG_APIV2_WRITEDEBUGREG:
        st = mem_write32(addr, get32(c + 6));
        last_status = st;
        return reply_status(r, st);

    case DBG_APIV2_READDEBUGREG:
        v = 0;
        st = mem_read32(addr, &v);
        last_status = st;
        return reply_status_val(r, st, v);

    case DBG_APIV2_GETLASTRWSTATUS:
        return reply_status(r, last_status);

    case DBG_APIV2_GETLASTRWSTATUS2:
        memset(r, 0, 12);
        r[0] = last_status;
        if (last_status != STLINK_OK)
            put32(r + 4, mem_fault_addr());
        last_status = STLINK_OK;
        return 12;

    case DBG_APIV2_SWD_SET_FREQ:
        set_swd_divisor(get16(c + 2));
        return reply_status(r, STLINK_OK);

    case DBG_APIV2_JTAG_SET_FREQ:
        return reply_status(r, STLINK_OK);

    case DBG_APIV2_READ_DAP_REG: {
        uint16_t port = get16(c + 2), reg = get16(c + 4);
        v = 0;
        st = port == 0xFFFF ? dp_read(reg, &v) : ap_read(port, reg, &v);
        if (st != STLINK_OK)
            swd_clear_errors();
        return reply_status_val(r, st, v);
    }

    case DBG_APIV2_WRITE_DAP_REG: {
        uint16_t port = get16(c + 2), reg = get16(c + 4);
        v = get32(c + 6);
        st = port == 0xFFFF ? dp_write(reg, v) : ap_write(port, reg, v);
        if (st != STLINK_OK)
            swd_clear_errors();
        return reply_status(r, st);
    }

    case DBG_APIV2_INIT_AP:
        return reply_status(r, mem_ap_init(c[2]));

    case DBG_APIV2_CLOSE_AP_DBG:
        mem_set_ap(0);
        return reply_status(r, STLINK_OK);

    case DBG_APIV2_START_TRACE_RX:
        trace_max = get16(c + 2);
        trace_on = hal_swo_start(get32(c + 4));
        return reply_status(r, trace_on ? STLINK_OK : STLINK_FALSE);

    case DBG_APIV2_STOP_TRACE_RX:
        hal_swo_stop();
        trace_on = false;
        return reply_status(r, STLINK_OK);

    case DBG_APIV2_GET_TRACE_NB: {
        uint32_t n = trace_on ? stlink_trace_count() : 0;
        if (n > 0xFFFF)
            n = 0xFFFF;
        r[0] = n;
        r[1] = n >> 8;
        return 2;
    }

    case DBG_APIV2_BOARD_INFO:
        /* observed reply of an unconfigured ST-Link/V2-1 */
        memset(r, 0, 128);
        r[0] = STLINK_OK;
        memcpy(r + 24, "UNDEFINED  ", 11);
        return 128;

    case DBG_APIV2_GET_CAPS:
        /* observed reply: 2000, 1, 36000, 18, 0xffff */
        memset(r, 0, 36);
        r[0] = STLINK_OK;
        put32(r + 4, 2000);
        put32(r + 8, 1);
        put32(r + 12, 36000);
        put32(r + 16, 18);
        put32(r + 20, 0xFFFF);
        return 36;

    case DBG_APIV2_GET_TRACE_CAPS:
        memset(r, 0, 48);
        r[0] = STLINK_OK;
        r[2] = 0x0A;
        r[5] = 0x08;
        return 48;

    case DBG_APIV1_SETFP:
    case DBG_APIV1_CLEARFP:
    case DBG_APIV1_SETWATCHPOINT:
    case DBG_APIV2_SETFP:
    case DBG_APIV2_CLEARFP:
    case DBG_APIV2_SETWATCHPOINT:
        return reply_status(r, STLINK_OK);

    default:
        return reply_status(r, STLINK_FALSE);
    }
}

void stlink_execute(const uint8_t *c, uint8_t *r, uint32_t *rlen)
{
    uint32_t vref, vt;
    uint16_t ver = version_reported();

    *rlen = 0;
    switch (c[0]) {
    case STLINK_GET_VERSION:
        r[0] = ver >> 8;
        r[1] = ver;
        r[2] = STLINK_USB_VID & 0xFF;
        r[3] = STLINK_USB_VID >> 8;
        r[4] = STLINK_USB_PID & 0xFF;
        r[5] = STLINK_USB_PID >> 8;
        *rlen = 6;
        break;

    case STLINK_GET_CURRENT_MODE:
        r[0] = mode;
        r[1] = 0;
        *rlen = 2;
        break;

    case STLINK_GET_TARGET_VOLTAGE:
        hal_target_voltage(&vref, &vt);
        put32(r, vref);
        put32(r + 4, vt);
        *rlen = 8;
        break;

    case STLINK_DFU_COMMAND:
        /* DFU_EXIT: we are not in DFU mode, nothing to do */
        break;

    case STLINK_BOOTLOADER_COMMAND:
        if (c[1] == 0) {
            r[0] = STLINK_OK;
            r[1] = 0;
            *rlen = 2;
        } else if (c[1] == 1) {
            want_bootloader = true;
        }
        break;

    case STLINK_DEBUG_COMMAND:
        *rlen = debug_command(c, r);
        break;

    default:
        break;
    }
}
