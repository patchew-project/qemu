/*
 * QTest testcase for VirtIO IOMMU
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-iommu.h"
#include "hw/virtio/virtio-iommu.h"

#define PCI_SLOT_HP             0x06
#define QVIRTIO_IOMMU_TIMEOUT_US (30 * 1000 * 1000)

static QGuestAllocator *alloc;

static void iommu_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    QVirtioPCIDevice *dev = obj;
    QTestState *qts = dev->pdev->bus->qts;

    qtest_qmp_device_add(qts, "virtio-iommu-pci", "iommu1",
                         "{'addr': %s}", stringify(PCI_SLOT_HP));

}

static void pci_config(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioIOMMU *v_iommu = obj;
    QVirtioDevice *dev = v_iommu->vdev;
    uint64_t input_range_start = qvirtio_config_readq(dev, 8);
    uint64_t input_range_end = qvirtio_config_readq(dev, 16);
    uint32_t domain_range_start = qvirtio_config_readl(dev, 24);
    uint32_t domain_range_end = qvirtio_config_readl(dev, 28);
    uint32_t probe_size = qvirtio_config_readl(dev, 32);

    g_assert_cmpint(input_range_start, ==, 0);
    g_assert_cmphex(input_range_end, ==, 0xFFFFFFFFFFFFFFFF);
    g_assert_cmpint(domain_range_start, ==, 0);
    g_assert_cmpint(domain_range_end, ==, 32);
    g_assert_cmphex(probe_size, ==, 0x200);
}

/**
 * send_attach_detach - Send an attach/detach command to the device
 * @type: VIRTIO_IOMMU_T_ATTACH/VIRTIO_IOMMU_T_DETACH
 * @domain: domain the end point is attached to
 * @ep: end-point
 */
static int send_attach_detach(QTestState *qts, QVirtioIOMMU *v_iommu,
                              uint8_t type, uint32_t domain, uint32_t ep)
{
    QVirtioDevice *dev = v_iommu->vdev;
    QVirtQueue *vq = v_iommu->vq;
    uint64_t ro_addr, wr_addr;
    uint32_t free_head;
    struct virtio_iommu_req_attach req; /* same layout as detach */
    size_t ro_size = sizeof(req) - sizeof(struct virtio_iommu_req_tail);
    size_t wr_size = sizeof(struct virtio_iommu_req_tail);
    char buffer[64];
    int ret;

    req.head.type = type;
    req.domain = domain;
    req.endpoint = ep;

    ro_addr = guest_alloc(alloc, ro_size);
    wr_addr = guest_alloc(alloc, wr_size);

    qtest_memwrite(qts, ro_addr, &req, ro_size);
    free_head = qvirtqueue_add(qts, vq, ro_addr, ro_size, false, true);
    qvirtqueue_add(qts, vq, wr_addr, wr_size, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);
    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_IOMMU_TIMEOUT_US);
    qtest_memread(qts, wr_addr, buffer, wr_size);
    ret = ((struct virtio_iommu_req_tail *)buffer)->status;
    guest_free(alloc, ro_addr);
    guest_free(alloc, wr_addr);
    return ret;
}

/**
 * send_map - Send a map command to the device
 * @domain: domain the new binding is attached to
 * @virt_start: iova start
 * @virt_end: iova end
 * @phys_start: base physical address
 * @flags: mapping flags
 */
static int send_map(QTestState *qts, QVirtioIOMMU *v_iommu,
                    uint32_t domain, uint64_t virt_start, uint64_t virt_end,
                    uint64_t phys_start, uint32_t flags)
{
    QVirtioDevice *dev = v_iommu->vdev;
    QVirtQueue *vq = v_iommu->vq;
    uint64_t ro_addr, wr_addr;
    uint32_t free_head;
    struct virtio_iommu_req_map req;
    size_t ro_size = sizeof(req) - sizeof(struct virtio_iommu_req_tail);
    size_t wr_size = sizeof(struct virtio_iommu_req_tail);
    char buffer[64];
    int ret;

    req.head.type = VIRTIO_IOMMU_T_MAP;
    req.domain = domain;
    req.virt_start = virt_start;
    req.virt_end = virt_end;
    req.phys_start = phys_start;
    req.flags = flags;

    ro_addr = guest_alloc(alloc, ro_size);
    wr_addr = guest_alloc(alloc, wr_size);

    qtest_memwrite(qts, ro_addr, &req, ro_size);
    free_head = qvirtqueue_add(qts, vq, ro_addr, ro_size, false, true);
    qvirtqueue_add(qts, vq, wr_addr, wr_size, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);
    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_IOMMU_TIMEOUT_US);
    memread(wr_addr, buffer, wr_size);
    ret = ((struct virtio_iommu_req_tail *)buffer)->status;
    guest_free(alloc, ro_addr);
    guest_free(alloc, wr_addr);
    return ret;
}

/**
 * send_unmap - Send an unmap command to the device
 * @domain: domain the new binding is attached to
 * @virt_start: iova start
 * @virt_end: iova end
 */
static int send_unmap(QTestState *qts, QVirtioIOMMU *v_iommu,
                      uint32_t domain, uint64_t virt_start, uint64_t virt_end)
{
    QVirtioDevice *dev = v_iommu->vdev;
    QVirtQueue *vq = v_iommu->vq;
    uint64_t ro_addr, wr_addr;
    uint32_t free_head;
    struct virtio_iommu_req_unmap req;
    size_t ro_size = sizeof(req) - sizeof(struct virtio_iommu_req_tail);
    size_t wr_size = sizeof(struct virtio_iommu_req_tail);
    char buffer[64];
    int ret;

    req.head.type = VIRTIO_IOMMU_T_UNMAP;
    req.domain = domain;
    req.virt_start = virt_start;
    req.virt_end = virt_end;

    ro_addr = guest_alloc(alloc, ro_size);
    wr_addr = guest_alloc(alloc, wr_size);

    qtest_memwrite(qts, ro_addr, &req, ro_size);
    free_head = qvirtqueue_add(qts, vq, ro_addr, ro_size, false, true);
    qvirtqueue_add(qts, vq, wr_addr, wr_size, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);
    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_IOMMU_TIMEOUT_US);
    memread(wr_addr, buffer, wr_size);
    ret = ((struct virtio_iommu_req_tail *)buffer)->status;
    guest_free(alloc, ro_addr);
    guest_free(alloc, wr_addr);
    return ret;
}

/* Test unmap scenari documented in the spec v0.12 */
static void test_attach_detach(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioIOMMU *v_iommu = obj;
    QTestState *qts = global_qtest;
    int ret;

    alloc = t_alloc;

    /* type, domain, ep */

    /* attach ep0 to domain 0 */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_ATTACH, 0, 0);
    g_assert_cmpint(ret, ==, 0);

    /* attach a non existing device (1) */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_ATTACH, 0, 1);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_NOENT);

    /* detach a non existing device (1) */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_DETACH, 0, 1);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_NOENT);

    /* move ep0 from domain 0 to domain 1 */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_ATTACH, 1, 0);
    g_assert_cmpint(ret, ==, 0);

    /* detach ep0 to domain 0 */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_DETACH, 0, 0);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_INVAL);

    /* detach ep0 from domain 1 */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_DETACH, 1, 0);
    g_assert_cmpint(ret, ==, 0);

    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_ATTACH, 1, 0);
    g_assert_cmpint(ret, ==, 0);
    ret = send_map(qts, v_iommu, 1, 0x0, 0xFFF, 0xa1000,
                   VIRTIO_IOMMU_MAP_F_READ);
    g_assert_cmpint(ret, ==, 0);
    ret = send_map(qts, v_iommu, 1, 0x2000, 0x2FFF, 0xb1000,
                   VIRTIO_IOMMU_MAP_F_READ);
    g_assert_cmpint(ret, ==, 0);
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_DETACH, 1, 0);
    g_assert_cmpint(ret, ==, 0);
}

static void test_map_unmap(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioIOMMU *v_iommu = obj;
    QTestState *qts = global_qtest;
    int ret;

    alloc = t_alloc;

    /* attach ep0 to domain 1 */
    ret = send_attach_detach(qts, v_iommu, VIRTIO_IOMMU_T_ATTACH, 1, 0);
    g_assert_cmpint(ret, ==, 0);

    ret = send_map(qts, v_iommu, 0, 0, 0xFFF, 0xa1000, VIRTIO_IOMMU_MAP_F_READ);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_NOENT);

    /* domain, virt start, virt end, phys start, flags */
    ret = send_map(qts, v_iommu, 1, 0, 0xFFF, 0xa1000, VIRTIO_IOMMU_MAP_F_READ);
    g_assert_cmpint(ret, ==, 0);

    ret = send_unmap(qts, v_iommu, 4, 0x10, 0xFFF);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_NOENT);

    ret = send_unmap(qts, v_iommu, 1, 0x10, 0xFFF);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_RANGE);

    ret = send_unmap(qts, v_iommu, 1, 0, 0x1000);
    g_assert_cmpint(ret, ==, 0); /* unmap everything */

    /* Spec example sequence */

    /* 1 */
    ret = send_unmap(qts, v_iommu, 1, 0, 4);
    g_assert_cmpint(ret, ==, 0); /* doesn't unmap anything */

    /* 2 */
    send_map(qts, v_iommu, 1, 0, 9, 0xa1000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 9);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,9] */

    /* 3 */
    send_map(qts, v_iommu, 1, 0, 4, 0xb1000, VIRTIO_IOMMU_MAP_F_READ);
    send_map(qts, v_iommu, 1, 5, 9, 0xb2000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 9);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,4] and [5,9] */

    /* 4 */
    send_map(qts, v_iommu, 1, 0, 9, 0xc1000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 4);
    g_assert_cmpint(ret, ==, VIRTIO_IOMMU_S_RANGE); /* doesn't unmap anything */

    ret = send_unmap(qts, v_iommu, 1, 0, 10);
    g_assert_cmpint(ret, ==, 0);

    /* 5 */
    send_map(qts, v_iommu, 1, 0, 4, 0xd1000, VIRTIO_IOMMU_MAP_F_READ);
    send_map(qts, v_iommu, 1, 5, 9, 0xd2000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 4);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,4] */

    ret = send_unmap(qts, v_iommu, 1, 5, 9);
    g_assert_cmpint(ret, ==, 0);

    /* 6 */
    send_map(qts, v_iommu, 1, 0, 4, 0xe2000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 9);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,4] */

    /* 7 */
    send_map(qts, v_iommu, 1, 0, 4, 0xf2000, VIRTIO_IOMMU_MAP_F_READ);
    send_map(qts, v_iommu, 1, 10, 14, 0xf3000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 14);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,4] and [10,14] */

    send_unmap(qts, v_iommu, 1, 0, 100);
    send_map(qts, v_iommu, 1, 10, 14, 0xf3000, VIRTIO_IOMMU_MAP_F_READ);
    send_map(qts, v_iommu, 1, 0, 4, 0xf2000, VIRTIO_IOMMU_MAP_F_READ);
    ret = send_unmap(qts, v_iommu, 1, 0, 4);
    g_assert_cmpint(ret, ==, 0); /* unmaps [0,4] and [10,14] */
}

static void register_virtio_iommu_test(void)
{
    qos_add_test("hotplug", "virtio-iommu-pci", iommu_hotplug, NULL);
    qos_add_test("config", "virtio-iommu", pci_config, NULL);
    qos_add_test("attach_detach", "virtio-iommu", test_attach_detach, NULL);
    qos_add_test("map_unmap", "virtio-iommu", test_map_unmap, NULL);
}

libqos_init(register_virtio_iommu_test);
