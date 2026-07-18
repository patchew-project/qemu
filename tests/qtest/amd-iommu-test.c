/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "hw/i386/amd_iommu.h"

#define CMDBUF_ADDR       0x200000
#define CMDBUF_LEN_FIELD  8
#define CMDBUF_ENTRIES    (1U << CMDBUF_LEN_FIELD)
#define CMDBUF_SIZE       (CMDBUF_ENTRIES * AMDVI_COMMAND_SIZE)

#define DEVTAB_ADDR       (CMDBUF_ADDR + CMDBUF_SIZE)
#define DEVTAB_LEN_FIELD  0x1ff
#define DEVTAB_ENTRIES    (((DEVTAB_LEN_FIELD + 1) << 12U) / AMDVI_DEVTAB_ENTRY_SIZE)
#define DEVTAB_SIZE       ((DEVTAB_LEN_FIELD + 1) << 12U)

#define EVLOG_ADDR        (DEVTAB_ADDR + DEVTAB_SIZE)
#define EVLOG_LEN_FIELD   0x0f
#define EVLOG_ENTRIES     ((4096U << (EVLOG_LEN_FIELD - 8)) / AMDVI_EVENT_LEN)
#define EVLOG_SIZE        (4096U << (EVLOG_LEN_FIELD - 8))

static inline uint64_t amdvi_reg_readq(QTestState *s, uint64_t offset)
{
    return qtest_readq(s, AMDVI_BASE_ADDR + offset);
}

static inline void amdvi_reg_writeq(QTestState *s, uint64_t offset,
                                    uint64_t val)
{
    qtest_writeq(s, AMDVI_BASE_ADDR + offset, val);
}

static void amdvi_invalidate_devtab_entry(QTestState *s, uint16_t bdf)
{
    uint64_t tail = amdvi_reg_readq(s, AMDVI_MMIO_COMMAND_TAIL);

    qtest_writel(s, CMDBUF_ADDR + tail, bdf);
    qtest_writel(s, CMDBUF_ADDR + tail + 4, 0x20000000);
    qtest_writel(s, CMDBUF_ADDR + tail + 8, 0);
    qtest_writel(s, CMDBUF_ADDR + tail + 12, 0);

    g_assert_true(amdvi_reg_readq(s, AMDVI_MMIO_STATUS)
                  & AMDVI_MMIO_STATUS_CMDBUF_RUN);

    tail = (tail + AMDVI_COMMAND_SIZE) % CMDBUF_SIZE;
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL, tail);

    for (;;) {
        g_assert_true(amdvi_reg_readq(s, AMDVI_MMIO_STATUS) &
                            AMDVI_MMIO_STATUS_CMDBUF_RUN);
        if ((amdvi_reg_readq(s, AMDVI_MMIO_COMMAND_HEAD)
            & AMDVI_MMIO_CMDBUF_HEAD_MASK) == tail) {
                break;
        }
    }

    g_assert_true(amdvi_reg_readq(s, AMDVI_MMIO_STATUS)
                  & AMDVI_MMIO_STATUS_CMDBUF_RUN);
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

static void test_dte_mode_0_edu_dma(QTestState *s,
                                    QPCIDevice *edu, QPCIBar edu_bar,
                                    hwaddr edu_dma_src, hwaddr edu_dma_dst,
                                    bool edu_to_ram,
                                    bool expect_fail)
{
    bool log;
    uint64_t head, tail, addr;

    qpci_io_writeq(edu, edu_bar, 0x80, edu_dma_src);
    qpci_io_writeq(edu, edu_bar, 0x88, edu_dma_dst);
    qpci_io_writeq(edu, edu_bar, 0x90, 1);
    qpci_io_writeq(edu, edu_bar, 0x98, 0x01 | (edu_to_ram ? 0x02 : 0x00));

    while (qpci_io_readq(edu, edu_bar, 0x98) & 0x01) {
        /* edu uses a timer for scheduling the DMA operation */
        qtest_clock_step_next(s);
    }

    log = amdvi_reg_readq(s, AMDVI_MMIO_STATUS) & AMDVI_MMIO_STATUS_EVENT_INT;
    if (expect_fail) {
        g_assert_true(log);
        amdvi_reg_writeq(s, AMDVI_MMIO_STATUS, AMDVI_MMIO_STATUS_EVENT_INT);
    } else {
        g_assert_false(log);
    }

    if (expect_fail) {
        head = amdvi_reg_readq(s, AMDVI_MMIO_EVENT_HEAD) &
                        AMDVI_MMIO_EVTLOG_HEAD_MASK;
        tail = amdvi_reg_readq(s, AMDVI_MMIO_EVENT_TAIL) &
                        AMDVI_MMIO_EVTLOG_TAIL_MASK;

        /* make sure only one event has occurred */
        g_assert_true(((head + AMDVI_EVENT_LEN) % EVLOG_SIZE) ==
                      (tail % EVLOG_SIZE));

        addr = qtest_readq(s, EVLOG_ADDR + head + 8);

        if (edu_to_ram) {
            g_assert_true(addr == edu_dma_dst);
        } else {
            g_assert_true(addr == edu_dma_src);
        }

        head = (head + AMDVI_EVENT_LEN) % EVLOG_SIZE;
        amdvi_reg_writeq(s, AMDVI_MMIO_EVENT_HEAD, head);
    }
}

static void test_dte_mode_0_iommu_setup(QTestState *s)
{
    /* command buffer */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_BASE,
                     CMDBUF_ADDR | ((uint64_t)CMDBUF_LEN_FIELD << 56));
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_HEAD, 0);
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL, 0);

    /* event log */
    amdvi_reg_writeq(s, AMDVI_MMIO_EVENT_BASE,
                     EVLOG_ADDR | ((uint64_t)EVLOG_LEN_FIELD << 56));
    amdvi_reg_writeq(s, AMDVI_MMIO_EVENT_HEAD, 0);
    amdvi_reg_writeq(s, AMDVI_MMIO_EVENT_TAIL, 0);

    /* device table */
    amdvi_reg_writeq(s, AMDVI_MMIO_DEVICE_TABLE,
                     DEVTAB_ADDR | DEVTAB_LEN_FIELD);

    /* enable */
    amdvi_reg_writeq(s, AMDVI_MMIO_CONTROL, AMDVI_MMIO_CONTROL_AMDVIEN |
                     AMDVI_MMIO_CONTROL_CMDBUFLEN |
                     AMDVI_MMIO_CONTROL_EVENTLOGEN |
                     AMDVI_MMIO_CONTROL_EVENTINTEN);
}

static void test_dte_mode_0(void)
{
    QTestState *s;
    QPCIBus *bus;
    QPCIDevice *edu;
    QPCIBar edu_bar;
    const unsigned int edu_devfn = PCI_DEVFN(0x14, 0),
                       edu_devtab_off = edu_devfn * AMDVI_DEVTAB_ENTRY_SIZE;
    const hwaddr edu_dma_test_buf = 0x1000, edu_dma_dev_buf = 0x40000;

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
    }

    if (!qtest_has_device("edu")) {
        g_test_skip("edu device not available");
    }

    s = qtest_init("-M q35 -device amd-iommu,dma-remap=on,dma-translation=on,"
                   "device-iotlb=on -device edu,addr=14.0,dma_mask=0xffffffff");
    bus = qpci_new_pc(s, NULL);

    test_dte_mode_0_iommu_setup(s);

    /* get bar of EDU device */
    edu = qpci_device_find(bus, edu_devfn);
    g_assert_nonnull(edu);

    edu_bar = qpci_iomap(edu, 0, NULL);
    qpci_device_enable(edu);

    g_test_message("testing DMA passthrough, IR = 1, IW = 1");
    qtest_writeq(s, DEVTAB_ADDR + edu_devtab_off,
                 AMDVI_DEV_PERM_READ | AMDVI_DEV_PERM_WRITE |
                 AMDVI_DEV_TRANSLATION_VALID | AMDVI_DEV_VALID);
    amdvi_invalidate_devtab_entry(s, edu_devfn);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, false);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, false);

    g_test_message("testing DMA passthrough, IR = 1, IW = 0");
    qtest_writeq(s, DEVTAB_ADDR + edu_devtab_off,
                 AMDVI_DEV_PERM_READ |
                 AMDVI_DEV_TRANSLATION_VALID | AMDVI_DEV_VALID);
    amdvi_invalidate_devtab_entry(s, edu_devfn);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, false);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, true);

    g_test_message("testing DMA passthrough, IR = 0, IW = 1");
    qtest_writeq(s, DEVTAB_ADDR + edu_devtab_off,
                 AMDVI_DEV_PERM_WRITE |
                 AMDVI_DEV_TRANSLATION_VALID | AMDVI_DEV_VALID);
    amdvi_invalidate_devtab_entry(s, edu_devfn);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, true);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, false);

    /* make sure the nodma memory region is activated again properly */
    g_test_message("testing DMA passthrough, IR = 1, IW = 1 (reset to nodma)");
    qtest_writeq(s, DEVTAB_ADDR + edu_devtab_off,
                 AMDVI_DEV_PERM_READ | AMDVI_DEV_PERM_WRITE |
                 AMDVI_DEV_TRANSLATION_VALID | AMDVI_DEV_VALID);
    amdvi_invalidate_devtab_entry(s, edu_devfn);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, false);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, false);

    g_test_message("testing DMA passthrough, IR = 0, IW = 0");
    qtest_writeq(s, DEVTAB_ADDR + edu_devtab_off,
                 AMDVI_DEV_TRANSLATION_VALID | AMDVI_DEV_VALID);
    amdvi_invalidate_devtab_entry(s, edu_devfn);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, true);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, true);

    /* make sure that after reset, the device switches to nodma */
    qtest_system_reset(s);
    test_dte_mode_0_iommu_setup(s);

    g_test_message("testing DMA after reset");
    edu_bar = qpci_iomap(edu, 0, NULL);
    qpci_device_enable(edu);

    g_test_message("testing read");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_test_buf, edu_dma_dev_buf,
                            false, false);
    g_test_message("testing write");
    test_dte_mode_0_edu_dma(s, edu, edu_bar, edu_dma_dev_buf, edu_dma_test_buf,
                            true, false);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/q35/amd-iommu/cmdbuf-head-wrap", test_cmdbuf_head_wrap);
    qtest_add_func("/q35/amd-iommu/dte-mode-0", test_dte_mode_0);
    return g_test_run();
}
