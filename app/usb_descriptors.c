/*
 * USB descriptors for both probe modes.
 *
 * ST-Link mode, ST-Link/V2-1 (no mass storage) compatible:
 *   IF0 vendor debug: EP1 OUT/IN (64), EP2 IN trace (32)
 *   IF1/IF2 CDC ACM VCP (IAD): EP4 IN notify, EP5 OUT/IN data
 *
 * WCH-Link mode, laid out like WCH-Link firmware 2.12:
 *   IF0 vendor FF/80/55: EP1 OUT/IN commands (16, see usb_desc.h),
 *   EP2 OUT/IN data (64)
 *   IF1/IF2 CDC ACM VCP, as above
 *
 * CMSIS-DAP mode:
 *   IF0 vendor "CMSIS-DAP v2": EP1 OUT/IN bulk (64)
 *   IF1/IF2 CDC ACM VCP, as above
 *   BOS + Microsoft OS 2.0 descriptors bind WinUSB to IF0 on Windows
 *
 * SPDX-License-Identifier: MIT
 */
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "usb_ids.h"
#include "usb_serial.h"
#include "usb_desc.h"
#include "settings.h"

extern const usbd_class_driver_t stlink_driver;
extern const usbd_class_driver_t dap_driver;
extern const usbd_class_driver_t wch_driver;

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count)
{
    *count = 1;
    switch (settings_mode()) {
    case MODE_DAP: return &dap_driver;
    case MODE_WCH: return &wch_driver;
    default:       return &stlink_driver;
    }
}

/* ---- device ----------------------------------------------------------- */

static const tusb_desc_device_t stlink_dev = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = STLINK_USB_VID,
    .idProduct          = STLINK_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const tusb_desc_device_t dap_dev = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0210,           /* BOS for MS OS 2.0 */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = DAP_USB_VID,
    .idProduct          = DAP_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const tusb_desc_device_t wch_dev = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = WCH_USB_VID,
    .idProduct          = WCH_USB_PID,
    .bcdDevice          = 0x0212,           /* WCH-Link firmware 2.12 */
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    switch (settings_mode()) {
    case MODE_DAP: return (const uint8_t *)&dap_dev;
    case MODE_WCH: return (const uint8_t *)&wch_dev;
    default:       return (const uint8_t *)&stlink_dev;
    }
}

/* ---- configuration ---------------------------------------------------- */

/* virtual COM port, laid out exactly like the genuine ST-Link one */
#define VCP_DESC(str_ctrl, str_data, size) \
    8, TUSB_DESC_INTERFACE_ASSOCIATION, ITF_CDC_CTRL, 2, TUSB_CLASS_CDC, \
       CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_ATCOMMAND, str_ctrl, \
    9, TUSB_DESC_INTERFACE, ITF_CDC_CTRL, 0, 1, TUSB_CLASS_CDC, \
       CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_ATCOMMAND, str_ctrl, \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0110), \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, ITF_CDC_DATA, \
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 6, \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, ITF_CDC_CTRL, ITF_CDC_DATA, \
    7, TUSB_DESC_ENDPOINT, EP_CDC_NOTIF, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(8), 16, \
    9, TUSB_DESC_INTERFACE, ITF_CDC_DATA, 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, str_data, \
    7, TUSB_DESC_ENDPOINT, EP_CDC_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(size), 0, \
    7, TUSB_DESC_ENDPOINT, EP_CDC_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(size), 0

#define STLINK_IF_LEN (9 + 3 * 7)
#define STLINK_CONFIG_LEN (TUD_CONFIG_DESC_LEN + STLINK_IF_LEN + TUD_CDC_DESC_LEN)

static const uint8_t stlink_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, STLINK_CONFIG_LEN, 0x80, USB_MAX_POWER_MA),
    9, TUSB_DESC_INTERFACE, ITF_STLINK, 0, 3, 0xFF, 0xFF, 0xFF, 4,
    7, TUSB_DESC_ENDPOINT, EP_STLINK_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EP_STLINK_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EP_TRACE_IN,   TUSB_XFER_BULK, U16_TO_U8S_LE(32), 0,
    VCP_DESC(6, 7, 64),
};
TU_VERIFY_STATIC(sizeof(stlink_cfg) == STLINK_CONFIG_LEN, "config length");

#define DAP_IF_LEN (9 + 2 * 7)
#define DAP_CONFIG_LEN (TUD_CONFIG_DESC_LEN + DAP_IF_LEN + TUD_CDC_DESC_LEN)

static const uint8_t dap_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, DAP_CONFIG_LEN, 0x80, USB_MAX_POWER_MA),
    9, TUSB_DESC_INTERFACE, ITF_DAP, 0, 2, 0xFF, 0x00, 0x00, 4,
    7, TUSB_DESC_ENDPOINT, EP_DAP_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EP_DAP_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    VCP_DESC(6, 7, 64),
};
TU_VERIFY_STATIC(sizeof(dap_cfg) == DAP_CONFIG_LEN, "config length");

#define WCH_IF_LEN (9 + 4 * 7)
#define WCH_CONFIG_LEN (TUD_CONFIG_DESC_LEN + WCH_IF_LEN + TUD_CDC_DESC_LEN)

static const uint8_t wch_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, WCH_CONFIG_LEN, 0x80, USB_MAX_POWER_MA),
    9, TUSB_DESC_INTERFACE, ITF_WCH, 0, 4, 0xFF, 0x80, 0x55, 4,
    7, TUSB_DESC_ENDPOINT, EP_WCH_DATA_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EP_WCH_DATA_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EP_WCH_CMD_IN,   TUSB_XFER_BULK, U16_TO_U8S_LE(WCH_CMD_EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, EP_WCH_CMD_OUT,  TUSB_XFER_BULK, U16_TO_U8S_LE(WCH_CMD_EP_SIZE), 0,
    VCP_DESC(6, 7, 64),
};
TU_VERIFY_STATIC(sizeof(wch_cfg) == WCH_CONFIG_LEN, "config length");

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    switch (settings_mode()) {
    case MODE_DAP: return dap_cfg;
    case MODE_WCH: return wch_cfg;
    default:       return stlink_cfg;
    }
}

/* ---- BOS / Microsoft OS 2.0 (CMSIS-DAP mode) -------------------------- */

#define MS_OS_20_VENDOR_CODE    0x20
#define MS_OS_20_DESC_LEN       (10 + 8 + 8 + 20 + 132)
#define BOS_TOTAL_LEN           (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t bos_desc[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, MS_OS_20_VENDOR_CODE),
};

const uint8_t *tud_descriptor_bos_cb(void)
{
    return settings_mode() == MODE_DAP ? bos_desc : NULL;
}

/* "DeviceInterfaceGUIDs" = the CMSIS-DAP v2 interface GUID */
#define U16S(c) c, 0
static const uint8_t ms_os_20_desc[] = {
    /* set header */
    U16_TO_U8S_LE(10), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    /* configuration subset */
    U16_TO_U8S_LE(8), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 10),
    /* function subset: interface 0 */
    U16_TO_U8S_LE(8), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_DAP, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 10 - 8),
    /* compatible ID */
    U16_TO_U8S_LE(20), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* registry property */
    U16_TO_U8S_LE(132), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(42),
    U16S('D'), U16S('e'), U16S('v'), U16S('i'), U16S('c'), U16S('e'),
    U16S('I'), U16S('n'), U16S('t'), U16S('e'), U16S('r'), U16S('f'),
    U16S('a'), U16S('c'), U16S('e'), U16S('G'), U16S('U'), U16S('I'),
    U16S('D'), U16S('s'), U16S(0),
    U16_TO_U8S_LE(80),
    U16S('{'), U16S('C'), U16S('D'), U16S('B'), U16S('3'), U16S('B'), U16S('5'),
    U16S('A'), U16S('D'), U16S('-'), U16S('2'), U16S('9'), U16S('3'), U16S('B'),
    U16S('-'), U16S('4'), U16S('6'), U16S('6'), U16S('3'), U16S('-'), U16S('A'),
    U16S('A'), U16S('3'), U16S('6'), U16S('-'), U16S('1'), U16S('A'), U16S('A'),
    U16S('E'), U16S('4'), U16S('6'), U16S('4'), U16S('6'), U16S('3'), U16S('7'),
    U16S('7'), U16S('6'), U16S('}'), U16S(0), U16S(0),
};
TU_VERIFY_STATIC(sizeof(ms_os_20_desc) == MS_OS_20_DESC_LEN, "MS OS 2.0 length");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *req)
{
#if RVSWD_RAW
    if (rvswd_raw_request(rhport, stage, req))
        return true;
#endif
    if (settings_vendor_request(rhport, stage, req))
        return true;
    if (settings_mode() == MODE_DAP && req->bRequest == MS_OS_20_VENDOR_CODE &&
        req->wIndex == 7 && req->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP)
            return true;
        uint16_t n = req->wLength < sizeof(ms_os_20_desc) ? req->wLength
                                                         : sizeof(ms_os_20_desc);
        return tud_control_xfer(rhport, req, (void *)(uintptr_t)ms_os_20_desc, n);
    }
    return false;
}

/* ---- strings ---------------------------------------------------------- */

static const char *const stlink_strings[] = {
    NULL, STLINK_USB_MANUFACTURER, STLINK_USB_PRODUCT, NULL, "ST-Link Debug",
    "ST-Link mass storage", "ST-Link VCP Ctrl", "ST-Link VCP Data",
};

static const char *const wch_strings[] = {
    NULL, STLINK_USB_MANUFACTURER, WCH_USB_PRODUCT, NULL, "WCH-Link",
    NULL, "hybridlink VCP Ctrl", "hybridlink VCP Data",
};

static const char *const dap_strings[] = {
    NULL, DAP_USB_MANUFACTURER, DAP_USB_PRODUCT, NULL, "CMSIS-DAP v2",
    NULL, "hybridlink VCP Ctrl", "hybridlink VCP Data",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t buf[33];
    const char *const *strings = settings_mode() == MODE_DAP ? dap_strings
                               : settings_mode() == MODE_WCH ? wch_strings
                               : stlink_strings;
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
    if (index >= 8 || !strings[index])
        return NULL;
    return usb_string_desc(buf, 32, strings[index]);
}
