/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(reset_handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2M
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

SECTIONS
{
    .vectors ORIGIN(FLASH) :
    {
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data :
    {
        *(.data*)
    } > SRAM AT > FLASH

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    } > SRAM

    /DISCARD/ :
    {
        *(.ARM.attributes)
        *(.comment)
    }
}
