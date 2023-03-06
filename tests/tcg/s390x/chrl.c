#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

static void test_chrl(void)
{
    uint32_t program_mask, cc;

    asm volatile (
               ".pushsection .rodata\n"
        "0:	.short	1,0x8000\n"
        "	.popsection\n"

        "	chrl	%[r],0b\n"
        "	ipm	%[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (1)
    );

    cc = program_mask >> 28;
    assert(!cc);

    asm volatile (
               ".pushsection .rodata\n"
        "0:	.short	-1,0x8000\n"
        "	.popsection\n"

        "	chrl	%[r],0b\n"
        "	ipm	%[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (-1)
    );

    cc = program_mask >> 28;
    assert(!cc);
}

static void test_cghrl(void)
{
    uint32_t program_mask, cc;

    asm volatile (
               ".pushsection .rodata\n"
        "0:	.short	1,0x8000,0,0\n"
        "	.popsection\n"

        "	cghrl	%[r],0b\n"
        "	ipm	%[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (1L)
    );

    cc = program_mask >> 28;
    assert(!cc);

    asm volatile (
               ".pushsection .rodata\n"
        "0:	.short	-1,0x8000,0,0\n"
        "	.popsection\n"

        "	cghrl	%[r],0b\n"
        "	ipm	%[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (-1L)
    );

    cc = program_mask >> 28;
    assert(!cc);
}

int main(void)
{
    test_chrl();
    test_cghrl();
    return EXIT_SUCCESS;
}
