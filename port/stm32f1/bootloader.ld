/* SPDX-License-Identifier: MIT */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 15K   /* 0x08003C00: ST configuration page */
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K - 16
}
BOOT_HEADER_OFFSET = 0x100;
INCLUDE sections.ld
ASSERT(boot_header == 0x08000100, "boot header must be at 0x08000100")
