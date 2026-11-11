/*
 * hybridlink bootloader installer for STM32F1 probes.
 *
 * A small application, linked at the application address (0x08004000),
 * that replaces whatever bootloader the probe has with the hybridlink
 * bootloader embedded in it. It is uploaded through the probe's existing
 * bootloader (for ST's bootloader: tools/hybridlink install), so a probe
 * can be converted over USB only.
 *
 *   1. if the bootloader area already holds the embedded bootloader, skip
 *   2. refuse if the bootloader area is read or write protected: the last
 *      flash page (where ST's bootloader keeps its "application valid"
 *      marker) is erased so the old bootloader stays in update mode
 *   3. otherwise write the bootloader page by page, verifying each page
 *   4. ask the hybridlink bootloader to stay in DFU (RAM mailbox) and reset
 *
 * The hybridlink bootloader then waits for the application, which replaces
 * this installer. Until it does, the installer only repeats step 1 and 4,
 * so a probe that is unplugged in between comes back in DFU mode.
 *
 * The configuration page (0x08003C00) is left as it is.
 *
 * Between erasing the first bootloader page and writing the last one (well
 * under a second) the probe has no working bootloader; losing power then
 * needs an SWD reflash.
 *
 * Runs with interrupts disabled and the clock the old bootloader left.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f1.h"
#include "board.h"
#include "layout.h"

#define FLASH_WRPR      REG32(FLASH_R_BASE + 0x20)
#define OBR_RDPRT       (1u << 1)
#define PAGE            1024u

extern const uint8_t boot_image[], boot_image_end[];
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

/* ---- flash ------------------------------------------------------------- */

static void unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = 0x45670123u;
        FLASH_KEYR = 0xCDEF89ABu;
    }
}

static bool wait(void)
{
    while (FLASH_SR & FLASH_SR_BSY) { }
    uint32_t sr = FLASH_SR;
    FLASH_SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
    return !(sr & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR));
}

static bool erase(uint32_t addr)
{
    unlock();
    FLASH_CR = FLASH_CR_PER;
    FLASH_AR = addr;
    FLASH_CR = FLASH_CR_PER | FLASH_CR_STRT;
    bool ok = wait();
    FLASH_CR = FLASH_CR_LOCK;
    return ok;
}

static bool program(uint32_t addr, const uint8_t *s, uint32_t len)
{
    bool ok = true;
    unlock();
    FLASH_CR = FLASH_CR_PG;
    for (uint32_t i = 0; ok && i < len; i += 2) {
        uint16_t hw = s[i] | ((i + 1 < len ? s[i + 1] : 0xFF) << 8);
        volatile uint16_t *d = (volatile uint16_t *)(addr + i);
        *d = hw;
        ok = wait();
    }
    FLASH_CR = FLASH_CR_LOCK;
    return ok;
}

static bool page_matches(uint32_t addr, const uint8_t *s, uint32_t len)
{
    const uint8_t *f = (const uint8_t *)addr;
    for (uint32_t i = 0; i < PAGE; i++)
        if (f[i] != (i < len ? s[i] : 0xFF))
            return false;
    return true;
}

static bool write_page(uint32_t addr, const uint8_t *s, uint32_t len)
{
    for (int tries = 0; tries < 3; tries++) {
        if (page_matches(addr, s, len))
            return true;
        if (erase(addr))
            program(addr, s, len);
    }
    return page_matches(addr, s, len);
}

/* ---- status LED ---------------------------------------------------------- */

static void led(bool on)
{
    RCC_APB2ENR |= 1u << 2;                         /* IOPA (LED port) */
    if (on)
        GPIO_BRR(LED_PORT) = 1u << LED_PIN;
    else
        GPIO_BSRR(LED_PORT) = 1u << LED_PIN;
    gpio_mode(LED_PORT, LED_PIN, GPIO_OUT_PP_2);
}

static void spin(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) { }
}

static void __attribute__((noreturn)) reset(void)
{
    __asm volatile ("dsb");
    SCB_AIRCR = 0x05FA0004u;
    for (;;) { }
}

static void __attribute__((noreturn)) fail(void)
{
    /* invalidate this installer for the old bootloader, then blink */
    uint32_t last = FLASH_BASE_ADDR + FLASH_SIZE_KB * 1024u - PAGE;
    if (FLASH_SIZE_KB >= 64 && FLASH_SIZE_KB <= 512)
        erase(last);
    for (int i = 0; i < 40; i++) {
        led(i & 1);
        spin(100000);
    }
    reset();
}

/* ---- installer ----------------------------------------------------------- */

static bool image_ok(uint32_t len)
{
    const uint32_t *v = (const uint32_t *)boot_image;
    return len >= 256 && len <= CFG_BASE - BOOT_BASE &&
           v[0] > RAM_BASE_ADDR && v[0] <= RAM_END_ADDR &&
           (v[1] & 1u) && v[1] > BOOT_BASE && v[1] < BOOT_BASE + len;
}

static void install(void)
{
    uint32_t len = (uint32_t)(boot_image_end - boot_image);
    if (!image_ok(len))
        fail();

    bool same = true;
    for (uint32_t off = 0; off < len; off += PAGE)
        same = same && page_matches(BOOT_BASE + off, boot_image + off,
                                    len - off < PAGE ? len - off : PAGE);
    if (same)
        return;

    /* read protection write-protects the first 4 KiB; WRPR bits 0..3
     * cover 0x08000000..0x08003FFF in 4 KiB steps */
    if ((FLASH_OBR & OBR_RDPRT) || (FLASH_WRPR & 0xFu) != 0xFu)
        fail();

    led(true);
    /* the vector table page last, so a cut short install is less likely
     * to leave a bootloader that starts and crashes */
    for (uint32_t off = PAGE; off < len; off += PAGE)
        if (!write_page(BOOT_BASE + off, boot_image + off,
                        len - off < PAGE ? len - off : PAGE))
            fail();
    for (uint32_t off = (len + PAGE - 1) & ~(PAGE - 1); off < CFG_BASE - BOOT_BASE; off += PAGE)
        if (!write_page(BOOT_BASE + off, boot_image, 0))
            fail();
    if (!write_page(BOOT_BASE, boot_image, len < PAGE ? len : PAGE))
        fail();
    led(false);
}

int main(void)
{
    install();
    boot_request_dfu();
    reset();
}

/* ---- start-up ------------------------------------------------------------ */

void Reset_Handler(void)
{
    __asm volatile ("cpsid i");
    SYST_CSR = 0;
    for (int i = 0; i < 3; i++) {
        NVIC_ICER(i) = 0xFFFFFFFFu;
        NVIC_ICPR(i) = 0xFFFFFFFFu;
    }
    SCB_VTOR = FLASH_BASE_ADDR + BOOT_SIZE;
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata)
        *d++ = *s++;
    for (d = &_sbss; d < &_ebss; )
        *d++ = 0;
    main();
}

static void Default_Handler(void)
{
    reset();
}

__attribute__((section(".isr_vector"), used))
static void (*const vectors[16])(void) = {
    (void (*)(void))&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    0, 0, 0, 0,
    Default_Handler, Default_Handler, 0, Default_Handler, Default_Handler,
};
