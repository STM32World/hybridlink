/*
 * Status LED, following the COM LED of ST's ST-Link/V2-1 (UM1724, LD1).
 * The Jolt / Streamline Connect has a red and a blue LED on one pin (red,
 * blue or off; never both), so ST's green is blue here and ST's orange
 * (failure) is red:
 *
 *   slow red blink      USB not configured yet
 *   fast red blink      enumerated, no host software has talked to the probe
 *   red                 host software talked to the probe, no target yet,
 *                       or the last target transfer failed
 *   red/blue blink      target communication in progress
 *   blue                last target communication succeeded
 *   off                 USB suspended
 *
 * The same in ST-Link, CMSIS-DAP and WCH-Link mode. The SWD and RVSWD
 * layers record every transfer (led.h), the protocol handlers the first
 * command; the tick interrupt turns that into the LED state.
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "hal.h"
#include "led.h"

#define SLOW_MS     500u        /* half periods */
#define FAST_MS     100u
#define BLINK_MS    50u
#define BUSY_MS     100u        /* traffic this recent blinks red/green */

volatile uint8_t led_traffic;
volatile uint8_t led_result;

static volatile bool host_seen;
static bool busy;
static uint32_t last_activity;
static int shown = -1;

void led_host(void)
{
    host_seen = true;
}

/* every 1 ms from the tick interrupt, so the LED keeps blinking while a
 * long debug command holds up the main loop */
void hal_tick_hook(void)
{
    uint32_t now = hal_millis();
    enum hal_led c;

    if (!tud_mounted()) {
        host_seen = false;
        led_result = LED_TARGET_NONE;
        led_traffic = 0;
        busy = false;
    }
    if (led_traffic) {
        led_traffic = 0;
        last_activity = now;
        busy = true;
    } else if (busy && now - last_activity >= BUSY_MS) {
        busy = false;
    }
    uint8_t target = led_result;

    if (tud_suspended())
        c = LED_OFF;
    else if (!tud_mounted())
        c = (now / SLOW_MS) & 1u ? LED_OFF : LED_RED;
    else if (!host_seen)
        c = (now / FAST_MS) & 1u ? LED_OFF : LED_RED;
    else if (busy && target != LED_TARGET_NONE)
        c = (now / BLINK_MS) & 1u ? LED_BLUE : LED_RED;
    else if (target == LED_TARGET_OK)
        c = LED_BLUE;
    else
        c = LED_RED;                    /* no target yet, or it failed */

    if ((int)c != shown) {
        shown = (int)c;
        hal_led(c);
    }
}
