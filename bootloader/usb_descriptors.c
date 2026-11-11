/*
 * Bootloader descriptors: 0483:3748 like the genuine ST-Link bootloader,
 * interface 0 = ST update protocol, interface 1 = standard DFU.
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "usb_ids.h"
#include "usb_serial.h"

static const tusb_desc_device_t dev_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = DFU_USB_VID,
    .idProduct          = DFU_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&dev_desc;
}

#define DFU_ATTRS (DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_CAN_UPLOAD | DFU_ATTR_MANIFESTATION_TOLERANT)
#define ST_IF_LEN  (9 + 3 * 7)
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + ST_IF_LEN + TUD_DFU_DESC_LEN(1))

static const uint8_t cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_LEN, 0x80, USB_MAX_POWER_MA),
    /* ST-Link bootloader interface, same endpoints as the genuine one */
    9, TUSB_DESC_INTERFACE, 0, 0, 3, 0xFF, 0xFF, 0xFF, 4,
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, 0x02, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, 0x83, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    /* standard DFU for hybridlink images */
    TUD_DFU_DESCRIPTOR(1, 1, 5, DFU_ATTRS, 1000, CFG_TUD_DFU_XFER_BUFSIZE),
};

TU_VERIFY_STATIC(sizeof(cfg_desc) == CONFIG_LEN, "config length");

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return cfg_desc;
}

static const char *const strings[] = {
    NULL, STLINK_USB_MANUFACTURER, STLINK_USB_PRODUCT, NULL, "ST Link", "hybridlink DFU",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t buf[33];
    (void)langid;
    if (index == 0) {
        buf[0] = (3u << 8) | 4u;
        buf[1] = 0x0409;
        return buf;
    }
    if (index == 3) {
        char sn[25];
        usb_serial_string(sn);
        return usb_string_desc(buf, 32, sn);
    }
    if (index >= sizeof(strings) / sizeof(strings[0]))
        return NULL;
    return usb_string_desc(buf, 32, strings[index]);
}
