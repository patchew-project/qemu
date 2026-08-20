/*
 * RISC-V vector load/store test, including accesses that cross a page
 * boundary. Intrinsics version of test-rvv-ldst.S.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <riscv_vector.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAGE_SIZE 65536
#define NUM_BYTES 16

struct cross_page {
    uint8_t pad[MAX_PAGE_SIZE - NUM_BYTES / 2];
    uint8_t data[NUM_BYTES];
};

/* Page aligned so the within-page cases never cross a boundary */
static const uint8_t src_data[NUM_BYTES]
    __attribute__((aligned(MAX_PAGE_SIZE))) = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static const struct cross_page src_cross
    __attribute__((aligned(MAX_PAGE_SIZE))) = {
    .data = {
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    },
};

static uint8_t dst_data[NUM_BYTES] __attribute__((aligned(MAX_PAGE_SIZE)));
static struct cross_page dst_cross __attribute__((aligned(MAX_PAGE_SIZE)));

static void vec_copy(uint8_t *dst, const uint8_t *src, int code)
{
    size_t vl = __riscv_vsetvl_e8m1(NUM_BYTES);
    if (vl != NUM_BYTES) {
        exit(code);
    }

    vuint8m1_t v = __riscv_vle8_v_u8m1(src, vl);
    __riscv_vse8_v_u8m1(dst, v, vl);

    if (memcmp(dst, src, NUM_BYTES)) {
        exit(code);
    }
}

int main(void)
{
    /* Case 1: load/store within a page */
    vec_copy(dst_data, src_data, 1);

    /* Case 2: the load straddles a page boundary */
    vec_copy(dst_data, src_cross.data, 2);

    /* Case 3: the store straddles a page boundary */
    vec_copy(dst_cross.data, src_data, 3);

    return 0;
}
