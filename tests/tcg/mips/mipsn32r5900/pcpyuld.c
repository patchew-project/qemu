/*
 * Test PCPYUD and PCPYLD.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

/* 128-bit multimedia register */
struct mmr { uint64_t hi, lo; } __attribute__((aligned(16)));

static void verify_zero(void)
{
    __asm__ __volatile__ (
        "    pcpyud  $0, $0, $0\n"
        "    pcpyld  $0, $0, $0\n"
    );
}

static void verify_copy(void)
{
    const struct mmr value = {
        .hi = 0x6665646362613938,
        .lo = 0x3736353433323130
    };
    struct mmr result = { };

    __asm__ __volatile__ (
        "    pcpyld  %0, %2, %3\n"
        "    move    %1, %0\n"
        "    pcpyud  %0, %0, %0\n"
        : "=r" (result.hi), "=r" (result.lo)
        : "r" (value.hi), "r" (value.lo));

    assert(value.hi == result.hi);
    assert(value.lo == result.lo);
}

int main()
{
    verify_zero();
    verify_copy();

    return 0;
}
