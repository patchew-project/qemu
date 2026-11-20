/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test conditional calls, compound testbit0-and-jump instructions,
 * and endloop01 (dual hardware loop end).
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/* Target function for conditional calls */
static int call_target_val;

__attribute__((noinline))
static void call_target(void)
{
    call_target_val = 42;
}

/*
 * Test conditional call: if (Pu) callr Rs / if (!Pu) callr Rs
 */
static void check_cond_call(void)
{
    void (*fn)(void) = call_target;

    /*
     * callr clobbers all caller-saved registers.
     * Hexagon caller-saved: r0-r5, r14-r15, r28, p0-p3.
     */

    /* if (p0) callr Rs -- predicate true */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[one], %[one])\n\t"  /* p0 = true */
                 "if (p0) callr %[fn]\n\t"
                 :
                 : [one] "r"(1), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 42);

    /* if (!p0) callr Rs -- predicate true, so !p0 is false */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[one], %[one])\n\t"  /* p0 = true */
                 "if (!p0) callr %[fn]\n\t"
                 :
                 : [one] "r"(1), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 0);

    /* if (!p0) callr Rs -- predicate false, so !p0 is true */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[a], %[b])\n\t"  /* p0 = false (1 != 2) */
                 "if (!p0) callr %[fn]\n\t"
                 :
                 : [a] "r"(1), [b] "r"(2), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 42);
}

/*
 * Test compound testbit0-and-jump.
 * p0 = tstbit(Rs, #0); if ([!]p0.new) jump:t/nt target
 */
static void check_tstbit0_jump(void)
{
    uint32_t result;

    /* Rs has bit0 = 1, test "if (p0.new) jump" -> should jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x5)  /* bit 0 is set */
        : "p0");
    check32(result, 1);

    /* Rs has bit0 = 0, test "if (p0.new) jump" -> should NOT jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x4)  /* bit 0 is clear */
        : "p0");
    check32(result, 0);

    /* Rs has bit0 = 0, test "if (!p0.new) jump" -> should jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (!p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x4)  /* bit 0 is clear */
        : "p0");
    check32(result, 1);
}

/*
 * Test endloop01: a packet that terminates both loop0 and loop1.
 * Outer loop runs 3 iterations, inner loop runs 4.
 */
static void check_endloop01(void)
{
    uint32_t count;

    asm volatile(
        "%[cnt] = #0\n\t"
        "loop1(1f, #3)\n\t"    /* outer loop: 3 iterations */
        "1:\n\t"
        "loop0(2f, #4)\n\t"    /* inner loop: 4 iterations */
        "2:\n\t"
        "{ %[cnt] = add(%[cnt], #1) }:endloop01\n\t"
        : [cnt] "=r"(count));

    /* 3 outer * 4 inner = 12 total iterations */
    check32(count, 12);
}

/* The first taken branch wins, but a parallel store still commits. */
static void check_dual_jump(void)
{
    for (int a = 0; a < 2; a++) {
        for (int b = 0; b < 2; b++) {
            uint32_t path, stored = 0;

            asm volatile("p0 = cmp.eq(%[a], #1)\n\t"
                         "p1 = cmp.eq(%[b], #1)\n\t"
                         "{\n\t"
                         "    if (p0) jump:t 1f\n\t"
                         "    if (p1) jump:nt 2f\n\t"
                         "    memw(%[stored]) = #7\n\t"
                         "}\n\t"
                         "{ %[path] = #0; jump 3f }\n\t"
                         "1: { %[path] = #1; jump 3f }\n\t"
                         "2: { %[path] = #2 }\n\t"
                         "3:\n\t"
                         : [path] "=r"(path)
                         : [a] "r"(a), [b] "r"(b), [stored] "r"(&stored)
                         : "p0", "p1", "memory");
            check32(path, a ? 1 : b ? 2 : 0);
            check32(stored, 7);
        }
    }
}

/* callr r31 reads the old LR before installing the return address in LR. */
static void check_callr_lr(void)
{
    uint32_t path, delta;

    asm volatile("r31 = ##1f\n\t"
                 "{ callr r31; r6 = ##2f }\n\t"
                 "2: { %[path] = #0; %[delta] = #1; jump 3f }\n\t"
                 "1: { %[path] = #1; %[delta] = sub(r31, r6) }\n\t"
                 "3:\n\t"
                 : [path] "=r"(path), [delta] "=r"(delta)
                 :
                 : "r6", "r31");
    check32(path, 1);
    check32(delta, 0);
}

/* An explicit target-register write also commits after the branch reads it. */
static void check_jumpr_old_target(void)
{
    uint32_t path;

    asm volatile("{ r5 = ##1f; r6 = ##2f }\n\t"
                 "{ r5 = r6; jumpr r5 }\n\t"
                 "{ %[path] = #0; jump 3f }\n\t"
                 "1: { %[path] = #1; jump 3f }\n\t"
                 "2: { %[path] = #2 }\n\t"
                 "3:\n\t"
                 : [path] "=r"(path)
                 :
                 : "r5", "r6");
    check32(path, 1);
}

/* Short software-pipelined loops exercise the LPCFG/P3 transition boundary. */
static void check_sp_loop(void)
{
    static const unsigned counts[] = { 1, 3, 4 };

    for (int i = 0; i < ARRAY_SIZE(counts); i++) {
        uint32_t iterations, active, pred, usr;
        unsigned count = counts[i];

        asm volatile("%[iterations] = #0\n\t"
                     "%[active] = #0\n\t"
                     "p3 = sp3loop0(1f, %[count])\n\t"
                     "1: {\n\t"
                     "    if (p3) %[active] = add(%[active], #1)\n\t"
                     "    %[iterations] = add(%[iterations], #1)\n\t"
                     "}:endloop0\n\t"
                     "%[pred] = p3\n\t"
                     "%[usr] = usr\n\t"
                     : [iterations] "=&r"(iterations), [active] "=&r"(active),
                       [pred] "=r"(pred), [usr] "=r"(usr)
                     : [count] "r"(count)
                     : "p3", "sa0", "lc0", "usr");
        check32(iterations, count);
        check32(active, count > 3 ? count - 3 : 0);
        check32(pred, count >= 3 ? 0xff : 0);
        check32((usr >> 8) & 3, count < 3 ? 3 - count : 0);
    }
}

int main(void)
{
    check_cond_call();
    check_tstbit0_jump();
    check_endloop01();
    check_sp_loop();

    check_dual_jump();
    check_callr_lr();
    check_jumpr_old_target();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
