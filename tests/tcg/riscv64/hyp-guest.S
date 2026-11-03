/*
 * Routines for the riscv64 hypervisor-extension system tests targeting
 * page-table translations between the host and guest.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hyp-guest.h"

#define FLUSH fence rw, rw; sfence.vma; hfence.vvma; hfence.gvma; fence.i

        .option norvc
        .text
        .global _start, set_tables_perm, run_guest, exit
_start:
        lla     sp, stack_top
        lla     t0, trap
        csrw    mtvec, t0
        lla     t0, hs_trap
        csrw    stvec, t0
        li      t0, -1                  /* no PMP restrictions */
        csrw    pmpaddr0, t0
        li      t0, 0x1f
        csrw    pmpcfg0, t0
        li      t0, ADUE                /* hardware A/D updating */
        csrw    menvcfg, t0
        csrw    henvcfg, t0
        li      t0, 0x100               /* hstatus.SPVP: hlv/hsv act as VS */
        csrs    hstatus, t0

        /* G-stage: regions 0-24, 1:1, all accessible */
        li      t0, G_ROOT + 2 * 8
        li      t1, table(G_L1)
        sd      t1, 0(t0)
        li      t0, G_L1
        li      t1, leaf(REGION(0), G_RW)
        li      t2, 0x200000 >> 2
        li      t3, 25
1:      sd      t1, 0(t0)
        add     t1, t1, t2
        addi    t0, t0, 8
        addi    t3, t3, -1
        bnez    t3, 1b

        /* guest: the code, the code again for VU, the 4 KiB table */
        li      t0, VS_ROOT + 2 * 8
        li      t1, table(VS_L1)
        sd      t1, 0(t0)
        li      t0, VS_L1
        li      t1, leaf(REGION(0), G_RW & ~PTE_U)
        sd      t1, 0(t0)
        li      t1, leaf(REGION(0), G_RO & ~PTE_W)
        sd      t1, 24 * 8(t0)
        li      t1, table(VS_L0)
        sd      t1, 8 * 8(t0)

        li      t0, (8 << 60) | (G_ROOT >> 12)
        csrw    hgatp, t0
        li      t0, (8 << 60) | (VS_ROOT >> 12)
        csrw    vsatp, t0
        FLUSH

        call    test
        li      a0, 1                   /* test returned: no trap */

/* exit through semihosting with status a0 */
exit:
        lla     a1, semiargs
        li      t0, 0x20026             /* ADP_Stopped_ApplicationExit */
        sd      t0, 0(a1)
        sd      a0, 8(a1)
        li      a0, 0x20                /* SYS_EXIT_EXTENDED */
        .balign 16
        slli    zero, zero, 0x1f
        ebreak
        srai    zero, zero, 0x7
1:      j       1b

set_tables_perm:
        li      t0, leaf(REGION(2), 0)
        or      t1, a0, t0
        li      t0, G_L1 + 2 * 8
        sd      t1, 0(t0)
        FLUSH
        ret

/* mret into the mode: mstatus.MPV = 1 for a guest, MPP = S or U */
run_guest:
        li      t0, (3 << 11) | (1 << 39)
        csrc    mstatus, t0
        li      t0, HS
        beq     a2, t0, 2f
        li      t0, 1 << 39             /* MPV: a guest mode */
        csrs    mstatus, t0
        li      t0, VU
        bne     a2, t0, 2f
        li      t0, REGION(24) - REGION(0)      /* VU: the user copy */
        add     a1, a1, t0
        j       3f
2:      li      t0, 1 << 11             /* MPP = S */
        csrs    mstatus, t0
3:      csrw    mepc, a1
        mret

        .data
        .balign 8
semiargs:       .dword 0, 0
        .bss
        .balign 16
        .space  4096
stack_top:
