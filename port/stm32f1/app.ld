/* SPDX-License-Identifier: MIT */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K - 16
}
BOOT_HEADER_OFFSET = 0;
INCLUDE sections.ld
