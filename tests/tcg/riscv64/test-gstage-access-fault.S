/*
 * Distinguish physical access faults from G-stage page faults.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Nested-PTE-read test flow (virt with the H extension):
 *
 * 1. Set up the test environment.
 *    Install an M-mode trap handler and disable delegation so every
 *    exception is observed in M-mode. The handler records mcause in s1
 *    and returns to the per-case continuation in s2. A failed CHECK
 *    exits through semihosting with a unique code; success exits zero.
 *
 * 2. Create working VS-stage and G-stage mappings.
 *    Build Sv39 VS tables mapping guest virtual address 0x4000 to guest
 *    physical address 0x8000. Build Sv39x4 G-stage tables mapping the VS
 *    page-table pages and final data page into RAM. Pre-set leaf A/D
 *    bits so these read cases do not enter A/D write-back handling.
 *    First verify that a normal load from 0x4000 succeeds.
 *
 * 3. Check PMP failures during nested PTE reads.
 *    TRY_DATA uses MPRV/MPV for guest loads and stores. TRY_FETCH uses
 *    mret to enter VS-mode and fetch from the same virtual address.
 *    Deny reads of the G-stage root page, G_ROOT, with PMP. A load,
 *    store and fetch must report access-fault causes 5, 7 and 1,
 *    respectively. CHECK also requires mtval == 0x4000 and mtval2 == 0.
 *    As a control, deny reads of the translated VS leaf PTE page and
 *    require the guest load to report the same load access fault.
 *
 * 4. Check genuine G-stage translation failures.
 *    Restore PMP access, then invalidate the G-stage mapping of the VS
 *    root page. Require guest-page-fault causes 21, 23 and 20 for load,
 *    store and fetch. Restore the mapping afterward. WRITE_PTE issues
 *    HFENCE.VVMA and HFENCE.GVMA so stale translations cannot hide the
 *    page-table changes.
 *
 * 5. Check PMA failures during nested PTE reads.
 *    Point hgatp at the unmapped physical root address 0x90000000 and
 *    repeat the load, store and fetch checks. The recursive walk must
 *    return TRANSLATE_PMA_FAIL for the inaccessible physical PTE read,
 *    rather than a normal G-stage mapping failure. Require the original
 *    access-fault causes and the same mtval/mtval2 checks as in step 3.
 *
 * 6. Verify recovery.
 *    Restore valid VS-stage and G-stage translation state, then confirm
 *    that a normal load succeeds again before exiting with zero.
 *    Keep the shared helpers and failure-code layout suitable for later
 *    A/D-update and direct G-stage test extensions.
 */

        .option norvc
        .equ MPRV, (1 << 17)
        .equ MPV, (1 << 39)
        .equ MPP_S, (1 << 11)
        .equ MPP_M, (3 << 11)
        .equ VS_ROOT, 0x80400000
        .equ VS_L1, 0x80401000
        .equ VS_L0, 0x80402000
        .equ G_ROOT, 0x80404000
        .equ G_L1, 0x80408000
        .equ G_L0, 0x80409000
        .equ DATA, 0x80500000

        .macro PMP_ALL
        csrw pmpcfg0, zero
        li t0, -1
        csrw pmpaddr0, t0
        li t0, 0x1f
        csrw pmpcfg0, t0
        .endm

        /* A higher-priority 4 KiB region, with a catch-all RWX region. */
        .macro PMP_PAGE address, permissions
        csrw pmpcfg0, zero
        li t0, ((\address >> 2) | 0x1ff)
        csrw pmpaddr0, t0
        li t0, -1
        csrw pmpaddr1, t0
        li t0, (0x1f00 | 0x18 | \permissions)
        csrw pmpcfg0, t0
        .endm

        .macro WRITE_PTE address, value
        li t0, \address
        li t1, \value
        sd t1, 0(t0)
        /* HFENCE.VVMA and HFENCE.GVMA for VS- and G-stage changes. */
        .insn r 0x73, 0, 0x11, x0, x0, x0
        .insn r 0x73, 0, 0x31, x0, x0, x0
        .endm

        /* The original faulting address is always GVA 0x4000. */
        .macro CHECK cause, code
        li a0, \code
        li t0, \cause
        bne s1, t0, fail
        .if \cause != 0
        csrr t0, mtval
        li t1, 0x4000
        bne t0, t1, fail
        .endif
        .if (\cause == 1) || (\cause == 5) || (\cause == 7)
        csrr t0, mtval2
        bnez t0, fail
        .endif
        .endm

        .macro TRY_DATA store, cause, code
        li s1, 0
        lla s2, .Lresume\@
        li a3, 0x4000
        li t0, MPP_M
        csrc mstatus, t0
        li t0, MPP_S | MPRV | MPV
        csrs mstatus, t0
        .if \store
        sw zero, 0(a3)
        .else
        lw t4, 0(a3)
        .endif
.Lresume\@:
        li t0, MPRV | MPV
        csrc mstatus, t0
        CHECK \cause, \code
        .endm

        .macro TRY_FETCH cause, code
        li s1, 0
        lla s2, .Lresume\@
        li t0, MPP_M | MPRV | MPV
        csrc mstatus, t0
        li t0, MPP_S | MPV
        csrs mstatus, t0
        li t0, 0x4000
        csrw mepc, t0
        mret
.Lresume\@:
        CHECK \cause, \code
        .endm

        .text
        .global _start
_start:
        lla t0, trap
        csrw mtvec, t0
        csrw medeleg, zero
        PMP_ALL

        /* VS Sv39 maps VA 0x4000 to GPA 0x8000, using guest PPNs 4/5/6. */
        WRITE_PTE VS_ROOT, 0x1401
        WRITE_PTE VS_L1, 0x1801
        WRITE_PTE (VS_L0 + 0x20), 0x20cf

        /* Sv39x4 maps the three VS page tables and the final data page. */
        WRITE_PTE G_ROOT, (((G_L1 >> 12) << 10) | 1)
        WRITE_PTE G_L1, (((G_L0 >> 12) << 10) | 1)
        WRITE_PTE (G_L0 + 0x20), (((VS_ROOT >> 12) << 10) | 0xdf)
        WRITE_PTE (G_L0 + 0x28), (((VS_L1 >> 12) << 10) | 0xdf)
        WRITE_PTE (G_L0 + 0x30), (((VS_L0 >> 12) << 10) | 0xdf)
        WRITE_PTE (G_L0 + 0x40), (((DATA >> 12) << 10) | 0xdf)
        li t0, 0x8000000000000004
        csrw vsatp, t0
        li t0, (0x8000000000000000 | (G_ROOT >> 12))
        csrw hgatp, t0
        TRY_DATA 0, 0, 1

        /* PMP denial while recursively reading a G-stage PTE. */
        PMP_PAGE G_ROOT, 0
        TRY_DATA 0, 5, 2
        TRY_DATA 1, 7, 3
        TRY_FETCH 1, 4

        /* A denial of the translated VS PTE is also an access fault. */
        PMP_PAGE VS_L0, 0
        TRY_DATA 0, 5, 5
        PMP_ALL

        /* A genuine G-stage mapping failure must remain a guest-page fault. */
        WRITE_PTE (G_L0 + 0x20), 0
        TRY_DATA 0, 21, 6
        TRY_DATA 1, 23, 7
        TRY_FETCH 20, 8
        WRITE_PTE (G_L0 + 0x20), (((VS_ROOT >> 12) << 10) | 0xdf)

        /*
         * VS A/D updating re-runs G-stage with store semantics. Its leaf D
         * update is rejected by PMP, after the initial read walk succeeded.
         */
        WRITE_PTE (VS_L0 + 0x20), 0x200f
        WRITE_PTE (G_L0 + 0x30), (((VS_L0 >> 12) << 10) | 0x5f)
        PMP_PAGE G_L0, 1
        TRY_DATA 0, 5, 9
        TRY_DATA 1, 7, 10
        li t0, (G_L0 + 0x30)
        ld t1, 0(t0)
        andi t1, t1, 0x80
        li a0, 11
        bnez t1, fail
        PMP_ALL
        WRITE_PTE (VS_L0 + 0x20), 0x20cf
        WRITE_PTE (G_L0 + 0x30), (((VS_L0 >> 12) << 10) | 0xdf)

        /* An unmapped physical G-stage root produces PMA access faults. */
        li t0, 0x8000000000090000
        csrw hgatp, t0
        TRY_DATA 0, 5, 12
        TRY_DATA 1, 7, 13
        TRY_FETCH 1, 14

        li t0, 0x8000000000000004
        csrw vsatp, t0
        li t0, (0x8000000000000000 | (G_ROOT >> 12))
        csrw hgatp, t0
        TRY_DATA 0, 0, 16
        li a0, 0
fail:
        lla a1, semiargs
        li t0, 0x20026
        sd t0, 0(a1)
        sd a0, 8(a1)
        li a0, 0x20
        .balign 16
        slli zero, zero, 0x1f
        ebreak
        srai zero, zero, 0x7
        j .

        .balign 4
trap:
        csrr s1, mcause
        csrw mepc, s2
        li t0, MPP_M | MPRV | MPV
        csrc mstatus, t0
        li t0, MPP_M
        csrs mstatus, t0
        mret

        .data
        .balign 8
semiargs:
        .space 16
