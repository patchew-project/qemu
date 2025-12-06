/*
 * QTest for SMMUv3 with iommu-testdev
 *
 * This QTest file is used to test the SMMUv3 with iommu-testdev so that we can
 * test SMMUv3 without any guest kernel or firmware.
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
#include "hw/misc/iommu-testdev.h"
#include "libqos/qos-smmuv3.h"

#define DMA_LEN           4

static QPCIDevice *setup_qtest_pci_device(QTestState *qts, QGenericPCIBus *gbus,
                                          QPCIBar *bar)
{
    uint16_t vid, did;
    QPCIDevice *dev = NULL;

    qpci_init_generic(gbus, qts, NULL, false);

    /* Find device by vendor/device ID to avoid slot surprises. */
    for (int s = 0; s < 32 && !dev; s++) {
        for (int fn = 0; fn < 8 && !dev; fn++) {
            QPCIDevice *cand = qpci_device_find(&gbus->bus, QPCI_DEVFN(s, fn));
            if (!cand) {
                continue;
            }
            vid = qpci_config_readw(cand, PCI_VENDOR_ID);
            did = qpci_config_readw(cand, PCI_DEVICE_ID);
            if (vid == IOMMU_TESTDEV_VENDOR_ID &&
                did == IOMMU_TESTDEV_DEVICE_ID) {
                dev = cand;
                g_test_message("Found iommu-testdev! devfn: 0x%x", cand->devfn);
            } else {
                g_free(cand);
            }
        }
    }
    g_assert(dev);

    qpci_device_enable(dev);
    *bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar->is_io);

    return dev;
}

static void run_smmuv3_translation(const QSMMUTestConfig *cfg)
{
    QTestState *qts;
    QGenericPCIBus gbus;
    QPCIDevice *dev;
    QPCIBar bar;

    /* Initialize QEMU environment for SMMU testing */
    qts = qtest_init("-machine virt,acpi=off,gic-version=3,iommu=smmuv3 "
                     "-smp 1 -m 512 -cpu max -net none "
                     "-device iommu-testdev");

    /* Setup and configure PCI device */
    dev = setup_qtest_pci_device(qts, &gbus, &bar);
    g_assert(dev);

    g_test_message("### SMMUv3 translation mode=%d sec_sid=%d ###",
                   cfg->trans_mode, cfg->sec_sid);
    qsmmu_run_translation_case(qts, dev, bar, VIRT_SMMU_BASE, cfg);
    qtest_quit(qts);
}

static void test_smmuv3_ns_s1_only(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_S1_ONLY,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_iova = QSMMU_IOVA_OR_IPA,
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

static void test_smmuv3_ns_s2_only(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_S2_ONLY,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_iova = QSMMU_IOVA_OR_IPA,
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

static void test_smmuv3_ns_nested(void)
{
    QSMMUTestConfig cfg = {
        .trans_mode = QSMMU_TM_NESTED,
        .sec_sid = QSMMU_SEC_SID_NONSECURE,
        .dma_iova = QSMMU_IOVA_OR_IPA,
        .dma_len = DMA_LEN,
        .expected_result = 0,
    };

    run_smmuv3_translation(&cfg);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/iommu-testdev/translation/ns-s1-only",
                   test_smmuv3_ns_s1_only);
    qtest_add_func("/iommu-testdev/translation/ns-s2-only",
                   test_smmuv3_ns_s2_only);
    qtest_add_func("/iommu-testdev/translation/ns-nested",
                   test_smmuv3_ns_nested);
    return g_test_run();
}
