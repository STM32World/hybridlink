/* Common output sections. SPDX-License-Identifier: MIT */
ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))
        /* bootloader header used by ST firmware, at a fixed offset */
        . = MAX(., BOOT_HEADER_OFFSET);
        KEEP(*(.boot_header))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.ramfunc .ramfunc.*)
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    _flash_end = LOADADDR(.data) + SIZEOF(.data);
    /DISCARD/ : { *(.ARM.attributes) }
}
