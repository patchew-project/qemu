/*
 * pic32mk_test_fw.ld — Linker script for PIC32MK functional test firmware
 *
 * Links everything at Boot Flash 1 (KSEG1 0xBFC40000, physical 0x1FC40000).
 * QEMU loads the raw binary there via load_image_targphys().
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS {
    . = 0xBFC40000;
    .text : {
        *(.text*)
        *(.rodata*)
    }
    /DISCARD/ : { *(*) }
}
