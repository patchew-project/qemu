/*
 * Test what a fault reports when the G-stage refuses the guest's
 * page-table walk: its read of a page-table entry, or its write of the
 * A/D bits.
 *
 * Each build is one case, set by -D flags in meson.build:
 * - MODE: VS, VU or HS (HS runs hlv/hsv on the guest's behalf)
 * - ACCESS: the instruction run on ADDR
 * - REG: the region accessed; ADDR defaults to its start
 * - FLAGS: the guest's page-table entry for ADDR
 * - PERM: the G-stage permission of the guest's tables (default G_RO)
 * - TO_HS: take the fault in HS-mode instead of M-mode
 * - MENVCFG, HENVCFG: turn ADUE off
 * - PTE_ADDR, PTE_PA: where the entry is and where it points, for the
 *   4 KiB and 1 GiB cases
 * - CAUSE, ENTRY, TINST, TINST_NOT_WALK: what the fault must report
 *
 * The trap handler checks, and the exit status is the first that fails:
 *   1  cause  CAUSE
 *   2  tval   ADDR
 *   3  tval2  ENTRY >> 2, the entry the walk failed on (default: the
 *             guest's entry for ADDR)
 *   4  tinst  TINST: 0x3020 if the walk was writing A/D (default),
 *             0x3000 if reading; with TINST_NOT_WALK, neither
 *   5  the entry is unchanged, so the refused A/D write did not happen
 *
 * `trap` checks a fault taken in M-mode. `hs_trap` checks one taken in
 * HS-mode, using scause, stval, htval and htinst.
 *
 * Privileged ISA v1.12: 8.5.1, 8.6.3 Table 8.12.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hyp-guest.h"

/* exit with status n unless reg == want */
#define CHECK(n, reg, want) li t6, want; li a0, n; bne reg, t6, exit

#ifdef REG
#ifndef ADDR
#define ADDR REGION(REG)
#endif
#ifndef PTE_ADDR
#define PTE_ADDR (VS_L1 + (REG) * 8)
#endif
#ifndef PTE_PA
#define PTE_PA REGION(REG)
#endif
#endif
#ifndef ENTRY
#define ENTRY PTE_ADDR
#endif
#ifndef PERM
#define PERM G_RO
#endif
#ifndef TINST
#define TINST TINST_WRITE
#endif

        .option norvc
        .text
        .global test, trap, hs_trap
test:
#ifdef MENVCFG
        li      t0, MENVCFG             /* A/D updating off */
        csrw    menvcfg, t0
#endif
#ifdef HENVCFG
        li      t0, HENVCFG
        csrw    henvcfg, t0
#endif
#ifdef TO_HS
        li      t0, 1 << CAUSE
        csrw    medeleg, t0
#endif
        li      t0, PTE_ADDR            /* the guest's entry for ADDR */
        li      t1, leaf(PTE_PA, FLAGS)
        sd      t1, 0(t0)
#if REG >= 3
        li      t0, REGION(REG)         /* an ecall for a jump to land on */
        li      t1, 0x00000073
        sw      t1, 0(t0)
#endif
        li      a0, PERM
        call    set_tables_perm

        ld      a0, addr
        lla     a1, probe
        li      a2, MODE
        call    run_guest

probe:
#ifdef ACCESS
        ACCESS
#endif
        ecall

/* the fault, as reported by CSRs cause, tval, tval2 and tinst */
.macro check_fault cause, tval, tval2, tinst
        csrr    t5, \cause
        CHECK(1, t5, CAUSE)
        csrr    t5, \tval
        ld      t6, addr
        li      a0, 2
        bne     t5, t6, exit
        csrr    t5, \tval2
        CHECK(3, t5, (ENTRY) >> 2)
        csrr    t5, \tinst
#ifdef TINST_NOT_WALK
        li      a0, 4
        li      t6, TINST_READ
        beq     t5, t6, exit
        li      t6, TINST_WRITE
        beq     t5, t6, exit
#else
        CHECK(4, t5, TINST)
#endif
        li      t0, PTE_ADDR
        ld      t5, 0(t0)
        CHECK(5, t5, leaf(PTE_PA, FLAGS))
        li      a0, 0
        j       exit
.endm

        .balign 4
trap:
        check_fault mcause, mtval, mtval2, mtinst

        .balign 4
hs_trap:
        check_fault scause, stval, htval, htinst

        .data
        .balign 8
addr:   .dword ADDR
