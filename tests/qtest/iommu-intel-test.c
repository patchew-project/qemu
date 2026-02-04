/*
 * QTest for Intel IOMMU (VT-d) with iommu-testdev
 *
 * This QTest file is used to test the Intel IOMMU with iommu-testdev so that
 * we can test VT-d without any guest kernel or firmware.
 *
 * Copyright (c) 2026 Fengyuan Yu <15fengyuan@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci_regs.h"
#include "hw/misc/iommu-testdev.h"
#include "libqos/qos-intel-iommu.h"

#define DMA_LEN           4

/* Test configurations for different Intel IOMMU modes */
static const QVTDTestConfig base_test_configs[] = {
    {
        .trans_mode = QVTD_TM_LEGACY_PT,
        .dma_iova = 0x10100000,  /* Use address in guest RAM range (inside 512MB) */
        .dma_pa = 0x10100000,
        .dma_len = DMA_LEN,
        .expected_result = 0,
        .domain_id = 1,
    },
    {
        .trans_mode = QVTD_TM_LEGACY_TRANS,
        .dma_iova = QVTD_TEST_IOVA,
        .dma_pa = QVTD_TEST_PA,
        .dma_len = DMA_LEN,
        .expected_result = 0,
        .domain_id = 1,
    },
};

static QPCIDevice *setup_qtest_pci_device(QTestState *qts, QPCIBus **pcibus,
                                          QPCIBar *bar)
{
    uint16_t vid, did;
    QPCIDevice *dev = NULL;
    int device_count = 0;

    *pcibus = qpci_new_pc(qts, NULL);
    g_assert(*pcibus != NULL);

    g_test_message("Scanning PCI bus for iommu-testdev (vendor:device = 0x%04x:0x%04x)...",
                   IOMMU_TESTDEV_VENDOR_ID, IOMMU_TESTDEV_DEVICE_ID);

    /* Find device by vendor/device ID to avoid slot surprises. */
    for (int s = 0; s < 32 && !dev; s++) {
        for (int fn = 0; fn < 8 && !dev; fn++) {
            QPCIDevice *cand = qpci_device_find(*pcibus, QPCI_DEVFN(s, fn));
            if (!cand) {
                continue;
            }
            vid = qpci_config_readw(cand, PCI_VENDOR_ID);
            did = qpci_config_readw(cand, PCI_DEVICE_ID);

            device_count++;
            g_test_message("  Found PCI device at %02x:%x - vendor:device = 0x%04x:0x%04x",
                           s, fn, vid, did);

            if (vid == IOMMU_TESTDEV_VENDOR_ID &&
                did == IOMMU_TESTDEV_DEVICE_ID) {
                dev = cand;
                g_test_message("Found iommu-testdev! devfn: 0x%x", cand->devfn);
            } else {
                g_free(cand);
            }
        }
    }

    if (!dev) {
        g_test_message("ERROR: iommu-testdev not found after scanning %d PCI devices", device_count);
        g_test_message("Expected vendor:device = 0x%04x:0x%04x (PCI_VENDOR_ID_REDHAT:PCI_DEVICE_ID_REDHAT_TEST)",
                       IOMMU_TESTDEV_VENDOR_ID, IOMMU_TESTDEV_DEVICE_ID);
        qpci_free_pc(*pcibus);
        *pcibus = NULL;
        g_test_skip("iommu-testdev not found on PCI bus - device may not be compiled or registered");
        return NULL;
    }

    /* Enable device - iommu-testdev only uses MMIO, not I/O ports */
    uint16_t cmd = qpci_config_readw(dev, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    qpci_config_writew(dev, PCI_COMMAND, cmd);

    *bar = qpci_iomap(dev, 0, NULL);
    g_assert_false(bar->is_io);

    return dev;
}

static void test_intel_iommu_translation(void)
{
    QTestState *qts;
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar bar;

    /* Initialize QEMU environment for Intel IOMMU testing */
    qts = qtest_init("-machine q35,kernel-irqchip=split "
                     "-accel tcg "
                     "-device intel-iommu,pt=on,aw-bits=48 "
                     "-device iommu-testdev,bus=pcie.0,addr=0x4 "
                     "-m 512");

    /* Setup and configure PCI device */
    dev = setup_qtest_pci_device(qts, &pcibus, &bar);
    if (!dev) {
        qtest_quit(qts);
        return;
    }

    /* Run the translation tests */
    g_test_message("### Starting Intel IOMMU translation tests...###");
    qvtd_translation_batch(base_test_configs, ARRAY_SIZE(base_test_configs),
                           qts, dev, bar, Q35_IOMMU_BASE);
    g_test_message("### Intel IOMMU translation tests completed successfully! ###");

    g_free(dev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/iommu-testdev/intel-translation", test_intel_iommu_translation);
    return g_test_run();
}
