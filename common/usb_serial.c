/* SPDX-License-Identifier: MIT */
#include "usb_serial.h"
#include "hal.h"

size_t usb_serial_string(char out[25])
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t uid[3];
    hal_uid(uid);
    size_t n = 0;
    for (int w = 0; w < 3; w++)
        for (int s = 28; s >= 0; s -= 4)
            out[n++] = hex[(uid[w] >> s) & 0xF];
    out[n] = 0;
    return n;
}

const uint16_t *usb_string_desc(uint16_t *buf, size_t max_chars, const char *s)
{
    size_t n = 0;
    while (s[n] && n < max_chars) {
        buf[1 + n] = (uint8_t)s[n];
        n++;
    }
    buf[0] = (uint16_t)((3u << 8) | (2u * n + 2u));
    return buf;
}
