#include <stdint.h>
#include <unistd.h>
#include "signal-helper.inc.c"

static inline void vlgv(uint64_t *r1, S390Vector *v3, const void *a2,
                        uint8_t m4)
{
    asm volatile("vlgv %[r1], %[v3], 0(%[a2]), %[m4]\n"
                 : [r1] "+d" (*r1),
                   [v3] "+v" (v3->v)
                 : [a2] "d" (a2),
                   [m4] "i" (m4));
}

int main(void)
{
    S390Vector v3 = {
        .q[0] = 0x0011223344556677ull,
        .q[1] = 0x8899aabbccddeeffull,
    };
    uint64_t r1 = 0;

    /* Directly set all ignored bits to  */
    vlgv(&r1, &v3, (void *)(7 | ~0xf), ES_8);
    check("8 bit", r1 == 0x77);
    vlgv(&r1, &v3, (void *)(4 | ~0x7), ES_16);
    check("16 bit", r1 == 0x8899);
    vlgv(&r1, &v3, (void *)(3 | ~0x3), ES_32);
    check("32 bit", r1 == 0xccddeeff);
    vlgv(&r1, &v3, (void *)(1 | ~0x1), ES_64);
    check("64 bit", r1 == 0x8899aabbccddeeffull);
    check("v3 not modified", v3.q[0] == 0x0011223344556677ull &&
                             v3.q[1] == 0x8899aabbccddeeffull);

    CHECK_SIGILL(vlgv(&r1, &v3, NULL, ES_128));
    return 0;
}
