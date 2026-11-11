#!/usr/bin/env python3
"""Turn hybridlink-full.bin into a DAPLink "bootloader update" image.

DAPLink's interface firmware replaces its own bootloader with an image that is
dropped on the DAPLINK drive when the image
  - carries DAPLink's info block at offset 0x20 (build key of a bootloader,
    the board's HIC ID, a version): these are reserved vector table slots,
    unused by hybridlink,
  - is exactly the size of the bootloader region and ends in the standard
    CRC-32 of everything before it.
The DAPLink bootloader region (0x08000000..0x0800BBFF on the STM32F103CB)
holds the hybridlink bootloader, a blank configuration page and the
application, so the probe starts hybridlink right after the update.

  mkdaplink.py hybridlink-full.bin hybridlink-daplink.bin
"""
import struct, sys, zlib

BL_SIZE = 0xBC00                 # DAPLINK_ROM_BL_SIZE, stm32f103xb
HIC_ID = 0x97969908              # DAPLINK_HIC_ID_STM32F103XB
BUILD_KEY_BL = 0x9B939D93        # DAPLINK_BUILD_KEY_BL
INFO_OFFSET = 0x20
VERSION = 0                      # shown as the bootloader version

img = bytearray(open(sys.argv[1], "rb").read())
if len(img) > BL_SIZE - 4:
    sys.exit(f"image too large for the DAPLink bootloader region: {len(img)} > {BL_SIZE - 4}")
if any(img[INFO_OFFSET:INFO_OFFSET + 12]):
    sys.exit("vector table slots 0x20..0x2B are not free")
img[INFO_OFFSET:INFO_OFFSET + 12] = struct.pack("<III", BUILD_KEY_BL, HIC_ID, VERSION)
img += b"\xff" * (BL_SIZE - 4 - len(img))
img += struct.pack("<I", zlib.crc32(img) & 0xFFFFFFFF)
open(sys.argv[2], "wb").write(img)
