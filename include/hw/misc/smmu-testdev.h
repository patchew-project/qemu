/*
 * A test device for the SMMU
 *
 * This test device is a minimal SMMU-aware device used to test the SMMU.
 *
 * Copyright (c) 2025 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_SMMU_TESTDEV_H
#define HW_MISC_SMMU_TESTDEV_H

#include "qemu/osdep.h"
typedef enum SMMUTestDevSpace {
    STD_SPACE_SECURE    = 0,
    STD_SPACE_NONSECURE = 1,
    STD_SPACE_ROOT      = 2,
    STD_SPACE_REALM     = 3,
} SMMUTestDevSpace;

/* Only the Non-Secure space is implemented; leave room for future domains. */
#define STD_SUPPORTED_SPACES 1

/* BAR0 registers (offsets) */
enum {
    STD_REG_ID           = 0x00,
    STD_REG_ATTR_NS      = 0x04,
    STD_REG_SMMU_BASE_LO = 0x20,
    STD_REG_SMMU_BASE_HI = 0x24,
    STD_REG_DMA_IOVA_LO  = 0x28,
    STD_REG_DMA_IOVA_HI  = 0x2C,
    STD_REG_DMA_LEN      = 0x30,
    STD_REG_DMA_DIR      = 0x34,
    STD_REG_DMA_RESULT   = 0x38,
    STD_REG_DMA_DBELL    = 0x3C,
    /* Extended controls for DMA attributes/mode */
    STD_REG_DMA_MODE     = 0x40,
    STD_REG_DMA_ATTRS    = 0x44,
    /* Translation controls */
    STD_REG_TRANS_MODE   = 0x48,
    STD_REG_S1_SPACE     = 0x4C,
    STD_REG_S2_SPACE     = 0x50,
    STD_REG_TRANS_DBELL  = 0x54,
    STD_REG_TRANS_STATUS = 0x58,
    /* Clear helper-built tables/descriptors (write-any to trigger) */
    STD_REG_TRANS_CLEAR  = 0x5C,
};

/* DMA result/status values shared with tests */
#define STD_DMA_RESULT_IDLE 0xffffffffu
#define STD_DMA_RESULT_BUSY 0xfffffffeu
#define STD_DMA_ERR_BAD_LEN 0xdead0001u
#define STD_DMA_ERR_TX_FAIL 0xdead0002u

/* DMA attributes layout (for STD_REG_DMA_ATTRS) */
#define STD_DMA_ATTR_SECURE        (1u << 0)
#define STD_DMA_ATTR_SPACE_SHIFT   1
#define STD_DMA_ATTR_SPACE_MASK    (0x3u << STD_DMA_ATTR_SPACE_SHIFT)
#define STD_DMA_ATTR_UNSPECIFIED   (1u << 3)

/* Command type */
#define STD_CMD_CFGI_STE        0x03
#define STD_CMD_CFGI_CD         0x05
#define STD_CMD_TLBI_NSNH_ALL   0x30

/*
 * Translation tables and descriptors for a mapping of IOVA to GPA.
 *
 * This file defines a set of constants used to construct a static page table
 * for an smmu-testdev device. The goal is to translate a specific  STD_IOVA
 * into a final GPA.
 * The translation is based on the Arm architecture with the following
 * prerequisites:
 * - Granule size: 4KB pages.
 * - Page table levels: 4 levels (L0, L1, L2, L3), starting at level 0.
 * - IOVA size: The walk resolves a 39-bit IOVA (0x8080604567).
 * - Address space: The 4-level lookup with 4KB granules supports up to a
 * 48-bit (256TB) virtual address space. Each level uses a 9-bit index
 * (512 entries per table). The breakdown is:
 * - L0 index: IOVA bits [47:39]
 * - L1 index: IOVA bits [38:30]
 * - L2 index: IOVA bits [29:21]
 * - L3 index: IOVA bits [20:12]
 * - Page offset: IOVA bits [11:0]
 *
 * NOTE: All physical addresses defined here (STD_VTTB, table addresses, etc.)
 * appear to be within a secure RAM region. In practice, an offset is added
 * to these values to place them in non-secure RAM. For example, when running
 * in a virt machine type, the RAM base address (e.g., 0x40000000) is added to
 * these constants.
 *
 * The page table walk for STD_IOVA (0x8080604567) proceeds as follows:
 *
 * The Translation Table Base (for both Stage 1 CD_TTB and Stage 2 STE_S2TTB)
 * is set to STD_VTTB (0xe4d0000).
 *
 * 1. Level 0 (L0) Table Walk:
 * l0_index = (0x8080604567 >> 39) & 0x1ff = 1
 * STD_L0_ADDR = STD_VTTB + (l0_index * 8) = 0xe4d0000 + 8 = 0xe4d0008
 * STD_L0_VAL  = 0xe4d1003
 * The next level table base address = STD_L0_VAL & ~0xfff = 0xe4d1000
 *
 * 2. Level 1 (L1) Table Walk:
 * l1_index = (0x8080604567 >> 30) & 0x1ff = 2
 * STD_L1_ADDR = 0xe4d1000 + (l1_index * 8) = 0xe4d1000 + 16 = 0xe4d1010
 * STD_L1_VAL  = 0xe4d2003
 * The next level table base address = STD_L1_VAL & ~0xfff = 0xe4d2000
 *
 * 3. Level 2 (L2) Table Walk:
 * l2_index = (0x8080604567 >> 21) & 0x1ff = 3
 * STD_L2_ADDR = 0xe4d2000 + (l2_index * 8) = 0xe4d2000 + 24 = 0xe4d2018
 * STD_L2_VAL  = 0xe4d3003
 * The next level table base address = STD_L2_VAL & ~0xfff = 0xe4d3000
 *
 * 4. Level 3 (L3) Table Walk (Leaf):
 * l3_index = (0x8080604567 >> 12) & 0x1ff = 4
 * STD_L3_ADDR = 0xe4d3000 + (l3_index * 8) = 0xe4d3000 + 32 = 0xe4d3020
 * STD_L3_VAL  = 0x040000000ECBA7C3
 * The next level table base address = STD_L3_VAL & ~0xfff = 0xecba000
 *
 * 5. Final GPA Calculation:
 * - The final output physical address is formed by combining the address from
 * the leaf descriptor with the original IOVA's page offset.
 * - Output Page Base Address = (STD_L3_VAL & ~0xFFFULL) = 0xECBA000
 * - Page Offset = (STD_IOVA & 0xFFFULL) = 0x567.
 * - Final GPA = Output Page Base Address + Page Offset
 * = 0xECBA000 + 0x567 = 0xECBA567
 */

#define STD_IOVA              0x0000008080604567ULL

#define STD_VTTB 0xe4d0000

#define STD_STR_TAB_BASE      0x000000000E179000ULL
#define STD_STE_GPA           (STD_STR_TAB_BASE + 0x40ULL)
#define STD_CD_GPA            (STD_STR_TAB_BASE + 0x80ULL)

/* Page table structures */
#define STD_L0_ADDR           0x000000000E4D0008ULL
#define STD_L1_ADDR           0x000000000E4D1010ULL
#define STD_L2_ADDR           0x000000000E4D2018ULL
#define STD_L3_ADDR           0x000000000E4D3020ULL
#define STD_L0_VAL            0x000000000E4D1003ULL
#define STD_L1_VAL            0x000000000E4D2003ULL
#define STD_L2_VAL            0x000000000E4D3003ULL
#define STD_L3_VAL            0x040000000ECBA7C3ULL

/*
 * Nested stage PTW maybe a bit more complex. We share the page tables in
 * nested stage 2 to avoid complicated definitions here. That is to say:
 *
 * At each level of the Stage 1 page table walk, a corresponding 4-level Stage 2
 * page table walk is performed. The intermediate Stage 2 page tables are shared
 * across these walks, with the key connecting PTE values being:
 * - l0_pte_val=0x4e4d1003
 * - l1_pte_val=0x4e4d2003
 * - l2_pte_val=0x4e4d3003
 *
 *
 * ======================================================================
 * Nested Page Table Walk (Stage 1 + Stage 2) Example
 * ======================================================================
 *
 * Goal: Translate IOVA 0x8080604567 to a final Physical Address (PA).
 *
 * Prerequisites:
 * - Stage 1 VTTB (as IPA): 0x4e4d0000
 * - Stage 2 VTTB (as PA):  0x4e4d0000
 *
 * ----------------------------------------------------------------------
 * 1. Stage 1 Page Table Walk (IOVA -> IPA)
 * ----------------------------------------------------------------------
 *
 * Level 0 (L0) Walk (IPA as PA)
 * =============================
 * iova            = 0x8080604567
 * l0_index        = (0x8080604567 >> 39) & 0x1ff = 1
 * s1_l0_pte_addr (IPA) = 0x4e4d0000 + (1 * 8) = 0x4e4d0008
 *
 * --> Nested Stage 2 Walk for S1 L0 Table (IPA 0x4e4d0000 -> PA 0x4e4d0000)
 * -------------------------------------------------------------------------
 * ipa_to_translate      = 0x4e4d0000
 * s2_vttb               = 0x4e4d0000
 * s2_l0_index           = (0x4e4d0000 >> 39) & 0x1ff = 0
 * s2_l0_pte_addr (PA)   = 0x4e4d0000 + (0 * 8) = 0x4e4d0000
 * s2_l0_pte_val         = 0x4e4d1003
 * s2_l1_table_base (PA) = 0x4e4d1003 & ~0xfff = 0x4e4d1000
 * s2_l1_index           = (0x4e4d0000 >> 30) & 0x1ff = 1
 * s2_l1_pte_addr (PA)   = 0x4e4d1000 + (1 * 8) = 0x4e4d1008
 * s2_l1_pte_val         = 0x4e4d2003
 * s2_l2_table_base (PA) = 0x4e4d2003 & ~0xfff = 0x4e4d2000
 * s2_l2_index           = (0x4e4d0000 >> 21) & 0x1ff = 114 (0x72)
 * s2_l2_pte_addr (PA)   = 0x4e4d2000 + (114 * 8) = 0x4e4d2390
 * s2_l2_pte_val         = 0x4e4d3003
 * s2_l3_table_base (PA) = 0x4e4d3003 & ~0xfff = 0x4e4d3000
 * s2_l3_index           = (0x4e4d0000 >> 12) & 0x1ff = 208 (0xd0)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (208 * 8) = 0x4e4d3680
 * s2_l3_pte_val         = 0x040000000E4D0743ULL
 * output_page_base (PA) = 0x040000000E4D0743ULL & ~0xfff = 0x4e4d0000
 * Final PA for table    = 0x4e4d0000 + (0x0000 & 0xfff) = 0x4e4d0000
 *
 * s1_l0_pte_val        (read from PA 0x4e4d0008) = 0x4e4d1003
 * s1_l1_table_base (IPA) = 0x4e4d1003 & ~0xfff = 0x4e4d1000
 *
 * Level 1 (L1) Walk (IPA as PA)
 * =============================
 * iova            = 0x8080604567
 * l1_index        = (0x8080604567 >> 30) & 0x1ff = 2
 * s1_l1_pte_addr (IPA) = 0x4e4d1000 + (2 * 8) = 0x4e4d1010
 *
 * --> Nested Stage 2 Walk for S1 L1 Table (IPA 0x4e4d1000 -> PA 0x4e4d1000)
 * -------------------------------------------------------------------------
 * ipa_to_translate      = 0x4e4d1000
 * s2_vttb               = 0x4e4d0000
 * s2_l0_index           = (0x4e4d1000 >> 39) & 0x1ff = 0
 * s2_l1_table_base (PA) = 0x4e4d1000
 * s2_l1_index           = (0x4e4d1000 >> 30) & 0x1ff = 1
 * s2_l2_table_base (PA) = 0x4e4d2000
 * s2_l2_index           = (0x4e4d1000 >> 21) & 0x1ff = 114 (0x72)
 * s2_l3_table_base (PA) = 0x4e4d3000
 * s2_l3_index           = (0x4e4d1000 >> 12) & 0x1ff = 209 (0xd1)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (209 * 8) = 0x4e4d3688
 * s2_l3_pte_val         = 0x40000004e4d1743
 * output_page_base (PA) = 0x40000004e4d1743 & ~0xfff = 0x4e4d1000
 * Final PA for table    = 0x4e4d1000 + (0x1000 & 0xfff) = 0x4e4d1000
 *
 * s1_l1_pte_val        (read from PA 0x4e4d1010) = 0x4e4d2003
 * s1_l2_table_base (IPA) = 0x4e4d2003 & ~0xfff = 0x4e4d2000
 *
 * Level 2 (L2) Walk (IPA as PA)
 * =============================
 * l2_index        = (0x8080604567 >> 21) & 0x1ff = 3
 * s1_l2_pte_addr (IPA) = 0x4e4d2000 + (3 * 8) = 0x4e4d2018
 *
 * --> Nested Stage 2 Walk for S1 L2 Table (IPA 0x4e4d2000 -> PA 0x4e4d2000)
 * -------------------------------------------------------------------------
 * ipa_to_translate      = 0x4e4d2000
 * s2_l0_index           = (0x4e4d2000 >> 39) & 0x1ff = 0
 * s2_l1_table_base (PA) = 0x4e4d1000
 * s2_l1_index           = (0x4e4d2000 >> 30) & 0x1ff = 1
 * s2_l2_table_base (PA) = 0x4e4d2000
 * s2_l2_index           = (0x4e4d2000 >> 21) & 0x1ff = 114 (0x72)
 * s2_l3_table_base (PA) = 0x4e4d3000
 * s2_l3_index           = (0x4e4d2000 >> 12) & 0x1ff = 210 (0xd2)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (210 * 8) = 0x4e4d3690
 * s2_l3_pte_val         = 0x40000004e4d2743
 * output_page_base (PA) = 0x40000004e4d2743 & ~0xfff = 0x4e4d2000
 * Final PA for table    = 0x4e4d2000 + (0x2000 & 0xfff) = 0x4e4d2000
 *
 * s1_l2_pte_val        (read from PA 0x4e4d2018) = 0x4e4d3003
 * s1_l3_table_base (IPA) = 0x4e4d3003 & ~0xfff = 0x4e4d3000
 *
 * Level 3 (L3) Walk (Leaf, IPA as PA)
 * ===================================
 * l3_index        = (0x8080604567 >> 12) & 0x1ff = 4
 * s1_l3_pte_addr (IPA) = 0x4e4d3000 + (4 * 8) = 0x4e4d3020
 *
 * --> Nested Stage 2 Walk for S1 L3 Table (IPA 0x4e4d3000 -> PA 0x4e4d3000)
 * -------------------------------------------------------------------------
 * ipa_to_translate      = 0x4e4d3000
 * s2_l0_index           = (0x4e4d3000 >> 39) & 0x1ff = 0
 * s2_l1_table_base (PA) = 0x4e4d1000
 * s2_l1_index           = (0x4e4d3000 >> 30) & 0x1ff = 1
 * s2_l2_table_base (PA) = 0x4e4d2000
 * s2_l2_index           = (0x4e4d3000 >> 21) & 0x1ff = 114 (0x72)
 * s2_l3_table_base (PA) = 0x4e4d3000
 * s2_l3_index           = (0x4e4d3000 >> 12) & 0x1ff = 211 (0xd3)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (211 * 8) = 0x4e4d3698
 * s2_l3_pte_val         = 0x40000004e4d3743
 * output_page_base (PA) = 0x40000004e4d3743 & ~0xfff = 0x4e4d3000
 * Final PA for table    = 0x4e4d3000 + (0x3000 & 0xfff) = 0x4e4d3000
 *
 * s1_l3_pte_val        (read from PA 0x4e4d3020) = 0x40000004ecba743
 * output_page_base (IPA) = 0x40000004ecba743 & ~0xfff = 0x4ecba000
 * page_offset          = 0x8080604567 & 0xfff = 0x567
 * Final IPA            = 0x4ecba000 + 0x567 = 0x4ecba567
 *
 * ----------------------------------------------------------------------
 * 2. Final Stage 2 Page Table Walk (Final IPA -> PA)
 * ----------------------------------------------------------------------
 *
 * ipa = 0x4ecba567
 *
 * s2_l0_index           = (0x4ecba567 >> 39) & 0x1ff = 0
 * s2_l1_table_base (PA) = 0x4e4d1000
 *
 * s2_l1_index           = (0x4ecba567 >> 30) & 0x1ff = 1
 * s2_l2_table_base (PA) = 0x4e4d2000
 *
 * s2_l2_index           = (0x4ecba567 >> 21) & 0x1ff = 118 (0x76)
 * s2_l3_table_base (PA) = 0x4e4d3000
 *
 * s2_l3_index           = (0x4ecba567 >> 12) & 0x1ff = 186 (0xba)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (186 * 8) = 0x4e4d35d0
 * s2_l3_pte_val         = 0x40000004ecba7c3
 * output_page_base (PA) = 0x40000004ecba7c3 & ~0xfff = 0x4ecba000
 * page_offset           = 0x4ecba567 & 0xfff = 0x567
 *
 * ----------------------------------------------------------------------
 * 3. Final Result
 * ----------------------------------------------------------------------
 * Final PA = 0x4ecba000 + 0x567 = 0x4ecba567
 *
 * ----------------------------------------------------------------------
 * 4. Appendix: Context Descriptor (CD) Fetch Walk
 * ----------------------------------------------------------------------
 * Before any S1 walk can begin, the SMMU must fetch the Context Descriptor.
 * The CD's address is an IPA, so it also requires a full S2 walk. This
 * walk RE-USES the exact same S2 page tables shown above.
 *
 * ipa = 0x4e179080 (Address of the CD)
 *
 * s2_l0_index           = (0x4e179080 >> 39) & 0x1ff = 0
 * s2_l0_pte_val         = 0x4e4d1003
 * s2_l1_table_base (PA) = 0x4e4d1000 (*RE-USED*)
 *
 * s2_l1_index           = (0x4e179080 >> 30) & 0x1ff = 1
 * s2_l1_pte_val         = 0x4e4d2003
 * s2_l2_table_base (PA) = 0x4e4d2000 (*RE-USED*)
 *
 * s2_l2_index           = (0x4e179080 >> 21) & 0x1ff = 112 (0x70)
 * s2_l2_pte_val         = 0x4e4d3003
 * s2_l3_table_base (PA) = 0x4e4d3000 (*RE-USED*)
 *
 * s2_l3_index           = (0x4e179080 >> 12) & 0x1ff = 377 (0x179)
 * s2_l3_pte_addr (PA)   = 0x4e4d3000 + (377 * 8) = 0x4e4d3bc8
 * s2_l3_pte_val         = 0x40000004e179743
 * output_page_base (PA) = 0x40000004e179743 & ~0xfff = 0x4e179000
 * page_offset           = 0x4e179080 & 0xfff = 0x080
 *
 * Final PA for CD       = 0x4e179000 + 0x080 = 0x4e179080
 *
 */
#define STD_CD_S2_L0_ADDR     0x000000000E4D0000ULL
#define STD_CD_S2_L1_ADDR     0x000000000E4D1008ULL
#define STD_CD_S2_L2_ADDR     0x000000000E4D2380ULL
#define STD_CD_S2_L3_ADDR     0x000000000E4D3BC8ULL
#define STD_CD_S2_L3_VAL      0x040000000E179743ULL

#define STD_CDTTB_S2_L2_ADDR  0x000000000E4D2390ULL
#define STD_CDTTB_S2_L3_ADDR  0x000000000E4D3680ULL
#define STD_CDTTB_S2_L3_VAL   0x040000000E4D0743ULL

#define STD_L3_S1_VAL         0x040000000ECBA743ULL

#define STD_S1L0_IN_S2L3_ADDR 0x000000000E4D3688ULL
#define STD_S1L0_IN_S2L3_VAL  0x040000000E4D1743ULL
#define STD_S1L1_IN_S2L3_ADDR 0x000000000E4D3690ULL
#define STD_S1L1_IN_S2L3_VAL  0x040000000E4D2743ULL
#define STD_S1L2_IN_S2L3_ADDR 0x000000000E4D3698ULL
#define STD_S1L2_IN_S2L3_VAL  0x040000000E4D3743ULL
#define STD_S1L3_IN_S2L2_ADDR 0x000000000E4D23B0ULL
#define STD_S1L3_IN_S2L2_VAL  0x000000000E4D3003ULL
#define STD_S1L3_IN_S2L3_ADDR 0x000000000E4D35D0ULL
#define STD_S1L3_IN_S2L3_VAL  0x040000000ECBA7C3ULL

/*
 * Address-space base offsets for test tables.
 * - Non-Secure uses a fixed offset, keeping internal layout identical.
 *
 * Note: Future spaces (e.g. Secure/Realm/Root) are not implemented here.
 * When needed, introduce new offsets and reuse the helpers below so
 * relative layout stays identical across spaces.
 */
#define STD_SPACE_OFFS_NS       0x40000000ULL

static inline uint64_t std_space_offset(SMMUTestDevSpace sp)
{
    switch (sp) {
    case STD_SPACE_NONSECURE:
        return STD_SPACE_OFFS_NS;
    default:
        g_assert_not_reached();
    }
}

static const char *std_space_to_str(SMMUTestDevSpace sp)
{
    switch (sp) {
    case STD_SPACE_NONSECURE:
        return "Non-Secure";
    default:
        g_assert_not_reached();
    }
}

static const char *std_mode_to_str(uint32_t m)
{
    switch (m & 0x3) {
    case 0: return "S1-only";
    case 1: return "S2-only";
    case 2: return "Nested";
    default:
        g_assert_not_reached();
    }
}

#endif /* HW_MISC_SMMU_TESTDEV_H */
