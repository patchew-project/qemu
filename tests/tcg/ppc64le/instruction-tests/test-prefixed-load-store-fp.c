#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <string.h>
#include <float.h>

bool debug = false;

#define dprintf(...) \
    do { \
        if (debug == true) { \
            fprintf(stderr, "%s: ", __func__); \
            fprintf(stderr, __VA_ARGS__); \
        } \
    } while (0);

bool le;

#define PLFS(_FRT, _RA, _d0, _d1, _R) \
    ".long 1<<26 | 2<<24 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 48<<26 | (" #_FRT ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"
#define PSTFS(_FRS, _RA, _d0, _d1, _R) \
    ".long 1<<26 | 2<<24 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 52<<26 | (" #_FRS ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"
#define PLFD(_FRT, _RA, _d0, _d1, _R) \
    ".long 1<<26 | 2<<24 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 50<<26 | (" #_FRT ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"
#define PSTFD(_FRS, _RA, _d0, _d1, _R) \
    ".long 1<<26 | 2<<24 | (" #_R ")<<20 | (" #_d0 ")\n" \
    ".long 54<<26 | (" #_FRS ")<<21 | (" #_RA ")<<16 | (" #_d1 ")\n"

void test_plfs(void) {
    float dest = 0;
    float dest_copy = 0;
    float src = FLT_MAX;
    void *src_ptr = &src;

    /* sanity check against lfs */
    asm(
        "lfs %0, 0(%2)"
        : "+f" (dest_copy)
        : "f" (src), "r" (src_ptr));

    asm(
        PLFS(%0, %2, 0, 0, 0)
        : "+f" (dest)
        : "f" (src), "r" (src_ptr));

    assert(dest == src);
    assert(dest_copy == dest);
}

void test_pstfs(void) {
    float dest = 0;
    float dest_copy = 0;
    float src = FLT_MAX;
    void *dest_ptr = &dest;
    void *dest_copy_ptr = &dest_copy;

    /* sanity check against stfs */
    asm(
        "stfs %1, 0(%0)"
        : "+r" (dest_copy_ptr)
        : "f" (src));

    asm(
        PSTFS(%1, %0, 0, 0, 0)
        : "+r" (dest_ptr)
        : "f" (src));

    assert(dest == src);
    assert(dest_copy == dest);
}

void test_plfd(void) {
    double dest = 0;
    double dest_copy = 0;
    double src = DBL_MAX;
    void *src_ptr = &src;

    /* sanity check against lfd */
    asm(
        "lfd %0, 0(%2)"
        : "+d" (dest_copy)
        : "d" (src), "r" (src_ptr));

    asm(
        PLFD(%0, %2, 0, 0, 0)
        : "+d" (dest)
        : "d" (src), "r" (src_ptr));

    assert(dest == src);
    assert(dest_copy == dest);
}

void test_pstfd(void) {
    double dest = 0;
    double dest_copy = 0;
    double src = DBL_MAX;
    void *dest_ptr = &dest;
    void *dest_copy_ptr = &dest_copy;

    /* sanity check against stfs */
    asm(
        "stfd %1, 0(%0)"
        : "+r" (dest_copy_ptr)
        : "f" (src));

    asm(
        PSTFD(%1, %0, 0, 0, 0)
        : "+r" (dest_ptr)
        : "f" (src));

    assert(dest == src);
    assert(dest_copy == dest);
}

void test_plfs_offset(void) {
    float dest;
    float src = FLT_MAX;
    void *src_ptr = &src;
    void *src_ptr_offset;

    src_ptr_offset = src_ptr - 1;
    dest = 0;
    asm(
        PLFS(%0, %2, 0, 0x1, 0)
        : "=f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);

    src_ptr_offset = src_ptr - 0x1FFFFFFFF;
    dest = 0;
    asm(
        PLFS(%0, %2, 0x1FFFF, 0xFFFF, 0)
        : "=f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);

    src_ptr_offset = src_ptr + 1;
    dest = 0;
    asm(
        PLFS(%0, %2, 0x3FFFF, 0xFFFF, 0)
        : "=f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);
}

void test_pstfs_offset(void) {
    float dest;
    float src = FLT_MAX;
    void *dest_ptr = &dest;
    void *dest_ptr_offset;

    dest_ptr_offset = dest_ptr - 1;
    dest = 0;
    asm(
        PSTFS(%1, %0, 0x0, 0x1, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);

    dest_ptr_offset = dest_ptr - 0x1FFFFFFFF;
    dest = 0;
    asm(
        PSTFS(%1, %0, 0x1FFFF, 0xFFFF, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);

    dest_ptr_offset = dest_ptr + 1;
    dest = 0;
    asm(
        PSTFS(%1, %0, 0x3FFFF, 0xFFFF, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);
}

void test_plfd_offset(void) {
    double dest;
    double src = DBL_MAX;
    void *src_ptr = &src;
    void *src_ptr_offset;

    src_ptr_offset = src_ptr - 1;
    dest = 0;
    asm(
        PLFD(%0, %2, 0, 0x1, 0)
        : "+f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);

    src_ptr_offset = src_ptr - 0x1FFFFFFFF;
    dest = 0;
    asm(
        PLFD(%0, %2, 0x1FFFF, 0xFFFF, 0)
        : "+f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);

    src_ptr_offset = src_ptr + 1;
    dest = 0;
    asm(
        PLFD(%0, %2, 0x3FFFF, 0xFFFF, 0)
        : "+f" (dest)
        : "f" (src), "r" (src_ptr_offset));
    assert(dest == src);
}

void test_pstfd_offset(void) {
    double dest;
    double src = DBL_MAX;
    void *dest_ptr = &dest;
    void *dest_ptr_offset;

    dest_ptr_offset = dest_ptr - 1;
    dest = 0;
    asm(
        PSTFD(%1, %0, 0x0, 0x1, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);

    dest_ptr_offset = dest_ptr - 0x1FFFFFFFF;
    dest = 0;
    asm(
        PSTFD(%1, %0, 0x1FFFF, 0xFFFF, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);

    dest_ptr_offset = dest_ptr + 1;
    dest = 0;
    asm(
        PSTFD(%1, %0, 0x3FFFF, 0xFFFF, 0)
        : "+r" (dest_ptr_offset)
        : "f" (src));
    assert(dest == src);
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

    do_test(plfs);
    do_test(pstfs);
    do_test(plfd);
    do_test(pstfd);

    do_test(plfs_offset);
    do_test(pstfs_offset);
    do_test(plfd_offset);
    do_test(pstfd_offset);

    dprintf("All tests passed\n");
    return 0;
}
