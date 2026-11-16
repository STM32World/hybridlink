/*
 * Status LED with the meaning of the COM LED of ST's ST-Link/V2-1 (LD1 on
 * Nucleo boards), in every probe mode.
 * SPDX-License-Identifier: MIT
 */
#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

#define LED_TARGET_NONE     0
#define LED_TARGET_OK       1
#define LED_TARGET_FAILED   2

/* written for every target transfer, so kept to two byte stores; the
 * tick interrupt (led.c) turns them into the LED state */
extern volatile uint8_t led_traffic;    /* a transfer took place          */
extern volatile uint8_t led_result;     /* LED_TARGET_* of the last one   */

static inline void led_transfer(bool ok)
{
    led_traffic = 1;
    led_result = ok ? LED_TARGET_OK : LED_TARGET_FAILED;
}

void led_host(void);            /* host software sent a probe command     */

#endif
