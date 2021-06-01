/*
 * QTest testcase for NVMe
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "libqos/libqtest.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc-pc.h"
#include "libqos/malloc.h"
#include "libqos/libqos.h"
#include "include/block/nvme.h"
#include "include/hw/pci/pci.h"

#define NVME_BPINFO_BPSZ_UNITS  (128 * KiB)
#define NVME_BRS_BPSZ_UNITS     (4 * KiB)
#define NVME_BRS_READ_MAX_TIME  1000000
#define TEST_IMAGE_SIZE         (2 * 128 * KiB)

static char *t_path;

typedef struct QNvme QNvme;

struct QNvme {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *nvme_get_driver(void *obj, const char *interface)
{
    QNvme *nvme = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &nvme->dev;
    }

    fprintf(stderr, "%s not present in nvme\n", interface);
    g_assert_not_reached();
}

static void *nvme_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QNvme *nvme = g_new0(QNvme, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&nvme->dev, bus, addr);
    nvme->obj.get_driver = nvme_get_driver;

    return &nvme->obj;
}

static void drive_destroy(void *path)
{
    unlink(path);
    g_free(path);
    qos_invalidate_command_line();
}

/* This used to cause a NULL pointer dereference.  */
static void nvmetest_oob_cmb_test(void *obj, void *data, QGuestAllocator *alloc)
{
    const int cmb_bar_size = 2 * MiB;
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar bar;

    qpci_device_enable(pdev);
    bar = qpci_iomap(pdev, 2, NULL);

    qpci_io_writel(pdev, bar, 0, 0xccbbaa99);
    g_assert_cmpint(qpci_io_readb(pdev, bar, 0), ==, 0x99);
    g_assert_cmpint(qpci_io_readw(pdev, bar, 0), ==, 0xaa99);

    /* Test partially out-of-bounds accesses.  */
    qpci_io_writel(pdev, bar, cmb_bar_size - 1, 0x44332211);
    g_assert_cmpint(qpci_io_readb(pdev, bar, cmb_bar_size - 1), ==, 0x11);
    g_assert_cmpint(qpci_io_readw(pdev, bar, cmb_bar_size - 1), !=, 0x2211);
    g_assert_cmpint(qpci_io_readl(pdev, bar, cmb_bar_size - 1), !=, 0x44332211);
}

static void nvmetest_bp_read_test(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t test_size = 32;
    size_t bp_test_len = test_size * NVME_BRS_BPSZ_UNITS;
    uint8_t *read_buf = g_malloc(bp_test_len);
    uint8_t *cmp_buf = g_malloc(bp_test_len);
    QNvme *nvme = obj;
    QPCIDevice *pdev = &nvme->dev;
    QPCIBar nvme_bar;
    uint8_t brs = 0;
    uint64_t sleep_time = 0;
    uintptr_t guest_buf;
    uint64_t buf_addr;

    memset(cmp_buf, 0x42, bp_test_len);

    guest_buf = guest_alloc(alloc, bp_test_len);
    buf_addr = cpu_to_le64(guest_buf);

    qpci_device_enable(pdev);
    nvme_bar = qpci_iomap(pdev, 0, NULL);

    /* BPINFO */
    uint32_t bpinfo = qpci_io_readl(pdev, nvme_bar, 0x40);
    uint16_t single_bp_size = bpinfo & BPINFO_BPSZ_MASK;
    uint8_t active_bpid = bpinfo >> BPINFO_ABPID_SHIFT;
    uint8_t read_select = (bpinfo >> BPINFO_BRS_SHIFT) & BPINFO_BRS_MASK;

    g_assert_cmpint(single_bp_size, ==, 0x1);
    g_assert_cmpint(active_bpid, ==, 0);
    g_assert_cmpint(read_select, ==, NVME_BPINFO_BRS_NOREAD);

    /* BPMBL */
    uint64_t bpmbl = buf_addr;
    uint32_t bpmbl_low = bpmbl & 0xffffffff;
    uint32_t bpmbl_hi = (bpmbl >> 32) & 0xffffffff;
    qpci_io_writel(pdev, nvme_bar, 0x48, bpmbl_low);
    qpci_io_writel(pdev, nvme_bar, 0x4c, bpmbl_hi);

    /* BPRSEL */
    qpci_io_writel(pdev, nvme_bar, 0x44, 32);

    while (true) {
        usleep(1000);
        sleep_time += 1000;
        brs = qpci_io_readb(pdev, nvme_bar, 0x43) & BPINFO_BRS_MASK;
        if (brs == NVME_BPINFO_BRS_SUCCESS || brs == NVME_BPINFO_BRS_ERROR ||
            sleep_time == NVME_BRS_READ_MAX_TIME) {
            break;
        }
    }
    g_assert_cmpint(brs, ==, NVME_BPINFO_BRS_SUCCESS);

    qtest_memread(pdev->bus->qts, guest_buf, read_buf, bp_test_len);
    g_assert_cmpint(memcmp(cmp_buf, read_buf, bp_test_len), ==, 0);

    g_free(cmp_buf);
    g_free(read_buf);
    g_test_queue_destroy(drive_destroy, t_path);
}

static void nvme_register_nodes(void)
{
    int fd;
    FILE *fh;
    uint16_t bpsz = 2;
    size_t bp_len = NVME_BPINFO_BPSZ_UNITS * bpsz;
    size_t ret;
    uint8_t *pattern = g_malloc(bp_len);

    t_path = g_strdup("/tmp/qtest.XXXXXX");

    /* Create a temporary raw image */
    fd = mkstemp(t_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    memset(pattern, 0x42, bp_len);

    fh = fopen(t_path, "w+");
    ret = fwrite(pattern, NVME_BPINFO_BPSZ_UNITS, bpsz, fh);
    g_assert_cmpint(ret, ==, bpsz);
    fclose(fh);

    char *bp_cmd_line = g_strdup_printf("-drive id=bp0,file=%s,if=none,"
                                        "format=raw", t_path);

    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,drive=drv0,serial=foo",
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw ",
                           bp_cmd_line,
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("nvme", nvme_create);
    qos_node_consumes("nvme", "pci-bus", &opts);
    qos_node_produces("nvme", "pci-device");

    qos_add_test("oob-cmb-access", "nvme", nvmetest_oob_cmb_test, &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "cmb_size_mb=2"
    });

    qos_add_test("bp-read-access", "nvme", nvmetest_bp_read_test,
                 &(QOSGraphTestOptions) {
        .edge.extra_device_opts = "bootpart=bp0"
    });

    /* Clean Up */
    g_free(pattern);
}

libqos_init(nvme_register_nodes);
