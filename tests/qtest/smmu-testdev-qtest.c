/*
 * QTest for smmu-testdev
 *
 * This QTest file is used to test the smmu-testdev so that we can test SMMU
 * without any guest kernel or firmware.
 *
 * Copyright (c) 2025 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/generic-pcihost.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/smmu-testdev.h"

#define VIRT_SMMU_BASE    0x0000000009050000ULL
#define DMA_LEN           0x20U

static inline uint64_t smmu_bank_base(uint64_t base, SMMUTestDevSpace sp)
{
    /* Map only the Non-Secure bank for now; future domains may offset. */
    (void)sp;
    return base;
}

static uint32_t expected_dma_result(uint32_t mode,
                                    SMMUTestDevSpace s1_space,
                                    SMMUTestDevSpace s2_space)
{
    (void)mode;
    if (s1_space != STD_SPACE_NONSECURE || s2_space != STD_SPACE_NONSECURE) {
        return STD_DMA_ERR_TX_FAIL;
    }
    return 0u;
}

static void smmu_prog_bank(QTestState *qts, uint64_t B, SMMUTestDevSpace sp)
{
    g_assert_cmpuint(sp, ==, STD_SPACE_NONSECURE);
    /* Program minimal SMMUv3 state in a given control bank. */
    qtest_writel(qts, B + 0x0044, 0x80000000); /* GBPA UPDATE */
    qtest_writel(qts, B + 0x0020, 0x0);       /* CR0 */
    qtest_writel(qts, B + 0x0028, 0x0d75);    /* CR1 */
    {
        /* CMDQ_BASE: add address-space offset (S/NS/Root/Realm). */
        uint64_t v = 0x400000000e16b00aULL + std_space_offset(sp);
        qtest_writeq(qts, B + 0x0090, v);
    }
    qtest_writel(qts, B + 0x009c, 0x0);       /* CMDQ_CONS */
    qtest_writel(qts, B + 0x0098, 0x0);       /* CMDQ_PROD */
    {
        /* EVENTQ_BASE: add address-space offset (S/NS/Root/Realm). */
        uint64_t v = 0x400000000e17000aULL + std_space_offset(sp);
        qtest_writeq(qts, B + 0x00a0, v);
    }
    qtest_writel(qts, B + 0x00a8, 0x0);       /* EVENTQ_PROD */
    qtest_writel(qts, B + 0x00ac, 0x0);       /* EVENTQ_CONS */
    qtest_writel(qts, B + 0x0088, 0x5);       /* STRTAB_BASE_CFG */
    {
        /* STRTAB_BASE: add address-space offset (S/NS/Root/Realm). */
        uint64_t v = 0x400000000e179000ULL + std_space_offset(sp);
        qtest_writeq(qts, B + 0x0080, v);
    }
    qtest_writel(qts, B + 0x003C, 0x1);       /* INIT */
    qtest_writel(qts, B + 0x0020, 0xD);       /* CR0 */
}

static void smmu_prog_minimal(QTestState *qts, SMMUTestDevSpace space)
{
    /* Always program Non-Secure bank, then the requested space. */
    uint64_t ns_base = smmu_bank_base(VIRT_SMMU_BASE, STD_SPACE_NONSECURE);
    smmu_prog_bank(qts, ns_base, STD_SPACE_NONSECURE);

    uint64_t sp_base = smmu_bank_base(VIRT_SMMU_BASE, space);
    if (sp_base != ns_base) {
        smmu_prog_bank(qts, sp_base, space);
    }
}

static uint32_t poll_dma_result(QPCIDevice *dev, QPCIBar bar,
                                QTestState *qts)
{
    /* Trigger side effects (DMA) via REG_ID read once. */
    (void)qpci_io_readl(dev, bar, STD_REG_ID);

    /* Poll until not BUSY, then return the result. */
    for (int i = 0; i < 1000; i++) {
        uint32_t r = qpci_io_readl(dev, bar, STD_REG_DMA_RESULT);
        if (r != STD_DMA_RESULT_BUSY) {
            return r;
        }
        /* Small backoff to avoid busy spinning. */
        g_usleep(1000);
    }
    /* Timeout treated as failure-like non-zero. */
    return STD_DMA_RESULT_BUSY;
}

static void test_mmio_access(void)
{
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;
    uint8_t buf[DMA_LEN];
    uint32_t attr_ns;
    qts = qtest_init("-machine virt,acpi=off,gic-version=3,iommu=smmuv3 " \
                     "-display none -smp 1  -m 512 -cpu max -net none "
                     "-device smmu-testdev,device=0x0,function=0x1 ");

    qpci_init_generic(&gbus, qts, NULL, false);

    /* Find device by vendor/device ID to avoid slot surprises. */
    dev = NULL;
    for (int slot = 0; slot < 32 && !dev; slot++) {
        for (int fn = 0; fn < 8 && !dev; fn++) {
            QPCIDevice *cand = qpci_device_find(&gbus.bus,
                                               QPCI_DEVFN(slot, fn));
            if (!cand) {
                continue;
            }
            uint16_t vid = qpci_config_readw(cand, PCI_VENDOR_ID);
            uint16_t did = qpci_config_readw(cand, PCI_DEVICE_ID);
            if (vid == 0x1b36 && did == 0x0005) {
                dev = cand;
            } else {
                g_free(cand);
            }
        }
    }
    g_assert_nonnull(dev);

    qpci_device_enable(dev);
    bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar.is_io);

    /* Baseline attribute reads. */
    attr_ns = qpci_io_readl(dev, bar, STD_REG_ATTR_NS);
    g_assert_cmpuint(attr_ns, ==, 0x2);

    /* Program SMMU base and DMA parameters. */
    qpci_io_writel(dev, bar, STD_REG_SMMU_BASE_LO, (uint32_t)VIRT_SMMU_BASE);
    qpci_io_writel(dev, bar, STD_REG_SMMU_BASE_HI,
                   (uint32_t)(VIRT_SMMU_BASE >> 32));
    qpci_io_writel(dev, bar, STD_REG_DMA_IOVA_LO, (uint32_t)STD_IOVA);
    qpci_io_writel(dev, bar, STD_REG_DMA_IOVA_HI,
                   (uint32_t)(STD_IOVA >> 32));
    qpci_io_writel(dev, bar, STD_REG_DMA_LEN, DMA_LEN);
    qpci_io_writel(dev, bar, STD_REG_DMA_DIR, 0); /* device -> host */

    qtest_memset(qts, STD_IOVA, 0x00, DMA_LEN);
    qtest_memread(qts, STD_IOVA, buf, DMA_LEN);

    /* Refresh attrs via write to ensure legacy functionality still works. */
    qpci_io_writel(dev, bar, STD_REG_ID, 0x1);
    /*
     * invoke translation builder for multiple
     * stage/security-space combinations (readable/refactored).
     */
    const uint32_t modes[] = { 0u, 1u, 2u }; /* Stage1, Stage2, Nested stage */
    const SMMUTestDevSpace spaces[] = { STD_SPACE_NONSECURE };
    /* Use attrs-DMA path for end-to-end */
    qpci_io_writel(dev, bar, STD_REG_DMA_MODE, 1);
    for (size_t mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
        const SMMUTestDevSpace *s1_set = NULL;
        size_t s1_count = 0;
        const SMMUTestDevSpace *s2_set = NULL;
        size_t s2_count = 0;

        switch (modes[mi]) {
        case 0u:
        case 1u:
        case 2u:
            s1_set = spaces;
            s1_count = sizeof(spaces) / sizeof(spaces[0]);
            s2_set = spaces;
            s2_count = sizeof(spaces) / sizeof(spaces[0]);
            break;
        default:
            g_assert_not_reached();
        }

        for (size_t si = 0; si < s1_count; si++) {
            for (size_t sj = 0; sj < s2_count; sj++) {
                qpci_io_writel(dev, bar, STD_REG_TRANS_MODE, modes[mi]);
                qpci_io_writel(dev, bar, STD_REG_S1_SPACE, s1_set[si]);
                qpci_io_writel(dev, bar, STD_REG_S2_SPACE, s2_set[sj]);
                qpci_io_writel(dev, bar, STD_REG_TRANS_DBELL, 0x2);
                qpci_io_writel(dev, bar, STD_REG_TRANS_DBELL, 0x1);

                uint32_t st = qpci_io_readl(dev, bar,
                                            STD_REG_TRANS_STATUS);
                g_test_message("build: stage=%s s1=%s s2=%s status=0x%x",
                                std_mode_to_str(modes[mi]),
                                std_space_to_str(s1_set[si]),
                                std_space_to_str(s2_set[sj]), st);
                /* Program SMMU registers in selected control bank. */
                smmu_prog_minimal(qts, s1_set[si]);

                /* End-to-end DMA using tx_space per mode. */
                SMMUTestDevSpace tx_space =
                    (modes[mi] == 0u) ? s1_set[si] : s2_set[sj];
                uint32_t dma_attrs = ((uint32_t)tx_space << 1);
                qpci_io_writel(dev, bar, STD_REG_DMA_ATTRS,
                                dma_attrs);
                qpci_io_writel(dev, bar, STD_REG_DMA_DBELL, 1);
                /* Wait for DMA completion and assert success. */
                {
                    uint32_t dr = poll_dma_result(dev, bar, qts);
                    uint32_t exp = expected_dma_result(modes[mi],
                                                        spaces[si],
                                                        spaces[sj]);
                    g_assert_cmpuint(dr, ==, exp);
                    g_test_message("polling end. attrs=0x%x res=0x%x",
                                   dma_attrs, dr);
                }
                /* Clear CD/STE/PTE built by the device for next round. */
                qpci_io_writel(dev, bar, STD_REG_TRANS_CLEAR, 1);
                g_test_message("clear cache end.");
            }
        }
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/smmu-testdev/mmio", test_mmio_access);
    return g_test_run();
}
