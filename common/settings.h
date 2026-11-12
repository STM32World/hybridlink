/*
 * Probe settings kept in the configuration page.
 *
 * ST's updater stores the version of the firmware it installed in the last
 * halfword of the configuration page (big endian, V:4 J:6 M:6). hybridlink
 * reports the same word, so the version it shows can be changed without a
 * rebuild: either by ST's updater or by tools/hybridlink set-version.
 * A blank record (0xFFFF) means the compiled-in default.
 *
 * The halfword before it selects the probe mode (ST-Link or CMSIS-DAP);
 * blank means ST-Link. ST's updater rewrites the page, so installing ST
 * firmware and coming back resets the mode to ST-Link.
 *
 * Vendor control requests to the application (device recipient,
 * wIndex = HL_REQ_MAGIC), available in every mode:
 *   IN  GET_VERSION  9 bytes: reported, stored, default (big endian each),
 *                    hybridlink major, minor, patch (patch from 0.5.1 on)
 *   OUT SET_VERSION  wValue = version word (0xFFFF = default)
 *   IN  GET_MODE     2 bytes: current mode, stored mode (raw)
 *   OUT SET_MODE     wValue = mode; the probe restarts in that mode
 *   OUT ENTER_DFU    restart into the bootloader
 *   IN  SWO_STATUS   32-bit little endian words (from 0.5.3 on): receiving,
 *                    bytes received, delivered, dropped, then port specific
 *                    registers (hal_swo_status)
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "layout.h"
#include "usb_ids.h"

#define HL_REQ_GET_VERSION 0x01
#define HL_REQ_SET_VERSION 0x02
#define HL_REQ_GET_MODE    0x03
#define HL_REQ_SET_MODE    0x04
#define HL_REQ_ENTER_DFU   0x05
#define HL_REQ_SWO_STATUS  0x06
#define HL_REQ_MAGIC       0x484C          /* "HL" */

#define CFG_MODE_ADDR       (CFG_BASE + CFG_SIZE - 4u)

enum probe_mode { MODE_STLINK = 0, MODE_DAP = 1, MODE_COUNT };

#define STLINK_VERSION_DEFAULT \
    ((STLINK_VER_STLINK << 12) | (STLINK_VER_JTAG << 6) | STLINK_VER_MSD)

static inline uint16_t cfg_read16(uint32_t addr)
{
    const volatile uint8_t *v = (const volatile uint8_t *)addr;
    return (uint16_t)((v[0] << 8) | v[1]);
}

static inline uint16_t version_stored(void)
{
    return cfg_read16(CFG_VERSION_ADDR);
}

static inline uint16_t version_reported(void)
{
    uint16_t v = version_stored();
    return (v == 0xFFFF || v == 0) ? STLINK_VERSION_DEFAULT : v;
}

static inline enum probe_mode mode_stored(void)
{
    uint16_t m = cfg_read16(CFG_MODE_ADDR);
    return m < MODE_COUNT ? (enum probe_mode)m : MODE_STLINK;
}

/* rewrites one halfword of the configuration page, keeping the rest */
bool settings_store16(uint32_t addr, uint16_t v);

/* application side */
enum probe_mode settings_mode(void);        /* mode this boot runs in */
void settings_init(void);
void settings_task(void);
bool settings_vendor_request(uint8_t rhport, uint8_t stage,
                             const void *request);   /* tusb_control_request_t */
enum settings_restart { RESTART_NONE, RESTART_APP, RESTART_DFU };
enum settings_restart settings_restart_requested(void);

#endif
