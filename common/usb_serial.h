/* SPDX-License-Identifier: MIT */
#ifndef USB_SERIAL_H
#define USB_SERIAL_H
#include <stdint.h>
#include <stddef.h>

/* ST-Link/V2-1 style serial number: the three UID words as 24 hex digits.
 * Writes a NUL terminated string, returns its length. */
size_t usb_serial_string(char out[25]);

/* Fill a USB string descriptor from ASCII. buf must hold 2 + 2*n bytes. */
const uint16_t *usb_string_desc(uint16_t *buf, size_t max_chars, const char *s);
#endif
