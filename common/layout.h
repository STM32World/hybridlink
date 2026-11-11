/*
 * Flash / RAM layout shared by the bootloader, the application and the
 * host tools.
 *
 *   0x08000000  bootloader (15 KiB), ST compatible header at 0x08000100
 *   0x08003C00  configuration page, written by ST's updater for ST firmware
 *   0x08004000  application (vector table first), up to the end of flash
 *
 *   RAM top - 16 bytes: boot mailbox (not initialised by either startup),
 *   used by the application to ask the bootloader to stay in DFU mode.
 *
 * The bootloader starts the application when its vector table looks sane.
 * While an image is downloaded the first flash page of the application is
 * written last, so an interrupted update never leaves a startable but
 * incomplete image behind.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>

#define FLASH_BASE_ADDR     0x08000000u
#define BOOT_BASE           0x08000000u
#define BOOT_SIZE           0x4000u
#define APP_BASE            (BOOT_BASE + BOOT_SIZE)
#define CFG_BASE            0x08003C00u   /* configuration page (1 KiB)      */
#define CFG_SIZE            0x400u
#define CFG_VERSION_ADDR    (CFG_BASE + CFG_SIZE - 2u)

#define RAM_BASE_ADDR       0x20000000u
#define RAM_SIZE_BYTES      (20u * 1024u)
#define RAM_END_ADDR        (RAM_BASE_ADDR + RAM_SIZE_BYTES)

/* hybridlink firmware carries this word in the first reserved vector table
 * slot (offset 0x1C); the bootloader only reads back images that have it */
#define HL_IMAGE_MAGIC      0x4B4E4C48u   /* "HLNK" */
#define HL_IMAGE_MAGIC_OFF  0x1Cu

#define BOOT_MAILBOX_ADDR   (RAM_END_ADDR - 16u)
#define BOOT_MAGIC_DFU      0xD0F0B007u   /* stay in the bootloader          */

struct boot_mailbox {
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t reserved[2];
};

#define BOOT_MAILBOX ((volatile struct boot_mailbox *)BOOT_MAILBOX_ADDR)

static inline void boot_request_dfu(void)
{
    BOOT_MAILBOX->magic = BOOT_MAGIC_DFU;
    BOOT_MAILBOX->magic_inv = ~BOOT_MAGIC_DFU;
}

#endif
