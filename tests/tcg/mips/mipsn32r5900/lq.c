/*
 * Test LQ.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

/* 128-bit multimedia register */
struct mmr { uint64_t hi, lo; } __attribute__((aligned(16)));

#define LQ(base, offset) \
    ({ \
        uint64_t hi, lo; \
    \
        __asm__ __volatile__ ( \
            "    pcpyld  %1, %1, %1\n" \
            "    lq %1, %3(%2)\n" \
            "    pcpyud  %0, %1, %1\n" \
            : "=r" (hi), "=r" (lo) \
            : "r" (base), "i" (offset)); \
    \
        (struct mmr) { .hi = hi, .lo = lo }; \
    })

static uint64_t ld_reference(const void *base, int16_t offset)
{
    const uint8_t *p = base;
    uint64_t r = 0;
    int i;

    for (i = 0; i < 8; i++) {
        r |= (uint64_t)p[offset + i] << (8 * i);
    }

    return r;
}

static struct mmr lq_reference(const void *base, int16_t offset)
{
    /*
     * The least significant four bits of the effective address are
     * masked to zero, effectively creating an aligned address. No
     * address exceptions due to alignment are possible.
     */
    const uint8_t *b = base;
    const uint8_t *o = &b[offset];
    const void *a = (const void*)((unsigned long)o & ~0xFUL);

    return (struct mmr) {
        .hi = ld_reference(a, 8),
        .lo = ld_reference(a, 0)
    };
}

static void assert_equal_mmr(struct mmr a, struct mmr b)
{
    assert(a.hi == b.hi);
    assert(a.lo == b.lo);
}

#define VERIFY_LQ(base, offset) \
    assert_equal_mmr(LQ(base, offset), lq_reference(base, offset))

int main()
{
    static const char data[] __attribute__((aligned(16)))=
        "0123456789abcdef"
        "ghijklmnopqrstuv"
        "wxyzABCDEFGHIJKL"
        "MNOPQRSTUVWXYZ.,";
    int i;

    for (i = 16; i < 48; i++) {
        VERIFY_LQ(&data[i], -16);
        VERIFY_LQ(&data[i], -15);
        VERIFY_LQ(&data[i], -14);
        VERIFY_LQ(&data[i], -13);
        VERIFY_LQ(&data[i], -12);
        VERIFY_LQ(&data[i], -11);
        VERIFY_LQ(&data[i], -10);
        VERIFY_LQ(&data[i], -9);
        VERIFY_LQ(&data[i], -8);
        VERIFY_LQ(&data[i], -7);
        VERIFY_LQ(&data[i], -6);
        VERIFY_LQ(&data[i], -5);
        VERIFY_LQ(&data[i], -4);
        VERIFY_LQ(&data[i], -3);
        VERIFY_LQ(&data[i], -2);
        VERIFY_LQ(&data[i], -1);
        VERIFY_LQ(&data[i],  0);
        VERIFY_LQ(&data[i],  1);
        VERIFY_LQ(&data[i],  2);
        VERIFY_LQ(&data[i],  3);
        VERIFY_LQ(&data[i],  4);
        VERIFY_LQ(&data[i],  5);
        VERIFY_LQ(&data[i],  6);
        VERIFY_LQ(&data[i],  7);
        VERIFY_LQ(&data[i],  8);
        VERIFY_LQ(&data[i],  9);
        VERIFY_LQ(&data[i],  10);
        VERIFY_LQ(&data[i],  11);
        VERIFY_LQ(&data[i],  12);
        VERIFY_LQ(&data[i],  13);
        VERIFY_LQ(&data[i],  14);
        VERIFY_LQ(&data[i],  15);
        VERIFY_LQ(&data[i],  16);
    }

    return 0;
}
