/*
 * QOS Intel IOMMU (VT-d) Module Implementation
 *
 * This module provides Intel IOMMU-specific helper functions for libqos tests.
 *
 * Copyright (c) 2026 Fengyuan Yu <15fengyuan@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "pci.h"
#include "qos-intel-iommu.h"

#define QVTD_POLL_DELAY_US        1000
#define QVTD_POLL_MAX_RETRIES     1000
#define QVTD_AW_48BIT_ENCODING    2

/*
 * iommu-testdev DMA attribute layout for Intel VT-d traffic.
 *
 * Bits [2:0] keep using the generic iommu-testdev encoding
 * (secure + ArmSecuritySpace). Bits [23:8] carry the PCI Requester ID in the
 * format defined in the Intel VT-d spec (Figure 3-2 in
 * spec/Intel-iommu-spec.txt), and bits [31:24] contain the PASID that tags
 * scalable-mode transactions. Bit 4 distinguishes between pure legacy RID
 * requests and scalable-mode PASID-tagged requests. The PASID field is
 * limited to 8 bits because MemTxAttrs::pid only carries 8 bits today (see
 * include/exec/memattrs.h and the VTD_ECAP_PSS limit in
 * hw/i386/intel_iommu_internal.h).
 */
#define QVTD_DMA_ATTR_MODE_SHIFT      4
#define QVTD_DMA_ATTR_MODE_MASK       0x1
#define QVTD_DMA_ATTR_MODE_LEGACY     0
#define QVTD_DMA_ATTR_MODE_SCALABLE   1
#define QVTD_DMA_ATTR_RID_SHIFT       8
#define QVTD_DMA_ATTR_RID_MASK        0xffffu
#define QVTD_DMA_ATTR_PASID_BITS      8
#define QVTD_DMA_ATTR_PASID_SHIFT     24
#define QVTD_DMA_ATTR_PASID_MASK      ((1u << QVTD_DMA_ATTR_PASID_BITS) - 1)

#define QVTD_PCI_FUNCS_PER_DEVICE     8
#define QVTD_PCI_DEVS_PER_BUS         32

static void qvtd_wait_for_bitsl(QTestState *qts, uint64_t addr,
                                uint32_t mask, bool expect_set)
{
    uint32_t val = 0;

    for (int attempt = 0; attempt < QVTD_POLL_MAX_RETRIES; attempt++) {
        val = qtest_readl(qts, addr);
        if (!!(val & mask) == expect_set) {
            return;
        }
        g_usleep(QVTD_POLL_DELAY_US);
    }

    g_error("Timeout waiting for bits 0x%x (%s) at 0x%llx, last=0x%x",
            mask, expect_set ? "set" : "clear",
            (unsigned long long)addr, val);
}

static void qvtd_wait_for_bitsq(QTestState *qts, uint64_t addr,
                                uint64_t mask, bool expect_set)
{
    uint64_t val = 0;

    for (int attempt = 0; attempt < QVTD_POLL_MAX_RETRIES; attempt++) {
        val = qtest_readq(qts, addr);
        if (!!(val & mask) == expect_set) {
            return;
        }
        g_usleep(QVTD_POLL_DELAY_US);
    }

    g_error("Timeout waiting for bits 0x%llx (%s) at 0x%llx, last=0x%llx",
            (unsigned long long)mask, expect_set ? "set" : "clear",
            (unsigned long long)addr, (unsigned long long)val);
}

static uint16_t qvtd_calc_sid(const QPCIDevice *dev)
{
    uint16_t devfn = dev->devfn & 0xff;
    uint16_t bus = (dev->devfn >> 8) & 0xff;
    uint8_t device = (devfn >> 3) & 0x1f;
    uint8_t function = devfn & 0x7;

    /* Validate BDF components. */
    if (device >= QVTD_PCI_DEVS_PER_BUS || function >= QVTD_PCI_FUNCS_PER_DEVICE) {
        g_error("Invalid BDF: bus=%u device=%u function=%u", bus, device, function);
    }

    return (bus << 8) | devfn;
}

static bool qvtd_validate_dma_memory(QVTDTestContext *ctx)
{
    uint32_t len = ctx->config.dma_len;
    g_autofree uint8_t *buf = NULL;

    if (!len) {
        return true;
    }

    buf = g_malloc(len);
    qtest_memread(ctx->qts, ctx->config.dma_pa, buf, len);

    for (uint32_t i = 0; i < len; i++) {
        uint8_t expected = (ITD_DMA_WRITE_VAL >> ((i % 4) * 8)) & 0xff;
        if (buf[i] != expected) {
            g_test_message("Memory mismatch at PA=0x%llx offset=%u "
                           "expected=0x%02x actual=0x%02x",
                           (unsigned long long)ctx->config.dma_pa, i,
                           expected, buf[i]);
            return false;
        }
    }

    return true;
}

uint32_t qvtd_expected_dma_result(QVTDTestContext *ctx)
{
    return ctx->config.expected_result;
}

uint32_t qvtd_build_dma_attrs(uint16_t bdf, uint32_t pasid)
{
    uint32_t attrs = 0;
    uint8_t bus = (bdf >> 8) & 0xff;
    uint8_t devfn = bdf & 0xff;
    uint8_t device = devfn >> 3;
    uint8_t function = devfn & 0x7;
    bool scalable_mode = pasid != 0;

    if (device >= QVTD_PCI_DEVS_PER_BUS || function >= QVTD_PCI_FUNCS_PER_DEVICE) {
        g_error("Invalid requester-id 0x%04x (bus=%u device=%u function=%u)",
                bdf, bus, device, function);
    }

    attrs = ITD_ATTRS_SET_SECURE(attrs, 0);
    attrs = ITD_ATTRS_SET_SPACE(attrs, 0);
    attrs |= ((uint32_t)bdf & QVTD_DMA_ATTR_RID_MASK) << QVTD_DMA_ATTR_RID_SHIFT;

    if (scalable_mode) {
        if (pasid > QVTD_DMA_ATTR_PASID_MASK) {
            g_error("PASID 0x%x exceeds %u-bit limit imposed by MemTxAttrs",
                    pasid, QVTD_DMA_ATTR_PASID_BITS);
        }

        attrs |= (QVTD_DMA_ATTR_MODE_SCALABLE << QVTD_DMA_ATTR_MODE_SHIFT);
        attrs |= ((pasid & QVTD_DMA_ATTR_PASID_MASK) << QVTD_DMA_ATTR_PASID_SHIFT);
    } else {
        attrs |= (QVTD_DMA_ATTR_MODE_LEGACY << QVTD_DMA_ATTR_MODE_SHIFT);
    }

    return attrs;
}

static void qvtd_build_root_entry(QTestState *qts, uint8_t bus,
                                  uint64_t context_table_ptr)
{
    uint64_t root_entry_addr = QVTD_ROOT_TABLE_BASE + (bus * 16);
    uint64_t lo, hi;

    /* Root Entry Low: Context Table Pointer + Present bit (VT-d spec Section 9.1). */
    lo = (context_table_ptr & VTD_CONTEXT_ENTRY_SLPTPTR) | VTD_CONTEXT_ENTRY_P;
    hi = 0;  /* Reserved. */

    qtest_writeq(qts, root_entry_addr, lo);
    qtest_writeq(qts, root_entry_addr + 8, hi);
}

static void qvtd_build_context_entry(QTestState *qts, uint16_t sid,
                                     QVTDTransMode mode, uint16_t domain_id,
                                     uint64_t slptptr)
{
    uint8_t devfn = sid & 0xff;
    uint64_t context_entry_addr = QVTD_CONTEXT_TABLE_BASE + (devfn * 16);
    uint64_t lo, hi;

    if (mode == QVTD_TM_LEGACY_PT) {
        /* Pass-through mode (VT-d spec Section 3.9, Section 9.3). */
        lo = VTD_CONTEXT_ENTRY_P | VTD_CONTEXT_TT_PASS_THROUGH;
        hi = ((uint64_t)domain_id << 8) | QVTD_AW_48BIT_ENCODING;
    } else {
        /* Translated mode: 4-level paging (AW=2 for 48-bit, VT-d spec Section 9.3). */
        lo = VTD_CONTEXT_ENTRY_P | VTD_CONTEXT_TT_MULTI_LEVEL |
             (slptptr & VTD_CONTEXT_ENTRY_SLPTPTR);
        hi = ((uint64_t)domain_id << 8) | QVTD_AW_48BIT_ENCODING;
    }

    qtest_writeq(qts, context_entry_addr, lo);
    qtest_writeq(qts, context_entry_addr + 8, hi);
}

void qvtd_setup_translation_tables(QTestState *qts, uint64_t iova,
                                   uint64_t pa, QVTDTransMode mode)
{
    uint64_t pml4_entry, pdpt_entry, pd_entry, pt_entry;
    uint64_t pml4_addr, pdpt_addr, pd_addr, pt_addr;
    uint32_t pml4_idx, pdpt_idx, pd_idx, pt_idx;
    const char *mode_str = (mode == QVTD_TM_LEGACY_PT) ?
                           "Pass-Through" : "Translated";

    g_test_message("Begin of page table construction: IOVA=0x%llx PA=0x%llx mode=%s",
                   (unsigned long long)iova, (unsigned long long)pa, mode_str);

    /* Pass-through mode doesn't need page tables */
    if (mode == QVTD_TM_LEGACY_PT) {
        g_test_message("Pass-through mode: skipping page table setup");
        return;
    }

    /* Extract indices from IOVA
     * 4-level paging for 48-bit virtual address space:
     * - PML4 index: bits [47:39] (9 bits = 512 entries)
     * - PDPT index:  bits [38:30] (9 bits = 512 entries)
     * - PD index:    bits [29:21] (9 bits = 512 entries)
     * - PT index:    bits [20:12] (9 bits = 512 entries)
     * - Page offset: bits [11:0]  (12 bits = 4KB pages)
     */
    pml4_idx = (iova >> 39) & 0x1ff;  /* Bits [47:39] */
    pdpt_idx = (iova >> 30) & 0x1ff;  /* Bits [38:30] */
    pd_idx = (iova >> 21) & 0x1ff;    /* Bits [29:21] */
    pt_idx = (iova >> 12) & 0x1ff;    /* Bits [20:12] */

    /*
     * Build 4-level page table hierarchy (VT-d spec Section 9.3, Table 9-3).
     * Non-leaf entries: both R+W set for full access (spec allows R or W individually).
     * Per VT-d spec Section 9.8: "If either the R or W field of a non-leaf
     * paging-structure entry is 1", indicating that setting one or both is valid.
     * We set both R+W for non-leaf entries as standard practice.
     */

    /* PML4 Entry: points to PDPT. */
    pml4_addr = QVTD_PT_L4_BASE + (pml4_idx * 8);
    pml4_entry = QVTD_PT_L3_BASE | VTD_SL_R | VTD_SL_W;
    qtest_writeq(qts, pml4_addr, pml4_entry);

    /* PDPT Entry: points to PD. */
    pdpt_addr = QVTD_PT_L3_BASE + (pdpt_idx * 8);
    pdpt_entry = QVTD_PT_L2_BASE | VTD_SL_R | VTD_SL_W;
    qtest_writeq(qts, pdpt_addr, pdpt_entry);

    /* PD Entry: points to PT. */
    pd_addr = QVTD_PT_L2_BASE + (pd_idx * 8);
    pd_entry = QVTD_PT_L1_BASE | VTD_SL_R | VTD_SL_W;
    qtest_writeq(qts, pd_addr, pd_entry);

    /* PT Entry: points to physical page (leaf). */
    pt_addr = QVTD_PT_L1_BASE + (pt_idx * 8);
    pt_entry = (pa & VTD_PAGE_MASK_4K) | VTD_SL_R | VTD_SL_W;
    qtest_writeq(qts, pt_addr, pt_entry);

    g_test_message("End of page table construction: mapped IOVA=0x%llx -> PA=0x%llx",
                   (unsigned long long)iova, (unsigned long long)pa);
}

static void qvtd_invalidate_context_cache(QTestState *qts,
                                          uint64_t iommu_base)
{
    uint64_t ccmd_val;

    /* Context Command Register: Global invalidation (VT-d spec Section 6.5.1.1). */
    ccmd_val = VTD_CCMD_ICC | VTD_CCMD_GLOBAL_INVL;
    qtest_writeq(qts, iommu_base + DMAR_CCMD_REG, ccmd_val);

    /* Wait for ICC bit to clear. */
    qvtd_wait_for_bitsq(qts, iommu_base + DMAR_CCMD_REG,
                        VTD_CCMD_ICC, false);
}

static void qvtd_invalidate_iotlb(QTestState *qts, uint64_t iommu_base)
{
    uint64_t iotlb_val;

    /* IOTLB Invalidate Register: Global flush (VT-d spec Section 6.5.1.2). */
    iotlb_val = VTD_TLB_IVT | VTD_TLB_GLOBAL_FLUSH;
    qtest_writeq(qts, iommu_base + DMAR_IOTLB_REG, iotlb_val);

    /* Wait for IVT bit to clear. */
    qvtd_wait_for_bitsq(qts, iommu_base + DMAR_IOTLB_REG,
                        VTD_TLB_IVT, false);
}

static void qvtd_clear_memory_regions(QTestState *qts)
{
    /* Clear root table. */
    qtest_memset(qts, QVTD_ROOT_TABLE_BASE, 0, 4096);

    /* Clear context table. */
    qtest_memset(qts, QVTD_CONTEXT_TABLE_BASE, 0, 4096);

    /* Clear all page table levels (4 levels * 4KB each = 16KB). */
    qtest_memset(qts, QVTD_PT_L4_BASE, 0, 16384);
}

void qvtd_program_regs(QTestState *qts, uint64_t iommu_base)
{
    uint32_t gcmd;

    /* 1. Disable translation (VT-d spec Section 11.4.4). */
    gcmd = qtest_readl(qts, iommu_base + DMAR_GCMD_REG);
    gcmd &= ~VTD_GCMD_TE;
    qtest_writel(qts, iommu_base + DMAR_GCMD_REG, gcmd);

    /* Wait for TES to clear. */
    qvtd_wait_for_bitsl(qts, iommu_base + DMAR_GSTS_REG,
                        VTD_GSTS_TES, false);

    /* 2. Program root table address (VT-d spec Section 11.4.5). */
    qtest_writeq(qts, iommu_base + DMAR_RTADDR_REG, QVTD_ROOT_TABLE_BASE);

    /* 3. Set root table pointer (VT-d spec Section 6.6). */
    gcmd = qtest_readl(qts, iommu_base + DMAR_GCMD_REG);
    gcmd |= VTD_GCMD_SRTP;
    qtest_writel(qts, iommu_base + DMAR_GCMD_REG, gcmd);

    /* Wait for RTPS. */
    qvtd_wait_for_bitsl(qts, iommu_base + DMAR_GSTS_REG,
                        VTD_GSTS_RTPS, true);

    /* Invalidate context cache after setting root table pointer. */
    qvtd_invalidate_context_cache(qts, iommu_base);

    /* 4. Unmask fault event interrupt to avoid warning messages. */
    qtest_writel(qts, iommu_base + DMAR_FECTL_REG, 0);

    /* NOTE: Translation is NOT enabled here - caller must enable after building structures. */
}

uint32_t qvtd_build_translation(QTestState *qts, QVTDTransMode mode,
                                uint16_t sid, uint16_t domain_id,
                                uint64_t iova, uint64_t pa)
{
    uint8_t bus = (sid >> 8) & 0xff;
    const char *mode_str = (mode == QVTD_TM_LEGACY_PT) ?
                           "Pass-Through" : "Translated";

    g_test_message("Begin of construction: IOVA=0x%llx PA=0x%llx "
                   "mode=%s domain_id=%u ===",
                   (unsigned long long)iova, (unsigned long long)pa,
                   mode_str, domain_id);

    /* Build root entry */
    qvtd_build_root_entry(qts, bus, QVTD_CONTEXT_TABLE_BASE);

    /* Build context entry */
    if (mode == QVTD_TM_LEGACY_PT) {
        /* Pass-through mode: no page tables needed */
        qvtd_build_context_entry(qts, sid, mode, domain_id, 0);
        g_test_message("End of construction: identity mapping to PA=0x%llx ===",
                       (unsigned long long)pa);
    } else {
        /* Translated mode: build 4-level page tables */
        qvtd_setup_translation_tables(qts, iova, pa, QVTD_TM_LEGACY_TRANS);
        qvtd_build_context_entry(qts, sid, mode, domain_id, QVTD_PT_L4_BASE);
        g_test_message("End of construction: mapped IOVA=0x%llx -> PA=0x%llx ===",
                       (unsigned long long)iova, (unsigned long long)pa);
    }

    return 0;
}

uint32_t qvtd_setup_and_enable_translation(QVTDTestContext *ctx)
{
    uint32_t gcmd;

    /* Clear memory regions once during setup */
    qvtd_clear_memory_regions(ctx->qts);

    /* Program IOMMU registers (sets up root table pointer) */
    qvtd_program_regs(ctx->qts, ctx->iommu_base);

    /* Build translation structures AFTER clearing memory */
    ctx->trans_status = qvtd_build_translation(ctx->qts, ctx->config.trans_mode,
                                               ctx->sid, ctx->config.domain_id,
                                               ctx->config.dma_iova,
                                               ctx->config.dma_pa);
    if (ctx->trans_status != 0) {
        return ctx->trans_status;
    }

    /* Invalidate caches using register-based invalidation */
    qvtd_invalidate_context_cache(ctx->qts, ctx->iommu_base);
    qvtd_invalidate_iotlb(ctx->qts, ctx->iommu_base);

    /* Enable translation AFTER building structures and invalidating caches */
    gcmd = qtest_readl(ctx->qts, ctx->iommu_base + DMAR_GCMD_REG);
    gcmd |= VTD_GCMD_TE;
    qtest_writel(ctx->qts, ctx->iommu_base + DMAR_GCMD_REG, gcmd);

    /* Wait for TES */
    qvtd_wait_for_bitsl(ctx->qts, ctx->iommu_base + DMAR_GSTS_REG,
                        VTD_GSTS_TES, true);

    return 0;
}

uint32_t qvtd_trigger_dma(QVTDTestContext *ctx)
{
    uint64_t iova = ctx->config.dma_iova;
    uint32_t len = ctx->config.dma_len;
    uint32_t result, attrs_val;
    const char *mode_str = (ctx->config.trans_mode == QVTD_TM_LEGACY_PT) ?
                           "Pass-Through" : "Translated";

    /* Write IOVA low 32 bits */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_GVA_LO, (uint32_t)iova);

    /* Write IOVA high 32 bits */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_GVA_HI, (uint32_t)(iova >> 32));

    /* Write DMA length */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_LEN, len);

    /* Build and write DMA attributes with BDF (PASID=0 for Legacy mode) */
    attrs_val = qvtd_build_dma_attrs(ctx->sid, 0);
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_ATTRS, attrs_val);

    /* Arm DMA by writing 1 to doorbell */
    qpci_io_writel(ctx->dev, ctx->bar, ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);

    /* Trigger DMA by reading from triggering register */
    qpci_io_readl(ctx->dev, ctx->bar, ITD_REG_DMA_TRIGGERING);

    /* Poll for completion */
    ctx->dma_result = ITD_DMA_RESULT_BUSY;
    for (int attempt = 0; attempt < QVTD_POLL_MAX_RETRIES; attempt++) {
        result = qpci_io_readl(ctx->dev, ctx->bar, ITD_REG_DMA_RESULT);
        if (result != ITD_DMA_RESULT_BUSY) {
            ctx->dma_result = result;
            break;
        }
        g_usleep(QVTD_POLL_DELAY_US);
    }

    if (ctx->dma_result == ITD_DMA_RESULT_BUSY) {
        ctx->dma_result = ITD_DMA_ERR_TX_FAIL;
        g_test_message("-> DMA timeout detected, forcing failure");
    }

    if (ctx->dma_result == 0) {
        g_test_message("-> DMA succeeded: mode=%s", mode_str);
    } else {
        g_test_message("-> DMA failed: mode=%s result=0x%x",
                       mode_str, ctx->dma_result);
    }

    return ctx->dma_result;
}

void qvtd_cleanup_translation(QVTDTestContext *ctx)
{
    uint8_t bus = (ctx->sid >> 8) & 0xff;
    uint8_t devfn = ctx->sid & 0xff;
    uint64_t root_entry_addr = QVTD_ROOT_TABLE_BASE + (bus * 16);
    uint64_t context_entry_addr = QVTD_CONTEXT_TABLE_BASE + (devfn * 16);
    uint32_t gcmd;

    /* Disable translation before tearing down the structures */
    gcmd = qtest_readl(ctx->qts, ctx->iommu_base + DMAR_GCMD_REG);
    if (gcmd & VTD_GCMD_TE) {
        gcmd &= ~VTD_GCMD_TE;
        qtest_writel(ctx->qts, ctx->iommu_base + DMAR_GCMD_REG, gcmd);
        qvtd_wait_for_bitsl(ctx->qts, ctx->iommu_base + DMAR_GSTS_REG,
                            VTD_GSTS_TES, false);
    }

    /* Clear context entry */
    qtest_writeq(ctx->qts, context_entry_addr, 0);
    qtest_writeq(ctx->qts, context_entry_addr + 8, 0);

    /* Clear root entry */
    qtest_writeq(ctx->qts, root_entry_addr, 0);
    qtest_writeq(ctx->qts, root_entry_addr + 8, 0);

    /* Invalidate caches using register-based invalidation */
    qvtd_invalidate_context_cache(ctx->qts, ctx->iommu_base);
    qvtd_invalidate_iotlb(ctx->qts, ctx->iommu_base);
}

bool qvtd_validate_test_result(QVTDTestContext *ctx)
{
    uint32_t expected = qvtd_expected_dma_result(ctx);
    bool passed = (ctx->dma_result == expected);
    bool mem_ok = true;

    g_test_message("-> Validating result: expected=0x%x actual=0x%x",
                   expected, ctx->dma_result);

    if (passed && expected == 0) {
        mem_ok = qvtd_validate_dma_memory(ctx);
        g_test_message("-> Memory validation %s at PA=0x%llx",
                       mem_ok ? "passed" : "failed",
                       (unsigned long long)ctx->config.dma_pa);
        passed = mem_ok;
    }

    return passed;
}

void qvtd_single_translation(QVTDTestContext *ctx)
{
    uint32_t config_result;
    bool test_passed;

    /* Configure Intel IOMMU translation */
    config_result = qvtd_setup_and_enable_translation(ctx);
    if (config_result != 0) {
        g_test_message("Configuration failed: mode=%u status=0x%x",
                       ctx->config.trans_mode, config_result);
    }
    g_assert_cmpint(config_result, ==, 0);

    /* Trigger DMA operation */
    qvtd_trigger_dma(ctx);

    /* Validate test result */
    test_passed = qvtd_validate_test_result(ctx);
    g_assert_true(test_passed);

    /* Clean up translation state to prepare for the next test */
    qvtd_cleanup_translation(ctx);
}

void qvtd_run_translation_case(QTestState *qts, QPCIDevice *dev,
                               QPCIBar bar, uint64_t iommu_base,
                               const QVTDTestConfig *cfg)
{
    /* Initialize test memory */
    qtest_memset(qts, cfg->dma_pa, 0x00, cfg->dma_len);

    /* Create test context on stack */
    QVTDTestContext ctx = {
        .qts = qts,
        .dev = dev,
        .bar = bar,
        .iommu_base = iommu_base,
        .config = *cfg,
        .trans_status = 0,
        .dma_result = 0,
        .sid = qvtd_calc_sid(dev),
    };

    /* Execute the test using existing single_translation logic */
    qvtd_single_translation(&ctx);

    /* Report results */
    g_test_message("--> Test completed: mode=%u domain_id=%u "
                   "status=0x%x result=0x%x",
                   cfg->trans_mode, cfg->domain_id,
                   ctx.trans_status, ctx.dma_result);
}

void qvtd_translation_batch(const QVTDTestConfig *configs, size_t count,
                            QTestState *qts, QPCIDevice *dev,
                            QPCIBar bar, uint64_t iommu_base)
{
    for (size_t i = 0; i < count; i++) {
        g_test_message("=== Running test %zu/%zu ===", i + 1, count);
        qvtd_run_translation_case(qts, dev, bar, iommu_base, &configs[i]);
    }
}
