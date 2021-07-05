/*
 *  Copyright(c) 2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int err;

static void __check(int line, uint64_t result, uint64_t expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%016llx != 0x%016llx\n",
               line, result, expect);
        err++;
    }
}

#define check(RES, EXP) __check(__LINE__, RES, EXP)

#define MAX_VEC_SIZE_BYTES         128

typedef union {
    uint64_t ud[MAX_VEC_SIZE_BYTES / 8];
    int64_t   d[MAX_VEC_SIZE_BYTES / 8];
    uint32_t uw[MAX_VEC_SIZE_BYTES / 4];
    int32_t   w[MAX_VEC_SIZE_BYTES / 4];
    uint16_t uh[MAX_VEC_SIZE_BYTES / 2];
    int16_t   h[MAX_VEC_SIZE_BYTES / 2];
    uint8_t  ub[MAX_VEC_SIZE_BYTES / 1];
    int8_t    b[MAX_VEC_SIZE_BYTES / 1];
} MMVector;

#define BUFSIZE      16
#define OUTSIZE      16
#define MASKMOD      3

MMVector buffer0[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector buffer1[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector mask[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector output[OUTSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector expect[OUTSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));

#define CHECK_OUTPUT_FUNC(FIELD, FIELDSZ) \
static void check_output_##FIELD(int line, size_t num_vectors) \
{ \
    for (int i = 0; i < num_vectors; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            __check(line, output[i].FIELD[j], expect[i].FIELD[j]); \
        } \
    } \
}

CHECK_OUTPUT_FUNC(d,  8)
CHECK_OUTPUT_FUNC(w,  4)
CHECK_OUTPUT_FUNC(h,  2)
CHECK_OUTPUT_FUNC(b,  1)

static void init_buffers(void)
{
    int counter0 = 0;
    int counter1 = 17;
    for (int i = 0; i < BUFSIZE; i++) {
        for (int j = 0; j < MAX_VEC_SIZE_BYTES; j++) {
            buffer0[i].b[j] = counter0++;
            buffer1[i].b[j] = counter1++;
        }
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            mask[i].w[j] = (i + j % MASKMOD == 0) ? 0 : 1;
        }
    }
}

static void test_load_tmp(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    for (int i = 0; i < BUFSIZE; i++) {
        /*
         * Load into v2 as .tmp, then ues it in the next packet
         * Should get the new value within the same packet and
         * the old value in the next packet
         */
        asm("v3 = vmem(%0 + #0)\n\t"
            "r1 = #1\n\t"
            "v2 = vsplat(r1)\n\t"
            "{\n\t"
            "    v2.tmp = vmem(%1 + #0)\n\t"
            "    v4.w = vadd(v2.w, v3.w)\n\t"
            "}\n\t"
            "v4.w = vadd(v4.w, v2.w)\n\t"
            "vmem(%2 + #0) = v4\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "r1", "v2", "v3", "v4", "v6", "memory");
        p0 += sizeof(MMVector);
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = buffer0[i].w[j] + buffer1[i].w[j] + 1;
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_load_cur(void)
{
    void *p0 = buffer0;
    void *pout = output;

    for (int i = 0; i < BUFSIZE; i++) {
        asm("{\n\t"
            "    v2.cur = vmem(%0 + #0)\n\t"
            "    vmem(%1 + #0) = v2\n\t"
            "}\n\t"
            : : "r"(p0), "r"(pout) : "v2", "memory");
        p0 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].uw[j] = buffer0[i].uw[j];
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_load_aligned(void)
{
    /* Aligned loads ignore the low bits of the address */
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 13;

    p0 += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    expect[0] = buffer0[0];

    check_output_w(__LINE__, 1);
}

static void test_load_unaligned(void)
{
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 12;

    p0 += offset;    /* Create an unaligned address */
    asm("v2 = vmemu(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    memcpy(expect, &buffer0[0].ub[offset], sizeof(MMVector));

    check_output_w(__LINE__, 1);
}

static void test_store_aligned(void)
{
    /* Aligned stores ignore the low bits of the address */
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 13;

    pout += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    expect[0] = buffer0[0];

    check_output_w(__LINE__, 1);
}

static void test_store_unaligned(void)
{
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 12;

    pout += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmemu(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    memcpy(expect, buffer0, 2 * sizeof(MMVector));
    memcpy(&expect[0].ub[offset], buffer0, sizeof(MMVector));

    check_output_w(__LINE__, 2);
}

static void test_masked_store(void)
{
    void *p0 = buffer0;
    void *pmask = mask;
    void *pout = output;

    memset(expect, 0xff, sizeof(expect));
    memset(output, 0xff, sizeof(expect));

    for (int i = 0; i < BUFSIZE; i++) {
        asm("r4 = #0\n\t"
            "v4 = vsplat(r4)\n\t"
            "v5 = vmem(%0 + #0)\n\t"
            "q0 = vcmp.eq(v4.w, v5.w)\n\t"
            "v5 = vmem(%1)\n\t"
            "if (q0) vmem(%2) = v5\n\t"
            : : "r"(pmask), "r"(p0), "r"(pout)
            : "r4", "v4", "v5", "q0", "memory");
        p0 += sizeof(MMVector);
        pmask += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            if (i + j % MASKMOD == 0) {
                expect[i].w[j] = buffer0[i].w[j];
            }
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

#define OP1(ASM, EL, IN, OUT) \
    asm("v2 = vmem(%0 + #0)\n\t" \
        "v2" #EL " = " #ASM "(v2" #EL ")\n\t" \
        "vmem(%1 + #0) = v2\n\t" \
        : : "r"(IN), "r"(OUT) : "v2", "memory")

#define OP2(ASM, EL, IN0, IN1, OUT) \
    asm("v2 = vmem(%0 + #0)\n\t" \
        "v3 = vmem(%1 + #0)\n\t" \
        "v2" #EL " = " #ASM "(v2" #EL ", v3" #EL ")\n\t" \
        "vmem(%2 + #0) = v2\n\t" \
        : : "r"(IN0), "r"(IN1), "r"(OUT) : "v2", "v3", "memory")

#define TEST_OP1(NAME, ASM, EL, FIELD, FIELDSZ, OP) \
static void test_##NAME(void) \
{ \
    void *pin = buffer0; \
    void *pout = output; \
    for (int i = 0; i < BUFSIZE; i++) { \
        OP1(ASM, EL, pin, pout); \
        pin += sizeof(MMVector); \
        pout += sizeof(MMVector); \
    } \
    for (int i = 0; i < BUFSIZE; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            expect[i].FIELD[j] = OP buffer0[i].FIELD[j]; \
        } \
    } \
    check_output_##FIELD(__LINE__, BUFSIZE); \
}

#define TEST_OP2(NAME, ASM, EL, FIELD, FIELDSZ, OP) \
static void test_##NAME(void) \
{ \
    void *p0 = buffer0; \
    void *p1 = buffer1; \
    void *pout = output; \
    for (int i = 0; i < BUFSIZE; i++) { \
        OP2(ASM, EL, p0, p1, pout); \
        p0 += sizeof(MMVector); \
        p1 += sizeof(MMVector); \
        pout += sizeof(MMVector); \
    } \
    for (int i = 0; i < BUFSIZE; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            expect[i].FIELD[j] = buffer0[i].FIELD[j] OP buffer1[i].FIELD[j]; \
        } \
    } \
    check_output_##FIELD(__LINE__, BUFSIZE); \
}

TEST_OP2(vadd_w, vadd, .w, w, 4, +)
TEST_OP2(vadd_h, vadd, .h, h, 2, +)
TEST_OP2(vadd_b, vadd, .b, b, 1, +)
TEST_OP2(vsub_w, vsub, .w, w, 4, -)
TEST_OP2(vsub_h, vsub, .h, h, 2, -)
TEST_OP2(vsub_b, vsub, .b, b, 1, -)
TEST_OP2(vxor, vxor, , d, 8, ^)
TEST_OP2(vand, vand, , d, 8, &)
TEST_OP2(vor, vor, , d, 8, |)
TEST_OP1(vnot, vnot, , d, 8, ~)

int main()
{
    init_buffers();

    test_load_tmp();
    test_load_cur();
    test_load_aligned();
    test_load_unaligned();
    test_store_aligned();
    test_store_unaligned();
    test_masked_store();

    test_vadd_w();
    test_vadd_h();
    test_vadd_b();
    test_vsub_w();
    test_vsub_h();
    test_vsub_b();
    test_vxor();
    test_vand();
    test_vor();
    test_vnot();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
