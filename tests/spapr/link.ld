/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(_start)
SECTIONS { . = 0x400000; .text : { *(.text) } }
