/*
 *  Header file for test utilities
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef TEST_UTILS_32_H
#define TEST_UTILS_32_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#define PRINT_RESULTS 0

#define DO_MIPS32_r(mnemonic, id, input1, input2, expect)              \
{                                                                      \
    uint32_t output;                                                   \
    uint32_t expect_val = expect;                                      \
    __asm__ volatile (                                                 \
      "li $t1, " #input1 "\n\t"                                        \
      "li $t2, " #input2 "\n\t"                                        \
      #mnemonic " $t0, $t1, $t2\n\t"                                   \
      "sw $t0, 0(%0)\n\t"                                              \
      :                                                                \
      : "r" (&output)                                                  \
      : "t0", "t1", "t2", "memory"                                     \
    );                                                                 \
    check_single_insn_32(id, &pass_count, &fail_count, 1, &expect_val, &output); \
}

#define DO_MIPS32_i(mnemonic, id, imm, input1, expect)                 \
{                                                                      \
    uint32_t output;                                                   \
    uint32_t expect_val = expect;                                      \
    __asm__ volatile (                                                 \
      "li $t1, " #input1 "\n\t"                                        \
      #mnemonic " $t0, $t1, " #imm "\n\t"                              \
      "sw $t0, 0(%0)\n\t"                                              \
      :                                                                \
      : "r" (&output)                                                  \
      : "t0", "t1", "memory"                                           \
    );                                                                 \
    check_single_insn_32(id, &pass_count, &fail_count, 1, &expect_val, &output); \
}

#define DO_MIPS32_r2_s(mnemonic, id, hi, lo, input1, input2, expect_hi, expect_lo) \
{                                                                      \
    uint32_t output[2];                                                \
    uint32_t expect_val[2] = {expect_lo, expect_hi};                   \
    __asm__ volatile (                                                 \
      ".set noreorder \n\t"                                            \
      "li $t0, " #hi "\n\t"                                            \
      "mthi $t0       \n\t"                                            \
      "li $t0, " #lo "\n\t"                                            \
      "mtlo $t0       \n\t"                                            \
      "li $t0, " #input1 "\n\t"                                        \
      "li $t1, " #input2 "\n\t"                                        \
      #mnemonic " $t0, $t1 \n\t"                                       \
      "mfhi $t0       \n\t"                                            \
      "sw $t0, 4(%0)\n\t"                                              \
      "mflo $t0       \n\t"                                            \
      "sw $t0, 0(%0)\n\t"                                              \
      ".set reorder \n\t"                                              \
      :                                                                \
      : "r" (&output)                                                  \
      : "t0", "t1", "hi", "lo", "memory"                              \
    );                                                                 \
    check_single_insn_32(id, &pass_count, &fail_count, 2, expect_val, output); \
}

static inline void check_single_insn_32(int id, int *pass, int *fail, int cnt,
                                        uint32_t *expect, uint32_t *actual)
{
    int i;

    if (memcmp(expect, actual, 4 * cnt) == 0) {
        (*pass)++;
        printf("Case %d pass:\n", id);
        for (i = 0; i < cnt; i++) {
            printf("    [%d]: actual: 0x%08x, expect: 0x%08x\n", i, actual[i], expect[i]);
        }
    } else {
        (*fail)++;
        printf("Case %d fail:\n", id);
        for (i = 0; i < cnt; i++) {
            printf("    [%d]: actual: 0x%08x, expect: 0x%08x\n", i, actual[i], expect[i]);
        }
    }
}

static inline int32_t check_results_32(const char *instruction_name,
                                       const uint32_t test_count,
                                       const double elapsed_time,
                                       const uint32_t *b32_result,
                                       const uint32_t *b32_expect)
{
#if PRINT_RESULTS
    uint32_t ii;
    printf("\n");
    for (ii = 0; ii < test_count; ii++) {
        uint64_t a;
        memcpy(&a, (b32_result + ii), 8);
        if (ii % 8 != 0) {
            printf("        0x%08lxULL,\n", a);
        } else {
            printf("        0x%08lxULL,                   /* %3d  */\n",
                   a, ii);
        }
    }
    printf("\n");
#endif
    uint32_t i;
    uint32_t pass_count = 0;
    uint32_t fail_count = 0;

    printf("%s:   ", instruction_name);
    for (i = 0; i < test_count; i++) {
        if (b32_result[i] == b32_expect[i]) {
            pass_count++;
        } else {
            fail_count++;
        }
    }

    printf("PASS: %3d   FAIL: %3d   elapsed time: %5.2f ms\n",
           pass_count, fail_count, elapsed_time);

    if (fail_count > 0) {
        return -1;
    } else {
        return 0;
    }
}


#endif
