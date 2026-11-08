/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Process Mission
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    .text : { *(.text.start) *(.text) }
    . = ALIGN(0x10000);
    .vectors : { *(.vectors) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
