#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#define TEST(INSN, B_HI, B_LO, T_HI, T_LO) \
    do {                                                                \
        __uint128_t t, b = B_HI;                                        \
        b <<= 64;                                                       \
        b |= B_LO;                                                      \
        asm(INSN " %x0, %x1\n\t"                                        \
            : "=wa" (t)                                                 \
            : "wa" (b));                                                \
        printf(INSN "(0x%016" PRIx64 "%016" PRIx64 ") = 0x%016" PRIx64  \
               "%016" PRIx64 "\n", (uint64_t)(b >> 64), (uint64_t)b,    \
               (uint64_t)(t >> 64), (uint64_t)t);                       \
        assert((uint64_t)(t >> 64) == T_HI && (uint64_t)t == T_LO);     \
    } while (0)

int main(void)
{
#ifndef __SIZEOF_INT128__
    puts("__uint128_t not available, skipping...\n");
#else
    /* SNaN shouldn't be silenced */
    TEST("xscvspdpn", 0x7fbfffff00000000ULL, 0x0, 0x7ff7ffffe0000000ULL, 0x0);
    TEST("xscvdpspn", 0x7ff7ffffffffffffULL, 0x0, 0x7fbfffff7fbfffffULL, 0x0);

    /*
     * SNaN inputs having no significant bits in the upper 23 bits of the
     * signifcand will return Infinity as the result.
     */
    TEST("xscvdpspn", 0x7ff000001fffffffULL, 0x0, 0x7f8000007f800000ULL, 0x0);
#endif
    return 0;
}
