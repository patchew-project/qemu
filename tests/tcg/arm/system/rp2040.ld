ENTRY(reset_handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2M
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

SECTIONS
{
    .boot2 ORIGIN(FLASH) :
    {
        KEEP(*(.boot2))
    } > FLASH

    ASSERT(SIZEOF(.boot2) == 256, "RP2040 boot2 must be exactly 256 bytes")

    .vectors ORIGIN(FLASH) + 0x100 :
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
