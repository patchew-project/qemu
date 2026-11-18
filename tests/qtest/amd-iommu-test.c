/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/i386/amd_iommu.h"

#define CMDBUF_ADDR       0x200000
#define CMDBUF_LEN_FIELD  8
#define CMDBUF_ENTRIES    (1U << CMDBUF_LEN_FIELD)

static inline uint64_t amdvi_reg_readq(QTestState *s, uint64_t offset)
{
    return qtest_readq(s, AMDVI_BASE_ADDR + offset);
}

static inline void amdvi_reg_writeq(QTestState *s, uint64_t offset,
                                    uint64_t val)
{
    qtest_writeq(s, AMDVI_BASE_ADDR + offset, val);
}

static void test_cmdbuf_head_wrap(void)
{
    QTestState *s;
    uint64_t head;
    int i;
    /* 16 bytes per command */
    struct {
        uint64_t qw0;
        uint64_t qw1;
    } cmdbuf[CMDBUF_ENTRIES];

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
        return;
    }

    s = qtest_init("-M q35 -device amd-iommu");

    /* fill the command buffer with COMPLETION_WAIT (no-op) commands */
    for (i = 0; i < CMDBUF_ENTRIES; i++) {
        cmdbuf[i].qw0 = (uint64_t)AMDVI_CMD_COMPLETION_WAIT << 60;
        cmdbuf[i].qw1 = 0;
    }
    qtest_memwrite(s, CMDBUF_ADDR, cmdbuf, sizeof(cmdbuf));

    /* point the IOMMU at the command buffer and set its length */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_BASE,
                     CMDBUF_ADDR | ((uint64_t)CMDBUF_LEN_FIELD << 56));

    /* enable the IOMMU and its command buffer processor */
    amdvi_reg_writeq(s, AMDVI_MMIO_CONTROL,
                     AMDVI_MMIO_CONTROL_AMDVIEN | AMDVI_MMIO_CONTROL_CMDBUFLEN);

    /* advance tail to the last entry, consuming all but the final entry */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL,
                     (CMDBUF_ENTRIES - 1) * AMDVI_COMMAND_SIZE);

    /* wrap tail to 0, consuming the final entry and completing the buffer */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL, 0);

    /* after consuming all entries the IOMMU must wrap CmdHeadPtr to 0 */
    head = amdvi_reg_readq(s, AMDVI_MMIO_COMMAND_HEAD);
    g_assert((head & AMDVI_MMIO_CMDBUF_HEAD_MASK) == 0);

    qtest_quit(s);
}

static void test_fch_stub(void)
{
    QTestState *s;
    QPCIBus *bus;
    QPCIDevice *dev;

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
        return;
    }

    s = qtest_init("-M q35 -device amd-iommu");
    bus = qpci_new_pc(s, NULL);
    dev = qpci_device_find(bus, QPCI_DEVFN(0x14, 0));
    g_assert_nonnull(dev);

    g_assert_cmphex(qpci_config_readw(dev, PCI_VENDOR_ID), ==,
                    PCI_VENDOR_ID_AMD);
    g_assert_cmphex(qpci_config_readw(dev, PCI_DEVICE_ID), ==, 0x790b);
    g_assert_cmphex(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==,
                    PCI_CLASS_SERIAL_SMBUS);

    g_free(dev);
    qpci_free_pc(bus);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/q35/amd-iommu/cmdbuf-head-wrap", test_cmdbuf_head_wrap);
    qtest_add_func("/q35/amd-iommu/fch-stub", test_fch_stub);
    return g_test_run();
}
