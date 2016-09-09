/*
 * QTest testcase for VirtIO 9P
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gprintf.h>
#include "libqtest.h"
#include "qemu-common.h"
#include "libqos/pci-pc.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/9pfs/9p.h"

#define QVIRTIO_9P_TIMEOUT_US (1 * 1000 * 1000)

static const char mount_tag[] = "qtest";
static char *test_share;

static void qvirtio_9p_start(void)
{
    char *args;

    test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(test_share));

    args = g_strdup_printf("-fsdev local,id=fsdev0,security_model=none,path=%s "
                           "-device virtio-9p-pci,fsdev=fsdev0,mount_tag=%s",
                           test_share, mount_tag);

    qtest_start(args);
    g_free(args);
}

static void qvirtio_9p_stop(void)
{
    qtest_end();
    rmdir(test_share);
    g_free(test_share);
}

static void pci_nop(void)
{
    qvirtio_9p_start();
    qvirtio_9p_stop();
}

typedef struct {
    QVirtioDevice *dev;
    QGuestAllocator *alloc;
    QPCIBus *bus;
    QVirtQueue *vq;
} QVirtIO9P;

static QVirtIO9P *qvirtio_9p_pci_init(void)
{
    QVirtIO9P *v9p;
    QVirtioPCIDevice *dev;

    v9p = g_new0(QVirtIO9P, 1);
    v9p->alloc = pc_alloc_init();
    v9p->bus = qpci_init_pc();

    dev = qvirtio_pci_device_find(v9p->bus, VIRTIO_ID_9P);
    g_assert_nonnull(dev);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_9P);
    v9p->dev = (QVirtioDevice *) dev;

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, v9p->dev);
    qvirtio_set_acknowledge(&qvirtio_pci, v9p->dev);
    qvirtio_set_driver(&qvirtio_pci, v9p->dev);

    v9p->vq = qvirtqueue_setup(&qvirtio_pci, v9p->dev, v9p->alloc, 0);
    return v9p;
}

static void qvirtio_9p_pci_free(QVirtIO9P *v9p)
{
    qvirtqueue_cleanup(&qvirtio_pci, v9p->vq, v9p->alloc);
    pc_alloc_uninit(v9p->alloc);
    qvirtio_pci_device_disable(container_of(v9p->dev, QVirtioPCIDevice, vdev));
    g_free(v9p->dev);
    qpci_free_pc(v9p->bus);
    g_free(v9p);
}

static void pci_basic_config(void)
{
    QVirtIO9P *v9p;
    void *addr;
    size_t tag_len;
    char* tag;
    int i;

    qvirtio_9p_start();
    v9p = qvirtio_9p_pci_init();

    addr = ((QVirtioPCIDevice *) v9p->dev)->addr + VIRTIO_PCI_CONFIG_OFF(false);
    tag_len = qvirtio_config_readw(&qvirtio_pci, v9p->dev,
                                   (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(tag_len, ==, strlen(mount_tag));
    addr += sizeof(uint16_t);

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(&qvirtio_pci, v9p->dev,
                                      (uint64_t)(uintptr_t)addr + i);
    }
    g_assert_cmpmem(tag, tag_len, mount_tag, tag_len);
    g_free(tag);

    qvirtio_9p_pci_free(v9p);
    qvirtio_9p_stop();
}

typedef struct VirtIO9PHdr {
    uint32_t size;
    uint8_t id;
    uint16_t tag;
} QEMU_PACKED VirtIO9PHdr;

typedef struct VirtIO9PMsgRError {
    uint16_t error_len;
    char error[0];
} QEMU_PACKED VirtIO9PMsgRError;

#define P9_MAX_SIZE 8192

static void pci_basic_transaction(void)
{
    QVirtIO9P *v9p;
    VirtIO9PHdr hdr;
    VirtIO9PMsgRError *resp;
    uint64_t req_addr, resp_addr;
    uint32_t free_head;
    char *expected_error = strerror(ENOTSUP);

    qvirtio_9p_start();
    v9p = qvirtio_9p_pci_init();

    hdr.size = sizeof(hdr);
    hdr.id = P9_TERROR;
    hdr.tag = 12345;

    req_addr = guest_alloc(v9p->alloc, hdr.size);
    memwrite(req_addr, &hdr, sizeof(hdr));
    free_head = qvirtqueue_add(v9p->vq, req_addr, hdr.size, false, true);

    resp_addr = guest_alloc(v9p->alloc, P9_MAX_SIZE);
    qvirtqueue_add(v9p->vq, resp_addr, P9_MAX_SIZE, true, false);

    qvirtqueue_kick(&qvirtio_pci, v9p->dev, v9p->vq, free_head);
    guest_free(v9p->alloc, req_addr);
    qvirtio_wait_queue_isr(&qvirtio_pci, v9p->dev, v9p->vq,
                           QVIRTIO_9P_TIMEOUT_US);

    memread(resp_addr, &hdr, sizeof(hdr));
    g_assert_cmpint(hdr.size, <, (uint32_t) P9_MAX_SIZE);
    g_assert_cmpint(hdr.id, ==, (uint8_t) P9_RERROR);
    g_assert_cmpint(hdr.tag, ==, (uint16_t) 12345);

    resp = g_malloc(hdr.size);
    memread(resp_addr + sizeof(hdr), resp, hdr.size - sizeof(hdr));
    guest_free(v9p->alloc, resp_addr);
    g_assert_cmpmem(resp->error, resp->error_len, expected_error,
                    strlen(expected_error));
    g_free(resp);

    qvirtio_9p_pci_free(v9p);
    qvirtio_9p_stop();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/9p/pci/nop", pci_nop);
    qtest_add_func("/virtio/9p/pci/basic/configuration", pci_basic_config);
    qtest_add_func("/virtio/9p/pci/basic/transaction", pci_basic_transaction);

    return g_test_run();
}
