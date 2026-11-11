/*
 * hybridlink bootloader.
 *
 * USB identity 0483:3748 (like the genuine ST-Link bootloader), composite:
 *   interface 0: ST-Link update protocol (ST-LinkUpgrade / STM32CubeIDE)
 *   interface 1: standard USB DFU 1.1 (dfu-util), for hybridlink images
 *
 * The application is started unless
 *   - the application asked to stay here (RAM mailbox), or
 *   - the application vector table does not look valid.
 *
 * ST firmware finds this bootloader through the header at 0x08000100 and
 * enters update mode through its entry point.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "tusb.h"
#include "hal.h"
#include "layout.h"
#include "image.h"
#include "st_proto.h"

/* ---- header used by ST's application firmware ------------------------ */

static void __attribute__((noreturn, used, noinline)) boot_dfu_entry(void)
{
    /* called by the application with its own clock and peripheral setup */
    boot_request_dfu();
    hal_system_reset();
}

struct boot_header {
    uint32_t magic;
    uint32_t version;
    void (*dfu_entry)(void);
};

__attribute__((section(".boot_header"), used))
const struct boot_header boot_header = { 0x47A53C15u, 0x21000000u, boot_dfu_entry };

/* ---- image writer ------------------------------------------------------ */

#define PAGE_MAX 2048u
static uint8_t first_page[PAGE_MAX];
static bool first_page_pending;

uint32_t image_flash_end(void)
{
    return FLASH_BASE_ADDR + hal_flash_size();
}

bool image_valid(void)
{
    const uint32_t *v = (const uint32_t *)APP_BASE;
    uint32_t sp = v[0], pc = v[1];
    if (sp < RAM_BASE_ADDR + 0x100u || sp > RAM_END_ADDR)
        return false;
    if (!(pc & 1u) || pc < APP_BASE + 8u || pc >= image_flash_end())
        return false;
    return true;
}

bool image_erase(uint32_t page)
{
    if (page == APP_BASE) {
        memset(first_page, 0xFF, sizeof(first_page));
        first_page_pending = true;
    }
    return hal_flash_erase_page(page);
}

bool image_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t page = hal_flash_page_size();
    if (first_page_pending && addr < APP_BASE + page && addr + len > APP_BASE) {
        uint32_t lo = addr < APP_BASE ? APP_BASE - addr : 0;
        uint32_t hi = addr + len > APP_BASE + page ? APP_BASE + page - addr : len;
        memcpy(first_page + (addr + lo - APP_BASE), data + lo, hi - lo);
        if (lo && !hal_flash_write(addr, data, lo))
            return false;
        if (hi < len && !hal_flash_write(addr + hi, data + hi, len - hi))
            return false;
        return true;
    }
    return hal_flash_write(addr, data, len);
}

bool image_commit(void)
{
    if (!first_page_pending)
        return true;
    first_page_pending = false;
    return hal_flash_write(APP_BASE, first_page, hal_flash_page_size());
}

/* ---- standard DFU (interface 1) --------------------------------------- */

static bool manifested;
static uint32_t erased_upto;
static volatile bool leave;

uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
    (void)alt;
    return state == DFU_DNBUSY ? 30 : (state == DFU_MANIFEST ? 60 : 0);
}

void tud_dfu_download_cb(uint8_t alt, uint16_t block, const uint8_t *data, uint16_t len)
{
    (void)alt;
    uint32_t addr = APP_BASE + (uint32_t)block * CFG_TUD_DFU_XFER_BUFSIZE;

    hal_led(block & 1 ? LED_ACTIVE : LED_IDLE);
    if (block == 0) {
        manifested = false;
        erased_upto = APP_BASE;
    }
    if (addr + len > image_flash_end()) {
        tud_dfu_finish_flashing(DFU_STATUS_ERR_ADDRESS);
        return;
    }
    while (erased_upto < addr + len) {
        if (!image_erase(erased_upto)) {
            tud_dfu_finish_flashing(DFU_STATUS_ERR_ERASE);
            return;
        }
        erased_upto += hal_flash_page_size();
    }
    tud_dfu_finish_flashing(image_write(addr, data, len) ? DFU_STATUS_OK : DFU_STATUS_ERR_PROG);
}

void tud_dfu_manifest_cb(uint8_t alt)
{
    (void)alt;
    if (!image_commit()) {
        tud_dfu_finish_flashing(DFU_STATUS_ERR_VERIFY);
        return;
    }
    manifested = true;
    hal_led(LED_IDLE);
    tud_dfu_finish_flashing(image_valid() ? DFU_STATUS_OK : DFU_STATUS_ERR_FIRMWARE);
}

uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block, uint8_t *data, uint16_t len)
{
    (void)alt;
#if !DFU_UPLOAD_ANY
    /* only hybridlink images are read back: other firmware installed
     * through the ST protocol (ST's own) stays unreadable over USB */
    if (*(const uint32_t *)(APP_BASE + HL_IMAGE_MAGIC_OFF) != HL_IMAGE_MAGIC)
        return 0;
#endif
    uint32_t addr = APP_BASE + (uint32_t)block * CFG_TUD_DFU_XFER_BUFSIZE;
    if (addr >= image_flash_end())
        return 0;
    if (addr + len > image_flash_end())
        len = (uint16_t)(image_flash_end() - addr);
    memcpy(data, (const void *)addr, len);
    return len;
}

void tud_dfu_abort_cb(uint8_t alt)
{
    (void)alt;
}

void tud_dfu_detach_cb(void)
{
    leave = true;
}

void tud_umount_cb(void)
{
    if (manifested)
        leave = true;
}

/* ---- main ---------------------------------------------------------------- */

static bool stay_requested(void)
{
    bool req = BOOT_MAILBOX->magic == BOOT_MAGIC_DFU &&
               BOOT_MAILBOX->magic_inv == (uint32_t)~BOOT_MAGIC_DFU;
    BOOT_MAILBOX->magic = 0;
    BOOT_MAILBOX->magic_inv = 0;
    return req;
}

static void __attribute__((noreturn)) restart(void)
{
    uint32_t t = hal_millis();
    while (hal_millis() - t < 20)
        tud_task();
    hal_usb_disconnect();
    hal_delay_ms(20);
    hal_system_reset();
}

int main(void)
{
    if (!stay_requested() && image_valid())
        hal_jump(APP_BASE);

    hal_init();
    hal_usb_init();
    tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    tusb_init(0, &dev_init);

    uint32_t blink = 0;
    bool on = false;
    for (;;) {
        tud_task();
        st_proto_task();
        if (hal_millis() - blink > 250) {
            blink = hal_millis();
            on = !on;
            hal_led(on ? LED_ACTIVE : LED_OFF);
        }
        if (st_proto_exit_requested()) {
            image_commit();
            restart();
        }
        if (leave && image_valid())
            restart();
    }
}
