/*
 * Shared definitions for the riscv64 hypervisor-extension system tests,
 * whose routines are in hyp-guest.S. hyp-guest.S:
 *
 * 1. Boots the machine in M-mode, sets up the stack and tells the CPU
 *    which trap handlers to use.
 * 2. Builds the guest's page tables and the G-stage (host) page tables.
 * 3. Calls `test`, which starts the test.
 *
 * The test file provides:
 * - test: where the test starts, called by _start after setup
 * - trap: the M-mode trap handler
 * - hs_trap: the HS-mode trap handler
 *
 * Helpers a test can call:
 * - set_tables_perm(a0): set what the G-stage lets the guest do to its
 *   own page tables. a0 is G_RW, G_RO or G_NOREAD.
 * - run_guest(a0, a1, a2): run the code at address a1 in mode a2 (VS, VU
 *   or HS), with a0 passed on to it in a0. It does not return: the
 *   code's trap goes to the test's handler.
 * - exit(a0): end the test with status a0, 0 for a pass, otherwise the
 *   number of the check that failed.
 *
 * Memory is split into 2 MiB regions; region n starts at
 * REGION(n) = 0x80000000 + n * 2 MiB. Both stages map each region to
 * itself.
 *
 *   0     code and the G-stage page tables
 *   1     data and the stack
 *   2     the guest's page tables
 *   3-23  free for the test
 *   24    region 0's code again, user-accessible, for VU-mode
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define PTE_V 0x01
#define PTE_R 0x02
#define PTE_W 0x04
#define PTE_X 0x08
#define PTE_U 0x10
#define PTE_A 0x40
#define PTE_D 0x80
#define leaf(pa, flags) (((pa) >> 2) | PTE_V | (flags))
#define table(pa) (((pa) >> 2) | PTE_V)

#define REGION(n) (0x80000000 + (n) * 0x200000)
#define G_ROOT  0x80104000
#define G_L1    0x80108000
#define VS_ROOT 0x80400000
#define VS_L1   0x80401000
#define VS_L0   0x80402000      /* 4 KiB table for region 8 */

/* G-stage permission of the guest's tables */
#define G_RW    (PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)
#define G_RO    (G_RW & ~PTE_W)
#define G_NOREAD (G_RO & ~PTE_R)

/* run_guest modes */
#define VU 0
#define HS 1
#define VS 2

#define LOAD_PF 13
#define STORE_PF 15
#define FETCH_GPF 20
#define LOAD_GPF 21
#define STORE_GPF 23
#define TINST_READ 0x3000       /* walk was reading a page-table entry */
#define TINST_WRITE 0x3020      /* walk was writing A/D */
#define ADUE (1 << 61)
