#include <stdint.h>
#include "sysregs.h"
#include "pgtable.h"

/*
 * Page table setup for AArch64 system tests.
 * We use a flat identity mapping.
 */

/* Symbols defined in kernel.ld */
extern char _text_start[];
extern char _data_start[];
extern char _tag_start[];

/* Assume these start zeroed */
uint64_t ttb_l1[512] __attribute__((aligned(4096)));
uint64_t ttb_l2[512] __attribute__((aligned(4096)));

/*
 * Setup a flat address mapping page-tables.
 *
 * ttb (Level 1):
 *   - Entry 0 [0 - 1GB]: 1GB Device block (for GIC and other H/W)
 *   - Entry 1 [1GB - 2GB]: Table entry pointing to ttb_l2 (for RAM)
 */
void setup_pgtables(void)
{
    /* L1 entry 0: 1GB Device block mapping at 0x0 */
    pgt_map_l1_block(ttb_l1, 0, 0, DESC_AF | DESC_ATTRINDX(2));

    /* L1 entry 1: points to L2 table for finer permissions */
    pgt_map_l1_table(ttb_l1, (uintptr_t)_text_start, ttb_l2);

    /* L2 entries: 2MB blocks */
    /* .text & .rodata (Read-only, executable) */
    flat_map_stage2(ttb_l2, (uintptr_t)_text_start, DESC_AF);

    /* .data & .bss (Read-write, no-execute) */
    flat_map_stage2(ttb_l2, (uintptr_t)_data_start, DESC_AF | DESC_PXN | DESC_UXN);

    /* mte_page (Read-write, no-execute, AttrIndx=1) */
    flat_map_stage2(ttb_l2, (uintptr_t)_tag_start, DESC_AF | DESC_PXN | DESC_UXN | DESC_ATTRINDX(1));

    /* Set TTBR0_EL1 */
    write_sysreg(ttb_l1, ttbr0_el1);

    /*
     * Set MAIR_EL1
     *
     * Attr0 (0xee): Normal memory, Outer/Inner WB/WA/Read-Alloc
     * Attr1 (0x00): MTE page
     * Attr2 (0x04): Device-nGnRE memory (for the first 1GB)
     */
    write_sysreg(0x0400eeULL, mair_el1);

    /* Set TCR_EL1 */
    uint64_t tcr = TCR_EL1_IPS_40BIT | TCR_EL1_TG0_4KB | TCR_EL1_ORGN0_WBWA | TCR_EL1_IRGN0_WBWA | TCR_EL1_T0SZ(25);
    write_sysreg(tcr, tcr_el1);
    isb();

    /* Enable MMU via SCTLR_EL1 */
    uint64_t sctlr = read_sysreg(sctlr_el1);
    sctlr &= ~(1ULL << 1); /* Clear A (alignment check) */
    sctlr &= ~(1ULL << 19); /* Clear WXN */
    sctlr |= SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I | SCTLR_EL1_SA;

    dsb(sy);
    write_sysreg(sctlr, sctlr_el1);
    isb();
}
