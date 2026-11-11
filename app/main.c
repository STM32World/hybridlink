/*
 * hybridlink application: ST-Link/V2-1 compatible or CMSIS-DAP debug probe
 * (SWD + virtual COM port + SWO) on TinyUSB. The mode is a setting in the
 * configuration page (see settings.h) and is fixed for one boot.
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "hal.h"
#include "layout.h"
#include "settings.h"
#include "stlink.h"
#include "dap.h"

void stlink_usb_task(void);
void dap_usb_task(void);
void vcp_init(void);
void vcp_task(void);

static void restart(bool dfu)
{
    uint32_t t = hal_millis();
    while (hal_millis() - t < 50)
        tud_task();
    hal_usb_disconnect();
    hal_delay_ms(50);
    if (dfu)
        boot_request_dfu();
    hal_system_reset();
}

int main(void)
{
    hal_init();
    settings_init();
    bool dap = settings_mode() == MODE_DAP;
    if (dap)
        dap_init();
    else
        stlink_init();
    vcp_init();
#ifdef SWD_CALIBRATE
    void swd_calibrate(void);
    swd_calibrate();
#endif
    hal_usb_init();
    tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    tusb_init(0, &dev_init);

    for (;;) {
        tud_task();
        if (dap)
            dap_usb_task();
        else
            stlink_usb_task();
        vcp_task();
        settings_task();
        enum settings_restart r = settings_restart_requested();
        if (r != RESTART_NONE)
            restart(r == RESTART_DFU);
        if (!dap && stlink_bootloader_requested())
            restart(true);
    }
}
