/*
 * Linker script for domain-fault-ordering test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
ENTRY(_vectors)

SECTIONS
{
    . = 0x00000000;
    .vectors : { *(.vectors) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
