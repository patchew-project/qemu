#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <altivec.h>
#include <endian.h>
#include <string.h>

bool debug = false;

#define dprintf(...) \
    do { \
        if (debug == true) { \
            fprintf(stderr, "%s: ", __func__); \
            fprintf(stderr, __VA_ARGS__); \
        } \
    } while (0);

bool le;

#define PLXVP(_Tp, _RA, _d0, _d1, _R, _TX) \
    ".align 6;" \
    ".long 1 << 26 | (" #_R ") << 20 | (" #_d0 ");" \
    ".long 58 << 26 | (" #_Tp ") << 22 | (" #_TX ") << 21 | (" #_RA ") << 16 | (" #_d1 ");"

void test_plxvp_cia(void) {
    register vector unsigned char v0 asm("vs8") = { 0 };
    register vector unsigned char v1 asm("vs9") = { 0 };
    int i;

    /* load defined bytes below into vs8,vs9 using CIA with relative offset */
    asm(
        PLXVP(4, 0, 0, 8 /* skip plxvp */ + 4 /* skip b */, 1, 0)
        "b 1f;"
        ".byte 0;"
        ".byte 1;"
        ".byte 2;"
        ".byte 3;"
        ".byte 4;"
        ".byte 5;"
        ".byte 6;"
        ".byte 7;"
        ".byte 8;"
        ".byte 9;"
        ".byte 10;"
        ".byte 11;"
        ".byte 12;"
        ".byte 13;"
        ".byte 14;"
        ".byte 15;"
        ".byte 16;"
        ".byte 17;"
        ".byte 18;"
        ".byte 19;"
        ".byte 20;"
        ".byte 21;"
        ".byte 22;"
        ".byte 23;"
        ".byte 24;"
        ".byte 25;"
        ".byte 26;"
        ".byte 27;"
        ".byte 28;"
        ".byte 29;"
        ".byte 30;"
        ".byte 31;"
        "1: nop;"
        : "+wa" (v0), "+wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == 16 + i);
        else
            assert(v0[i] == (31 - i)); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == i);
        else
            assert(v1[i] == 15 - i); // FIXME
    }
}

void test_plxvp(void) {
    register vector unsigned char v0 asm("vs6") = { 0 };
    register vector unsigned char v1 asm("vs7") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into vs6,vs7 using EA with no offset */
    asm(PLXVP(3, %2, 0, 0, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(v0[i] == buf[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16 + i]); // FIXME
    }

    /* load buf[32:63] into vs6,vs7 using EA with d1 offset */
    buf_ptr = buf_ptr + 32 - 0x1000;
    asm(PLXVP(3, %2, 0, 0x1000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32 + 16 + i]);
        else
            assert(v0[i] == buf[32 + i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32 + i]);
        else
            assert(v1[i] == buf[48 + i]); // FIXME
    }

    /* load buf[0:31] into vs6,vs7 using EA with d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr - ((0x1000 << 16) | 0x1000);
    asm(PLXVP(3, %2, 0x1000, 0x1000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(v0[i] == buf[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16 + i]); // FIXME
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
    /* load buf[32:63] into vs6,vs7 using EA with negative d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr + 32 + 0x1000;
    asm(PLXVP(3, %2, 0x3ffff, 0xf000, 0, 0)
        : "+wa" (v0), "+wa" (v1)
        : "r" (buf_ptr));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32 + 16 + i]);
        else
            assert(v0[i] == buf[32 + i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32 + i]);
        else
            assert(v1[i] == buf[48 + i]); // FIXME
    }
}

#define PSTXVP(_Sp, _RA, _d0, _d1, _R, _SX) \
    ".align 6;" \
    ".long 1 << 26 | (" #_R ") << 20 | (" #_d0 ");" \
    ".long 62 << 26 | (" #_Sp ") << 22 | (" #_SX ") << 21 | (" #_RA ") << 16 | (" #_d1 ");"

void test_pstxvp(void) {
    register vector unsigned char v0 asm("vs6") = {
// FIXME: reorder values for readability
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs7") = {
// FIXME: reorder values for readability
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 0;
    }

    /* store vs6,vs7 into buf[0:31] using EA with no offset */
    asm(PSTXVP(3, %0, 0, 0, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(v0[i] == buf[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16 + i]); // FIXME
    }

    /* store vs6,vs7 into buf[32:63] using EA with d1 offset */
    buf_ptr = buf_ptr + 32 - 0x1000;
    asm(PSTXVP(3, %0, 0, 0x1000, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32 + 16 + i]);
        else
            assert(v0[i] == buf[32 + i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32 + i]);
        else
            assert(v1[i] == buf[48 + i]); // FIXME
    }

    /* store buf[0:31] into vs6,vs7 using EA with d0||d1 offset */
    buf_ptr = buf;
    buf_ptr = buf_ptr - ((0x1000 << 16) | 0x1000);
    asm(PSTXVP(3, %0, 0x1000, 0x1000, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1));

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(v0[i] == buf[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16 + i]); // FIXME
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}

/* TODO: we force 2 instead of 1 in opc2 currently to hack around
 * QEMU impl, need a single handler to deal with the 1 in bit 31
 */
#define STXVP(_Sp, _RA, _DQ, _SX) \
    ".align 6;" \
    ".long 6 << 26 | (" #_Sp ") << 22 | (" #_SX ") << 21 | (" #_RA ") << 16 | (" #_DQ ") << 4 | 1;"

void test_stxvp(void) {
    register vector unsigned char v0 asm("vs4") = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs5") = {
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 7;
    }

    /* store v0,v1 into buf[0:31] using EA with no offset */
    asm(STXVP(2, %0, 0, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[i] == v1[i]);
        else
            assert(buf[i] == v0[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[16 + i] == v0[i]);
        else
            assert(buf[16 + i] == v1[i]); // FIXME
    }

    /* store v0,v1 into buf[32:63] using EA with offset 0x40 */
    buf_ptr = buf_ptr + 32 - 0x40;
    asm(STXVP(2, %0, 4, 0)
        : "+r" (buf_ptr)
        : "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[32 + i] == v1[i]);
        else
            assert(buf[32 + i] == v0[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[32 + 16 + i] == v0[i]);
        else
            assert(buf[48 + i] == v1[i]); // FIXME
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}

#define LXVP(_Tp, _RA, _DQ, _TX) \
    ".long 6 << 26 | (" #_Tp ") << 22 | (" #_TX ") << 21 | (" #_RA ") << 16 | (" #_DQ ") << 4;"

void test_lxvp(void) {
    register vector unsigned char v0 asm("vs4") = { 0 };
    register vector unsigned char v1 asm("vs5") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into v0,v1 using EA with no offset */
    asm(LXVP(2, %2, 0, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr)
        );

    for (i = 0; i < 16; i++) {
        if (le) {
            assert(v0[i] == buf[16 + i]);

         } else
            assert(v0[i] == buf[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16+i]); // FIXME
    }

    /* load buf[32:63] into v0,v1 using EA with 0x40 offset */
    buf_ptr = buf_ptr + 32 - 0x40;
    asm(LXVP(2, %2, 4, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32+16+i]);
        else
            assert(v0[i] == buf[32+i]); // FIXME

    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32+i]);
        else
            assert(v1[i] == buf[48+i]); // FIXME
    }

    /* TODO: signed offsets */
    /* TODO: PC-relative addresses */
}

#define LXVPX(_Tp, _RA, _RB, _TX) \
    ".long 31 << 26 | (" #_Tp ") << 22 | (" #_TX ") << 21 | (" #_RA ") << 16 | (" #_RB ") << 11 | 333 << 1;"

void test_lxvpx(void) {
    register vector unsigned char v0 asm("vs8") = { 0 };
    register vector unsigned char v1 asm("vs9") = { 0 };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    uint32_t offset;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = i;
    }

    /* load buf[0:31] into v0,v1 using EA with no offset */
    offset = 0;
    asm(LXVPX(4, %2, %3, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr), "r" (offset)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(v0[i] == buf[i]); // FIXME

    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[i]);
        else
            assert(v1[i] == buf[16+i]); // FIXME
    }

    /* load buf[32:63] into v0,v1 using EA with 0x40 offset */
    offset = 0x40;
    buf_ptr = buf_ptr + 32 - offset;
    asm(LXVPX(4, %2, %3, 0)
        : "=wa" (v0), "=wa" (v1)
        : "r" (buf_ptr), "r" (offset)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32 + 16 + i]);
        else
            assert(v0[i] == buf[32+i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32 + i]);
        else
            assert(v1[i] == buf[48+i]); // FIXME
    }

    /* TODO: signed offsets */
    /* TODO: PC-relative addresses */
}

#define STXVPX(_Sp, _RA, _RB, _SX) \
    ".long 31 << 26 | (" #_Sp ") << 22 | (" #_SX ") << 21 | (" #_RA ") << 16 | (" #_RB ") << 11 | 461 << 1;"

void test_stxvpx(void) {
    register vector unsigned char v0 asm("vs10") = {
// FIXME: reorder for readability
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15
    };
    register vector unsigned char v1 asm("vs11") = {
// FIXME: ditto
        16, 17, 18, 19,
        20, 21, 22, 23,
        24, 25, 26, 27,
        28, 29, 30, 31
    };
    uint8_t buf[64] __attribute__((aligned(16)));
    uint8_t *buf_ptr = (uint8_t *)&buf;
    uint32_t offset;
    int i;

    for (i = 0; i < 64; i++) {
        buf[i] = 7;
    }

    /* store v0,v1 into buf[0:31] using EA with no offset */
    offset = 0;
    asm(STXVPX(5, %0, %1, 0)
        : "+r" (buf_ptr)
        : "r" (offset), "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(buf[i] == v1[i]);
        else
            assert(buf[i] == v0[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[16 + i]);
        else
            assert(buf[16 + i] == v1[i]); // FIXME
    }

    /* store v0,v1 into buf[32:63] using EA with offset 0x40 */
    offset = 0x40;
    buf_ptr = buf_ptr + 32 - offset;
    asm(STXVPX(5, %0, %1, 0)
        : "+r" (buf_ptr)
        : "r" (offset), "wa" (v0), "wa" (v1)
        );

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v0[i] == buf[32 + 16 + i]);
        else
            assert(buf[48 + i] == v1[i]); // FIXME
    }

    for (i = 0; i < 16; i++) {
        if (le)
            assert(v1[i] == buf[32 + i]);
        else
            assert(buf[32 + i] == v0[i]); // FIXME
    }

    /* TODO: test signed offset */
    /* TODO: PC-relative addresses */
}

#define do_test(testname) \
    if (debug) \
        fprintf(stderr, "-> running test: " #testname "\n"); \
    test_##testname(); \

int main(int argc, char **argv)
{
    le = (htole16(1) == 1);

    if (argc > 1 && !strcmp(argv[1], "-d")) {
        debug = true;
    }

    do_test(lxvp);
    do_test(stxvp);
    do_test(plxvp);
    do_test(plxvp_cia);
    do_test(pstxvp);
    do_test(lxvpx);
    do_test(stxvpx);

    dprintf("All tests passed\n");
    return 0;
}
