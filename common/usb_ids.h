/*
 * USB identities. The application must use ST's VID/PID so that ST tools
 * (STM32CubeIDE, STM32CubeProgrammer) as well as OpenOCD, pyOCD and stlink
 * recognise it as an ST-Link/V2-1.
 *
 * The bootloader uses the genuine ST-Link bootloader identity (0483:3748)
 * so ST-LinkUpgrade / STM32CubeIDE find it; it also carries a standard DFU
 * interface for dfu-util.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef USB_IDS_H
#define USB_IDS_H

#ifndef STLINK_USB_VID
#define STLINK_USB_VID      0x0483
#endif
#ifndef STLINK_USB_PID
#define STLINK_USB_PID      0x3752      /* ST-Link/V2-1 without mass storage */
#endif

#ifndef DFU_USB_VID
#define DFU_USB_VID         0x0483
#endif
#ifndef DFU_USB_PID
#define DFU_USB_PID         0x3748
#endif

/* Default reported ST-Link firmware version (V2 J48 M35, same as a current
 * V2-1); the configuration page can override it, see settings.h */
#ifndef STLINK_VER_STLINK
#define STLINK_VER_STLINK   2
#endif
#ifndef STLINK_VER_JTAG
#define STLINK_VER_JTAG     48
#endif
#ifndef STLINK_VER_MSD
#define STLINK_VER_MSD      35
#endif

/* CMSIS-DAP mode. Tools find CMSIS-DAP probes by "CMSIS-DAP" in the
 * product string, not by VID/PID; 0D28:0204 is the DAPLink identity, kept
 * as default so existing udev rules and tool configurations apply. */
#ifndef DAP_USB_VID
#define DAP_USB_VID         0x0D28
#endif
#ifndef DAP_USB_PID
#define DAP_USB_PID         0x0204
#endif
/* bMaxPower requested in every configuration: the probe can supply a
 * target board from VBUS */
#ifndef USB_MAX_POWER_MA
#define USB_MAX_POWER_MA    500
#endif

/* USB strings in ST-Link mode and in the bootloader. ST's tools identify
 * the probe by VID/PID and protocol, not by these strings (tested with
 * STM32CubeProgrammer, the CubeIDE 1.17-2.2 gdbservers and ST-LinkUpgrade),
 * so the probe does not claim to be made by ST. */
#ifndef STLINK_USB_MANUFACTURER
#define STLINK_USB_MANUFACTURER "STM32World"
#endif
#ifndef STLINK_USB_PRODUCT
#define STLINK_USB_PRODUCT  "hybridlink ST-Link compatible"  /* max 32 chars */
#endif

/* CMSIS-DAP tools find the probe by "CMSIS-DAP" in the product string;
 * the manufacturer is only shown (also as the DAP_Info vendor) */
#ifndef DAP_USB_MANUFACTURER
#define DAP_USB_MANUFACTURER STLINK_USB_MANUFACTURER
#endif
#ifndef DAP_USB_PRODUCT
#define DAP_USB_PRODUCT     "hybridlink CMSIS-DAP"
#endif

#define HYBRIDLINK_VERSION  "0.5.0"      /* make dist / releases use this */
#define HYBRIDLINK_VER_MAJOR 0
#define HYBRIDLINK_VER_MINOR 5

#endif
