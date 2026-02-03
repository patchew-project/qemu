/* Linker script to create RX PT_LOAD with anonymous .bss tail
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
PHDRS
{
    text PT_LOAD FLAGS(5);
}
SECTIONS
{
    . = 0x400000;
    .text : {
        *(.text .text.*)
        _etext = .;
    } :text
    .bss ALIGN(1) : {
        _bss_start = .;
        *(.bss .bss.* COMMON)
        _bss_end = .;
    } :text
    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.note.gnu.property)
        *(.comment)
        *(.eh_frame)
    }
}
ENTRY(_start)
