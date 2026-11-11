/* The bootloader image embedded in the installer. SPDX-License-Identifier: MIT */
    .section .rodata.boot_image, "a"
    .balign 4
    .global boot_image, boot_image_end
boot_image:
    .incbin BOOT_IMAGE
boot_image_end:
