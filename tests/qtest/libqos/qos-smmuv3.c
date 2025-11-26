/*
 * QOS SMMUv3 Module
 *
 * This module provides SMMUv3-specific helper functions for libqos tests,
 * encapsulating SMMUv3 setup, assertion, and cleanup operations.
 *
 * Copyright (c) 2025 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/smmuv3-common.h"
#include "tests/qtest/libqos/pci.h"
#include "hw/misc/iommu-testdev.h"
#include "qos-smmuv3.h"

#define QSMMU_STE_S2T0SZ_VAL 0x14

/* Apply space offset to address */
static inline uint64_t qsmmu_apply_space_offs(QSMMUSpace sp,
                                              uint64_t address)
{
    return address + qsmmu_space_offset(sp);
}

uint32_t qsmmu_expected_dma_result(QSMMUTestContext *ctx)
{
    /* Currently only non-secure space is supported. */
    if (ctx->tx_space != QSMMU_SPACE_NONSECURE) {
        return ITD_DMA_ERR_TX_FAIL;
    }
    return ctx->config.expected_result;
}

uint32_t qsmmu_build_dma_attrs(QSMMUSpace space)
{
    uint32_t attrs = 0;
    switch (space) {
    case QSMMU_SPACE_NONSECURE:
        /* Non-secure: secure=0, space=1 */
        attrs = ITD_ATTRS_SET_SECURE(attrs, 0);
        attrs = ITD_ATTRS_SET_SPACE(attrs, QSMMU_SPACE_NONSECURE);
        break;
    default:
        g_assert_not_reached();
    }

    return attrs;
}

uint32_t qsmmu_setup_and_enable_translation(QSMMUTestContext *ctx)
{
    uint32_t build_result;

    /* Build page tables and SMMU structures first */
    build_result = qsmmu_build_translation(
                       ctx->qts, ctx->config.trans_mode,
                       ctx->tx_space, ctx->sid);
    if (build_result != 0) {
        g_test_message("Build failed: mode=%u sid=%u status=0x%x",
                       ctx->config.trans_mode, ctx->sid, build_result);
        ctx->trans_status = build_result;
        return ctx->trans_status;
    }

    /* Program SMMU registers for the appropriate security space */
    qsmmu_program_regs(ctx->qts, ctx->smmu_base, ctx->tx_space);

    ctx->trans_status = 0;
    return ctx->trans_status;
}

uint32_t qsmmu_trigger_dma(QSMMUTestContext *ctx)
{
    uint32_t result, attrs_val;

    /* Program DMA parameters */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_GVA_LO,
                   (uint32_t)ctx->config.dma_iova);
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_GVA_HI,
                   (uint32_t)(ctx->config.dma_iova >> 32));
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_LEN,
                   ctx->config.dma_len);

    /* Build and write DMA attributes based on device security state. */
    attrs_val = qsmmu_build_dma_attrs(QSMMU_SPACE_NONSECURE);
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_ATTRS, attrs_val);

    /* Flip status */
    /* Arm iommu-testdev so the next read triggers DMA */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);

    /* Trigger DMA by reading ID register */
    qpci_io_readl(ctx->dev, ctx->bar, ITD_REG_DMA_TRIGGERING);

    /* Poll for DMA completion */
    for (int i = 0; i < 1000; i++) {
        result = qpci_io_readl(ctx->dev, ctx->bar, ITD_REG_DMA_RESULT);
        if (result != ITD_DMA_RESULT_BUSY) {
            ctx->dma_result = result;
            break;
        }
        g_usleep(1000);
    }

    /* Fallback for timeout */
    if (ctx->dma_result == ITD_DMA_RESULT_BUSY) {
        ctx->dma_result = ITD_DMA_ERR_TX_FAIL;
    }

    return ctx->dma_result;
}

static void qsmmu_push_cfgi_cmd(QTestState *qts, uint64_t smmu_base,
                                QSMMUSpace bank_sp, uint32_t type,
                                uint32_t sid, bool ssec)
{
    hwaddr bank_off;
    uint32_t new_prod, base_lo, base_hi, log2size, prod;
    uint32_t index_mask, slot, words[4];
    uint64_t base, qbase, entry_pa;

    g_assert_false(ssec);

    bank_off = 0;

    /* Read CMDQ_BASE register */
    base_lo = qtest_readl(qts, smmu_base + bank_off + A_CMDQ_BASE);
    base_hi = qtest_readl(qts, smmu_base + bank_off + A_CMDQ_BASE + 4);
    base = ((uint64_t)base_hi << 32) | base_lo;
    log2size = base & 0x1f;
    qbase = base & SMMU_BASE_ADDR_MASK;

    /* Read CMDQ_PROD register */
    prod = qtest_readl(qts, smmu_base + bank_off + A_CMDQ_PROD);
    index_mask = (1u << log2size) - 1u;
    slot = prod & index_mask;
    entry_pa = qbase + (uint64_t)slot * 16u;

    /* Prepare command words */
    memset(words, 0, sizeof(words));
    words[0] = (type & 0xff) | (ssec ? (1u << 10) : 0u);
    words[1] = sid;

    /* Write command to the command queue */
    for (int i = 0; i < 4; i++) {
        qtest_writel(qts, entry_pa + i * 4, words[i]);
    }

    /* Update PROD to trigger command handler */
    new_prod = (prod + 1) & ((1u << (log2size + 1)) - 1u);
    qtest_writel(qts, smmu_base + bank_off + A_CMDQ_PROD, new_prod);
}

void qsmmu_cleanup_translation(QSMMUTestContext *ctx)
{
    static const QSMMUSpace spaces[] = { QSMMU_SPACE_NONSECURE };
    size_t ste_cd_entry_bytes = sizeof(STE); /* STE size == CD size */
    uint32_t sid;
    uint64_t ste_addr, ste_addr_real, cd_addr_real;
    QSMMUSpace build_space;

    sid = ctx->sid;
    ste_addr = sid * ste_cd_entry_bytes + QSMMU_STR_TAB_BASE;

    /* Clear page table entries and configuration structures */
    for (int idx = 0; idx < ARRAY_SIZE(spaces); idx++) {
        build_space = spaces[idx];

        ste_addr_real = qsmmu_apply_space_offs(build_space, ste_addr);
        cd_addr_real = qsmmu_apply_space_offs(build_space, QSMMU_CD_GPA);
        for (int i = 0; i < ste_cd_entry_bytes / sizeof(uint32_t); i++) {
            qtest_writel(ctx->qts, ste_addr_real + i * 4, 0);
            g_assert_cmpint(qtest_readl(ctx->qts, ste_addr_real + i * 4),
                            ==, 0);

            qtest_writel(ctx->qts, cd_addr_real + i * 4, 0);
            g_assert_cmpint(qtest_readl(ctx->qts, cd_addr_real + i * 4),
                            ==, 0);
        }
    }

    /* Invalidate SMMU caches via configuration invalidation commands */
    if (ctx->smmu_base) {
        /* Issue cache invalidation commands to SMMU */
        qsmmu_push_cfgi_cmd(ctx->qts, ctx->smmu_base, QSMMU_SPACE_NONSECURE,
                            SMMU_CMD_CFGI_STE, sid, false);
        qsmmu_push_cfgi_cmd(ctx->qts, ctx->smmu_base, QSMMU_SPACE_NONSECURE,
                            SMMU_CMD_CFGI_CD, sid, false);
        qsmmu_push_cfgi_cmd(ctx->qts, ctx->smmu_base, QSMMU_SPACE_NONSECURE,
                            SMMU_CMD_TLBI_NSNH_ALL, sid, false);
    }
}

bool qsmmu_validate_test_result(QSMMUTestContext *ctx)
{
    uint32_t expected = qsmmu_expected_dma_result(ctx);
    g_test_message("-> Validating result: expected=0x%x actual=0x%x",
                   expected, ctx->dma_result);
    return (ctx->dma_result == expected);
}

QSMMUSpace qsmmu_sec_sid_to_space(QSMMUSecSID sec_sid)
{
    switch (sec_sid) {
    case QSMMU_SEC_SID_NONSECURE:
        return QSMMU_SPACE_NONSECURE;
    default:
        g_assert_not_reached();
    }
}

uint64_t qsmmu_space_offset(QSMMUSpace sp)
{
    switch (sp) {
    case QSMMU_SPACE_NONSECURE:
        return QSMMU_SPACE_OFFS_NS;
    default:
        g_assert_not_reached();
    }
}

void qsmmu_single_translation(QSMMUTestContext *ctx)
{
    uint32_t config_result;
    uint32_t dma_result;
    bool test_passed;

    /* Configure SMMU translation */
    config_result = qsmmu_setup_and_enable_translation(ctx);
    if (config_result != 0) {
        g_test_message("Configuration failed: mode=%u status=0x%x",
                       ctx->config.trans_mode, config_result);
        return;
    }

    /* Trigger DMA operation */
    dma_result = qsmmu_trigger_dma(ctx);
    if (dma_result != 0) {
        g_test_message("DMA failed: mode=%u result=0x%x",
                       ctx->config.trans_mode, dma_result);
    } else {
        g_test_message("-> DMA succeeded: mode=%u", ctx->config.trans_mode);
    }

    /* Validate test result */
    test_passed = qsmmu_validate_test_result(ctx);
    g_assert_true(test_passed);

    /* Clean up translation state to prepare for the next test */
    qsmmu_cleanup_translation(ctx);
}

void qsmmu_translation_batch(const QSMMUTestConfig *configs, size_t count,
                             QTestState *qts, QPCIDevice *dev,
                             QPCIBar bar, uint64_t smmu_base)
{
    for (int i = 0; i < count; i++) {
        /* Initialize test memory */
        qtest_memset(qts, configs[i].dma_iova, 0x00, configs[i].dma_len);
        /* Execute each test configuration */
        QSMMUTestContext ctx = {
            .qts = qts,
            .dev = dev,
            .bar = bar,
            .smmu_base = smmu_base,
            .config = configs[i],
            .trans_status = 0,
            .dma_result = 0,
            .sid = dev->devfn,
            .tx_space = qsmmu_sec_sid_to_space(configs[i].sec_sid),
        };

        qsmmu_single_translation(&ctx);
        g_test_message("--> Test %d completed: mode=%u sec_sid=%u "
                       "status=0x%x result=0x%x", i, configs[i].trans_mode,
                       configs[i].sec_sid, ctx.trans_status, ctx.dma_result);
    }
}

uint32_t qsmmu_build_translation(QTestState *qts, QSMMUTransMode mode,
                                 QSMMUSpace tx_space, uint32_t sid)
{
    uint64_t ste_addr, ste_addr_real, cd_addr_real;
    uint64_t cd_ttb, vttb, vttb_real;
    uint8_t nscfg0, nscfg1;
    QSMMUSpace build_space;
    size_t ste_cd_entry_bytes = sizeof(STE);
    STE ste;
    CD cd;

    build_space = tx_space;
    if (build_space != QSMMU_SPACE_NONSECURE) {
        return 0xdeadbeafu;
    }

    /* Build STE image */
    memset(&ste, 0, sizeof(ste));
    switch (mode) {
    case QSMMU_TM_S1_ONLY:
        STE_SET_CONFIG(&ste, 0x5);
        break;
    case QSMMU_TM_S2_ONLY:
        STE_SET_CONFIG(&ste, 0x6);
        break;
    case QSMMU_TM_NESTED:
    default:
        STE_SET_CONFIG(&ste, 0x7);
        break;
    }

    STE_SET_VALID(&ste, 1);
    STE_SET_S2T0SZ(&ste, QSMMU_STE_S2T0SZ_VAL);
    STE_SET_S2SL0(&ste, 0x2);
    STE_SET_S2TG(&ste, 0);
    STE_SET_S2PS(&ste, 0x5);
    STE_SET_S2AA64(&ste, 1);
    STE_SET_S2ENDI(&ste, 0);
    STE_SET_S2AFFD(&ste, 0);

    /*
     * The consistent policy also extends to pointer fetches. For cases that
     * require reading STE.S1ContextPtr or STE.S2TTB, we still follow the same
     * policy:
     * - The PA space security attribute of the address pointed to
     *   (e.g., the CD or S2L1 table) must also match the input 'SEC_SID'.
     */
    cd_addr_real = qsmmu_apply_space_offs(build_space, QSMMU_CD_GPA);
    STE_SET_CTXPTR(&ste, cd_addr_real);

    vttb = QSMMU_VTTB;
    vttb_real = qsmmu_apply_space_offs(build_space, vttb);
    STE_SET_S2TTB(&ste, vttb_real);

    ste_addr = sid * ste_cd_entry_bytes + QSMMU_STR_TAB_BASE;
    ste_addr_real = qsmmu_apply_space_offs(build_space, ste_addr);

    /* Write STE to memory */
    for (int i = 0; i < ARRAY_SIZE(ste.word); i++) {
        qtest_writel(qts, ste_addr_real + i * 4, ste.word[i]);
    }

    switch (tx_space) {
    case QSMMU_SPACE_NONSECURE:
        nscfg0 = 0x1;
        nscfg1 = 0x1;
        break;
    default:
        g_assert_not_reached();
    }
    /* Build CD image for S1 path if needed */
    if (mode != QSMMU_TM_S2_ONLY) {
        memset(&cd, 0, sizeof(cd));

        CD_SET_ASID(&cd, 0x1e20);
        CD_SET_AARCH64(&cd, 1);
        CD_SET_VALID(&cd, 1);
        CD_SET_A(&cd, 1);
        CD_SET_S(&cd, 0);
        CD_SET_HD(&cd, 0);
        CD_SET_HA(&cd, 0);
        CD_SET_IPS(&cd, 0x4);
        CD_SET_TBI(&cd, 0x0);
        CD_SET_AFFD(&cd, 0x0);
        CD_SET_EPD(&cd, 0, 0x0);
        CD_SET_EPD(&cd, 1, 0x1);
        CD_SET_TSZ(&cd, 0, 0x10);
        CD_SET_TG(&cd, 0, 0x0);
        CD_SET_ENDI(&cd, 0x0);

        CD_SET_NSCFG(&cd, 0, nscfg0);
        CD_SET_NSCFG(&cd, 1, nscfg1);
        CD_SET_R(&cd, 0x1);
        cd_ttb = vttb_real;
        CD_SET_TTB(&cd, 0, cd_ttb);

        for (int i = 0; i < ARRAY_SIZE(cd.word); i++) {
            /* TODO: Maybe need more work to write to secure RAM in future */
            qtest_writel(qts, cd_addr_real + i * 4, cd.word[i]);
            g_assert_cmpint(qtest_readl(qts, cd_addr_real + i * 4), ==,
                            cd.word[i]);
        }
    }

    qsmmu_setup_translation_tables(qts, QSMMU_IOVA_OR_IPA, build_space,
                                   false, mode);
    /* Nested extras: CD S2 tables */
    if (mode == QSMMU_TM_NESTED) {
        /*
         * Extra Stage 2 page tables is needed if
         *          SMMUTranslationClass == SMMU_CLASS_CD
         * as smmuv3_do_translate would translate an IPA of the CD to the final
         * output CD after a Stage 2 translation.
         */
        qsmmu_setup_translation_tables(qts, cd_addr_real, build_space,
                                       true, mode);
    }

    return 0;
}

uint64_t qsmmu_bank_base(uint64_t base, QSMMUSpace sp)
{
    switch (sp) {
    case QSMMU_SPACE_NONSECURE:
        return base;
    default:
        g_assert_not_reached();
    }
}

void qsmmu_program_bank(QTestState *qts, uint64_t bank_base, QSMMUSpace sp)
{
    uint64_t cmdq_base, eventq_base, strtab_base;

    qtest_writel(qts, bank_base + A_GBPA, 0x80000000);  /* UPDATE */
    qtest_writel(qts, bank_base + A_CR0, 0x0);          /* Disable */
    qtest_writel(qts, bank_base + A_CR1, 0x0d75);       /* Config */

    /* CMDQ_BASE: add address-space offset*/
    cmdq_base = qsmmu_apply_space_offs(sp, QSMMU_CMDQ_BASE_ADDR);
    cmdq_base |= 0x0a;  /* Size and valid bits */
    qtest_writeq(qts, bank_base + A_CMDQ_BASE, cmdq_base);

    qtest_writel(qts, bank_base + A_CMDQ_CONS, 0x0);
    qtest_writel(qts, bank_base + A_CMDQ_PROD, 0x0);

    /* EVENTQ_BASE: add address-space offset */
    eventq_base = qsmmu_apply_space_offs(sp, QSMMU_EVENTQ_BASE_ADDR);
    eventq_base |= 0x0a;  /* Size and valid bits */
    qtest_writeq(qts, bank_base + A_EVENTQ_BASE, eventq_base);

    qtest_writel(qts, bank_base + A_EVENTQ_PROD, 0x0);
    qtest_writel(qts, bank_base + A_EVENTQ_CONS, 0x0);

    /* STRTAB_BASE_CFG: linear stream table, LOG2SIZE=5 */
    qtest_writel(qts, bank_base + A_STRTAB_BASE_CFG, 0x5);

    /* STRTAB_BASE: add address-space offset */
    strtab_base = qsmmu_apply_space_offs(sp, QSMMU_STR_TAB_BASE);
    qtest_writeq(qts, bank_base + A_STRTAB_BASE, strtab_base);

    /* CR0: Enable SMMU with appropriate flags */
    qtest_writel(qts, bank_base + A_CR0, 0xd);
}

void qsmmu_program_regs(QTestState *qts, uint64_t smmu_base, QSMMUSpace space)
{
    uint64_t sp_base;
    /* Always program Non-Secure bank first */
    uint64_t ns_base = qsmmu_bank_base(smmu_base, QSMMU_SPACE_NONSECURE);
    qsmmu_program_bank(qts, ns_base, QSMMU_SPACE_NONSECURE);

    /* Program the requested space if different from Non-Secure */
    sp_base = qsmmu_bank_base(smmu_base, space);
    if (sp_base != ns_base) {
        qsmmu_program_bank(qts, sp_base, space);
    }
}

static uint32_t qsmmu_get_table_index(uint64_t addr, int level)
{
    switch (level) {
    case 0:
        return (addr >> 39) & 0x1ff;
    case 1:
        return (addr >> 30) & 0x1ff;
    case 2:
        return (addr >> 21) & 0x1ff;
    case 3:
        return (addr >> 12) & 0x1ff;
    default:
        g_assert_not_reached();
    }
}

static uint64_t qsmmu_get_table_addr(uint64_t base, int level, uint64_t iova)
{
    uint32_t index = qsmmu_get_table_index(iova, level);
    return (base & QSMMU_PTE_MASK) + (index * 8);
}

/*
 * qsmmu_get_pte_attrs - Calculate the S1 leaf PTE value
 *
 * IOMMU need to set different attributes for PTEs based on the translation mode
 */
static uint64_t qsmmu_get_pte_attrs(QSMMUTransMode mode, bool is_leaf,
                                    QSMMUSpace space)
{
    uint64_t rw_mask = QSMMU_LEAF_PTE_RW_MASK;
    uint64_t ro_mask = QSMMU_LEAF_PTE_RO_MASK;
    uint64_t non_leaf_mask = QSMMU_NON_LEAF_PTE_MASK;

    switch (space) {
    case QSMMU_SPACE_NONSECURE:
        break;
    default:
        g_assert_not_reached();
    }

    if (!is_leaf) {
        return non_leaf_mask;
    }

    /* For leaf PTE */
    if (mode == QSMMU_TM_NESTED || mode == QSMMU_TM_S1_ONLY) {
        return rw_mask;
    }

    return ro_mask;
}

/*
 * qsmmu_setup_s2_walk_for_ipa - Setup Stage 2 page table walk for an IPA
 *
 * @qts: QTest state handle
 * @space: Security space
 * @ipa: Intermediate Physical Address to translate
 * @s2_vttb: Stage 2 VTTB (page table base)
 * @mode: Translation mode
 * @is_final: Whether this is the final S2 walk (not nested within S1)
 *
 * Calculates and writes a 4-level Stage 2 page table walk for the given IPA.
 * This function dynamically generates and writes all page table entries
 * (L0-L3) to guest memory based on the input IPA and configuration.
 */
static void qsmmu_setup_s2_walk_for_ipa(QTestState *qts,
                                        QSMMUSpace space,
                                        uint64_t ipa,
                                        uint64_t s2_vttb,
                                        QSMMUTransMode mode,
                                        bool is_final)
{
    uint64_t all_s2_l0_pte_val;
    uint64_t all_s2_l1_pte_val;
    uint64_t all_s2_l2_pte_val;
    uint64_t all_s2_l3_pte_val;
    uint64_t s2_l0_addr, s2_l1_addr, s2_l2_addr, s2_l3_addr;

    /* Shared intermediate PTE values for all S2 walks */
    all_s2_l0_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L0_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));
    all_s2_l1_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L1_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));
    all_s2_l2_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L2_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));

    /* Stage 2 Level 0 */
    s2_l0_addr = qsmmu_get_table_addr(s2_vttb, 0, ipa);
    qtest_writeq(qts, s2_l0_addr, all_s2_l0_pte_val);

    /* Stage 2 Level 1 */
    s2_l1_addr = qsmmu_get_table_addr(all_s2_l0_pte_val, 1, ipa);
    qtest_writeq(qts, s2_l1_addr, all_s2_l1_pte_val);

    /* Stage 2 Level 2 */
    s2_l2_addr = qsmmu_get_table_addr(all_s2_l1_pte_val, 2, ipa);
    qtest_writeq(qts, s2_l2_addr, all_s2_l2_pte_val);

    /* Stage 2 Level 3 (leaf) */
    s2_l3_addr = qsmmu_get_table_addr(all_s2_l2_pte_val, 3, ipa);

    /*
     * Stage 2 L3 PTE attributes depend on the context:
     * - For nested S1 table address translations (!is_final):
     *   Use LEAF attrs (0x763) because these PTEs map S1 table pages directly
     * - For final S2 walk (is_final):
     *   Use TABLE attrs (0x7e3) for the final IPA→PA mapping
     */
    if (!is_final) {
        all_s2_l3_pte_val =
            (ipa & QSMMU_PTE_MASK) |
            qsmmu_get_pte_attrs(QSMMU_TM_NESTED, true, space);
    } else {
        all_s2_l3_pte_val =
            (ipa & QSMMU_PTE_MASK) |
            qsmmu_get_pte_attrs(QSMMU_TM_S2_ONLY, true, space);
    }

    qtest_writeq(qts, s2_l3_addr, all_s2_l3_pte_val);
}

/*
 * qsmmu_setup_s1_level_with_nested_s2 - Setup S1 level with nested S2 walk
 *
 * @qts: QTest state handle
 * @space: Security space
 * @s1_level: Stage 1 level (0-3)
 * @s1_pte_addr: Stage 1 PTE address (as IPA)
 * @s1_pte_val: Stage 1 PTE value to write
 * @s2_vttb: Stage 2 VTTB for nested translation
 * @mode: Translation mode
 *
 * For nested translation, each S1 table access requires a full S2 walk
 * to translate the S1 table's IPA to PA. This function performs the nested
 * S2 walk and writes the S1 PTE value to guest memory.
 */
static void qsmmu_setup_s1_level_with_nested_s2(QTestState *qts,
                                                QSMMUSpace space,
                                                int s1_level,
                                                uint64_t s1_pte_addr,
                                                uint64_t s1_pte_val,
                                                uint64_t s2_vttb,
                                                QSMMUTransMode mode)
{
    /*
     * Perform nested S2 walk to translate S1 table IPA to PA.
     * This is always needed for S1_ONLY/S2_ONLY/NESTED modes because:
     * - S1_ONLY: Needs S2 tables for "IPA as PA" mapping (for testing)
     * - S2_ONLY: Needs S2 tables for direct translation
     * - NESTED: Needs S2 tables for nested translation
     */
    qsmmu_setup_s2_walk_for_ipa(qts, space, s1_pte_addr,
                                s2_vttb, mode, false);

    /* Write the S1 PTE value */
    qtest_writeq(qts, s1_pte_addr, s1_pte_val);
}

/*
 * qsmmu_setup_translation_tables - Setup SMMU translation tables
 *
 * The 'SEC_SID' represents the input security state of the device/transaction,
 * whether it's a static Secure state or a dynamically-switched Realm state.
 * SEC_SID has been converted to the corresponding SEcurity Space (QSMMUSpace)
 * before calling this function.
 *
 * In a real SMMU translation, this input security state does not unilaterally
 * determine the output Physical Address (PA) space. The output PA space is
 * ultimately determined by attributes encountered during the page table walk,
 * such as NSCFG and NSTable.
 *
 * However, for the specific context of testing the SMMU with the iommu-testdev,
 * and to simplify the future support for Secure and Realm states, we adopt a
 * consistent policy:
 *
 * - We always ensure that the page table attributes (e.g., nscfg, nstable)
 * *match* the input 'SEC_SID' of the test case.
 *
 * For example: If 'SEC_SID' is Non-Secure, the corresponding nscfg and nstable
 * attributes in the translation tables will always be set to 1.
 *
 */
void qsmmu_setup_translation_tables(QTestState *qts,
                                    uint64_t iova,
                                    QSMMUSpace space,
                                    bool is_cd,
                                    QSMMUTransMode mode)
{
    uint64_t all_s2_l0_pte_val, all_s2_l1_pte_val, all_s2_l2_pte_val;
    uint64_t s1_vttb, s2_vttb, s1_leaf_pte_val;
    uint64_t l0_addr, l1_addr, l2_addr, l3_addr;

    g_test_message("Begin of construction: IOVA=0x%lx mode=%d is_building_CD=%s"
                   " ===", iova, mode, is_cd ? "yes" : "no");

    /* Initialize shared S2 PTE values used across all walks */
    all_s2_l0_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L0_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));
    all_s2_l1_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L1_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));
    all_s2_l2_pte_val = qsmmu_apply_space_offs(
        space, QSMMU_L2_PTE_VAL | qsmmu_get_pte_attrs(mode, false, space));

    /* Both S1 and S2 share the same VTTB base */
    s1_vttb = qsmmu_apply_space_offs(space, QSMMU_VTTB & QSMMU_PTE_MASK);
    s2_vttb = s1_vttb;

    if (!is_cd) {
        /*
         * Setup Stage 1 page tables with nested Stage 2 walks.
         * For each S1 level (L0-L3), we need to:
         * 1. Calculate S1 PTE address (as IPA)
         * 2. Perform nested S2 walk to translate that IPA to PA
         * 3. Write the S1 PTE value
         */

        /* Stage 1 Level 0 */
        l0_addr = qsmmu_get_table_addr(s1_vttb, 0, iova);
        qsmmu_setup_s1_level_with_nested_s2(qts, space, 0, l0_addr,
                                            all_s2_l0_pte_val, s2_vttb, mode);

        /* Stage 1 Level 1 */
        l1_addr = qsmmu_get_table_addr(all_s2_l0_pte_val & QSMMU_PTE_MASK,
                                       1, iova);
        qsmmu_setup_s1_level_with_nested_s2(qts, space, 1, l1_addr,
                                            all_s2_l1_pte_val, s2_vttb, mode);

        /* Stage 1 Level 2 */
        l2_addr = qsmmu_get_table_addr(all_s2_l1_pte_val & QSMMU_PTE_MASK,
                                       2, iova);
        qsmmu_setup_s1_level_with_nested_s2(qts, space, 2, l2_addr,
                                            all_s2_l2_pte_val, s2_vttb, mode);

        /* Stage 1 Level 3 (leaf) */
        l3_addr = qsmmu_get_table_addr(all_s2_l2_pte_val & QSMMU_PTE_MASK,
                                       3, iova);

        s1_leaf_pte_val = qsmmu_apply_space_offs(
            space, QSMMU_L3_PTE_VAL | qsmmu_get_pte_attrs(mode, true, space)
        );

        qsmmu_setup_s1_level_with_nested_s2(qts, space, 3, l3_addr,
                                            s1_leaf_pte_val, s2_vttb, mode);
    } else {
        /*
         * For CD address translation, we start directly with the IPA.
         */
        s1_leaf_pte_val = iova | qsmmu_get_pte_attrs(QSMMU_TM_NESTED,
                                                     false, space);
    }

    /*
     * Final Stage 2 walk: Translate the result from Stage 1.
     * - For S1_ONLY: This is skipped in hardware but we set it up for testing
     * - For S2_ONLY: This is the only walk
     * - For NESTED: This translates the IPA from S1 to final PA
     * - For CD address (is_cd=true): This is a table address, use !is_final
     */
    qsmmu_setup_s2_walk_for_ipa(qts, space, s1_leaf_pte_val, s2_vttb,
                                mode, !is_cd);

    /* Calculate and log final translated PA */
    g_test_message("End of construction: PA=0x%llx ===",
                   (s1_leaf_pte_val & QSMMU_PTE_MASK) + (iova & 0xfff));
}
