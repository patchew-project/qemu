/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Private RISC-V IOMMU MSI trap callback
 *
 * Copyright (C) 2021-2023, Rivos Inc.
 */

/* Shared privately with the deterministic trap callback unit tests. */

/*
 * Translations matching MSI pattern check are redirected to "riscv-iommu-trap"
 * memory region as untranslated address, for additional MSI/MRIF interception
 * by IOMMU interrupt remapping implementation.
 * Note: Device emulation code generating an MSI is expected to provide a valid
 * memory transaction attributes with requested_id set.
 */
static MemTxResult riscv_iommu_trap_write(void *opaque, hwaddr addr,
    uint64_t data, unsigned size, MemTxAttrs attrs)
{
    RISCVIOMMUState *s = opaque;
    RISCVIOMMUContext *ctx;
    MemTxResult res;
    void *ref;
    uint32_t devid = attrs.requester_id;
    IOMMUTLBEntry iotlb = {
        .iova = addr,
        .target_as = s->target_as,
        .addr_mask = ~0ULL,
        .perm = IOMMU_WO,
    };
    int cause;

    if (attrs.unspecified) {
        return MEMTX_ACCESS_ERROR;
    }

    /* FIXME: PCIe bus remapping for attached endpoints. */
    devid |= s->bus << 8;

    ctx = riscv_iommu_ctx(s, devid, attrs.pid, IOMMU_WO, addr, &ref);
    if (ctx == NULL) {
        res = MEMTX_ACCESS_ERROR;
    } else {
        /*
         * The trap address is the original IOVA. Resolve the MSI GPA
         * without consulting or updating the translation cache.
         * Exclude this internal replay from the modeled HPM events.
         */
        cause = riscv_iommu_spa_fetch(s, ctx, &iotlb, false);
        if (cause) {
            riscv_iommu_report_fault(s, ctx,
                RISCV_IOMMU_FQ_TTYPE_UADDR_WR, cause,
                !!(ctx->tc & RISCV_IOMMU_DC_TC_PDTV),
                addr, iotlb.translated_addr);
            res = MEMTX_ACCESS_ERROR;
        } else if (iotlb.target_as == &s->trap_as) {
            res = riscv_iommu_msi_write(s, ctx, addr,
                                       iotlb.translated_addr, data,
                                       size, attrs);
        } else {
            /* The mapping may have changed since the original walk. */
            data = cpu_to_le64(data);
            res = dma_memory_write(iotlb.target_as, iotlb.translated_addr,
                                   &data, size, attrs);
        }
    }
    riscv_iommu_ctx_put(s, ref);
    return res;
}
