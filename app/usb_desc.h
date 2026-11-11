/* SPDX-License-Identifier: MIT */
#ifndef USB_DESC_H
#define USB_DESC_H

/* both modes: debug interface 0, CDC ACM on interfaces 1 and 2 */
enum { ITF_STLINK = 0, ITF_DAP = 0, ITF_CDC_CTRL = 1, ITF_CDC_DATA = 2 };

#define EP_STLINK_IN    0x81
#define EP_STLINK_OUT   0x01
#define EP_TRACE_IN     0x82
#define EP_DAP_OUT      0x01
#define EP_DAP_IN       0x81
#define EP_CDC_NOTIF    0x84
#define EP_CDC_OUT      0x05
#define EP_CDC_IN       0x85

#endif
