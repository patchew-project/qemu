/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/*
 *  Make sure that two stores in the same packet honor proper
 *  semantics: slot 1 executes first, then slot 0.
 *  This is important when the addresses overlap.
 */
static inline void dual_stores(int32_t *p, int8_t *q, int32_t x, int8_t y)
{
  asm volatile("{\n\t"
               "    memw(%0) = %2\n\t"
               "    memb(%1) = %3\n\t"
               "}\n"
               :: "r"(p), "r"(q), "r"(x), "r"(y)
               : "memory");
}

typedef union {
    int32_t word;
    int8_t byte;
} Dual;

int main()
{
    Dual d;

    d.word = ~0;
    dual_stores(&d.word, &d.byte, 0x12345678, 0xff);
    check32(d.word, 0x123456ff);

    puts(err ? "FAIL" : "PASS");
    return err;
}
