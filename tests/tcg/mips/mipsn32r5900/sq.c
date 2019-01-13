/*
 * Test SQ.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define GUARD_BYTE 0xA9

/* 128-bit multimedia register */
struct mmr { uint64_t hi, lo; } __attribute__((aligned(16)));

static uint8_t data[64];

#define SQ(value, base, offset) \
        __asm__ __volatile__ ( \
            "    pcpyld  %0, %0, %1\n" \
            "    sq %0, %3(%2)\n" \
            : \
            : "r" (value.hi), "r" (value.lo), \
              "r" (base), "i" (offset))

static void verify_sq(struct mmr value, const void *base, int16_t offset)
{
    /*
     * The least significant four bits of the effective address are
     * masked to zero, effectively creating an aligned address. No
     * address exceptions due to alignment are possible.
     */
    const uint8_t *b = base;
    const uint8_t *o = &b[offset];
    const uint8_t *a = (const uint8_t*)((unsigned long)o & ~0xFUL);
    size_t i;

    for (i = 0; i < sizeof(data); i++) {
        ssize_t d = &data[i] - &a[0];

        if (d < 0) {
            assert(data[i] == GUARD_BYTE);
        } else if (d < 8) {
            assert(data[i] == ((value.lo >> (8 * (d - 0))) & 0xFF));
        } else if (d < 16) {
            assert(data[i] == ((value.hi >> (8 * (d - 8))) & 0xFF));
        } else {
            assert(data[i] == GUARD_BYTE);
        }
    }
}

#define VERIFY_SQ(base, offset) \
    do { \
        struct mmr value = { \
            .hi = 0x6665646362613938, \
            .lo = 0x3736353433323130 \
        }; \
    \
        memset(data, GUARD_BYTE, sizeof(data)); \
        SQ(value, base, offset); \
        verify_sq(value, base, offset); \
    } while (0)

int main()
{
    int i;

    for (i = 16; i < 48; i++) {
        VERIFY_SQ(&data[i], -16);
        VERIFY_SQ(&data[i], -15);
        VERIFY_SQ(&data[i], -14);
        VERIFY_SQ(&data[i], -13);
        VERIFY_SQ(&data[i], -12);
        VERIFY_SQ(&data[i], -11);
        VERIFY_SQ(&data[i], -10);
        VERIFY_SQ(&data[i], -9);
        VERIFY_SQ(&data[i], -8);
        VERIFY_SQ(&data[i], -7);
        VERIFY_SQ(&data[i], -6);
        VERIFY_SQ(&data[i], -5);
        VERIFY_SQ(&data[i], -4);
        VERIFY_SQ(&data[i], -3);
        VERIFY_SQ(&data[i], -2);
        VERIFY_SQ(&data[i], -1);
        VERIFY_SQ(&data[i],  0);
        VERIFY_SQ(&data[i],  1);
        VERIFY_SQ(&data[i],  2);
        VERIFY_SQ(&data[i],  3);
        VERIFY_SQ(&data[i],  4);
        VERIFY_SQ(&data[i],  5);
        VERIFY_SQ(&data[i],  6);
        VERIFY_SQ(&data[i],  7);
        VERIFY_SQ(&data[i],  8);
        VERIFY_SQ(&data[i],  9);
        VERIFY_SQ(&data[i],  10);
        VERIFY_SQ(&data[i],  11);
        VERIFY_SQ(&data[i],  12);
        VERIFY_SQ(&data[i],  13);
        VERIFY_SQ(&data[i],  14);
        VERIFY_SQ(&data[i],  15);
        VERIFY_SQ(&data[i],  16);
    }

    return 0;
}
