/*
 * Test that every trap into M-mode writes mstatus.GVA: 1 if mtval holds
 * a guest virtual address, 0 otherwise.
 *
 * Each build is one case, set by -D flags in meson.build:
 * - MODE: VS, VU or HS (HS runs hlv/hsv on the guest's behalf)
 * - ACCESS: the instruction run on the start of region REG
 * - REG: the region accessed
 * - FLAGS: the guest's page-table entry for REG, with A or D clear, so
 *   that the walk's A/D write faults on the read-only G-stage
 * - CAUSE: the fault expected
 *
 * The trap handler checks, and the exit status is the first that fails:
 *   1  mcause is CAUSE
 *   2  GVA is 1, since mtval holds the guest address
 *   3  after an ecall from M-mode, whose trap has no guest address, GVA
 *      is 0
 *
 * `trap` makes checks 1 and 2, then runs the ecall, whose trap goes to
 * `ecall_trap` for check 3.
 *
 * Privileged ISA v1.12: 8.2.1.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hyp-guest.h"

/* exit with status n unless reg == want */
#define CHECK(n, reg, want) li t6, want; li a0, n; bne reg, t6, exit

/* read mstatus.GVA into t5 */
#define MSTATUS_GVA_SHIFT 38
#define GVA csrr t5, mstatus; srli t5, t5, MSTATUS_GVA_SHIFT; andi t5, t5, 1

        .option norvc
        .text
        .global test, trap, hs_trap
test:
        li      t0, VS_L1 + (REG) * 8   /* the guest's entry for REG */
        li      t1, leaf(REGION(REG), FLAGS)
        sd      t1, 0(t0)
        li      a0, G_RO
        call    set_tables_perm

        li      a0, REGION(REG)
        lla     a1, probe
        li      a2, MODE
        call    run_guest

probe:
        ACCESS
        ecall

        .balign 4
trap:
        csrr    t5, mcause
        CHECK(1, t5, CAUSE)
        GVA
        CHECK(2, t5, 1)
        lla     t0, ecall_trap          /* the ecall below traps there */
        csrw    mtvec, t0
        ecall

        .balign 4
ecall_trap:
        GVA
        CHECK(3, t5, 0)
        li      a0, 0
        j       exit

/* nothing is delegated to HS-mode, so no trap should come here */
        .balign 4
hs_trap:
        li      a0, 1
        j       exit
