MEMORY
{
    RAM : ORIGIN = 0x00000000, LENGTH = 128M
}

REGION_ALIAS("startup", RAM)
REGION_ALIAS("text", RAM)
REGION_ALIAS("data", RAM)
REGION_ALIAS("sdata", RAM)

PROVIDE (__stack_top = (0xFFFF & -4) );
PROVIDE (__end_heap = (0xFFFF) );
