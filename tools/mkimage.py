#!/usr/bin/env python3
"""Combine bootloader and application into one image for SWD flashing at 0x08000000."""
import sys
BOOT_SIZE = 0x4000          # application starts here
BOOT_MAX = 0x3C00           # configuration page follows the bootloader
boot = open(sys.argv[1], 'rb').read()
app = open(sys.argv[2], 'rb').read()
if len(boot) > BOOT_MAX:
    sys.exit(f"bootloader too large: {len(boot)} > {BOOT_MAX}")
open(sys.argv[3], 'wb').write(boot + b'\xff' * (BOOT_SIZE - len(boot)) + app)
