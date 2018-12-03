/*
 * QTest testcase for VirtIO NIC
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/iov.h"
#include "qapi/qmp/qdict.h"
#include "hw/virtio/virtio-net.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-net.h"

#define PCI_SLOT_HP             0x06
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

#define QVIRTIO_NET_TIMEOUT_US (30 * 1000 * 1000)
#define VNET_HDR_SIZE sizeof(struct virtio_net_hdr_mrg_rxbuf)

#ifndef _WIN32

static void rx_test(QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
    uint64_t req_addr;
    uint32_t free_head;
    char test[] = "TEST";
    char buffer[64];
    int len = htonl(sizeof(test));
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        }, {
            .iov_base = test,
            .iov_len = sizeof(test),
        },
    };
    int ret;

    req_addr = guest_alloc(alloc, 64);

    free_head = qvirtqueue_add(vq, req_addr, 64, true, false);
    qvirtqueue_kick(dev, vq, free_head);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    qvirtio_wait_used_elem(dev, vq, free_head, NULL, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void tx_test(QVirtioDevice *dev,
                    QGuestAllocator *alloc, QVirtQueue *vq,
                    int socket)
{
    uint64_t req_addr;
    uint32_t free_head;
    uint32_t len;
    char buffer[64];
    int ret;

    req_addr = guest_alloc(alloc, 64);
    memwrite(req_addr + VNET_HDR_SIZE, "TEST", 4);

    free_head = qvirtqueue_add(vq, req_addr, 64, false, false);
    qvirtqueue_kick(dev, vq, free_head);

    qvirtio_wait_used_elem(dev, vq, free_head, NULL, QVIRTIO_NET_TIMEOUT_US);
    guest_free(alloc, req_addr);

    ret = qemu_recv(socket, &len, sizeof(len), 0);
    g_assert_cmpint(ret, ==, sizeof(len));
    len = ntohl(len);

    ret = qemu_recv(socket, buffer, len, 0);
    g_assert_cmpstr(buffer, ==, "TEST");
}

static void rx_stop_cont_test(QVirtioDevice *dev,
                              QGuestAllocator *alloc, QVirtQueue *vq,
                              int socket)
{
    uint64_t req_addr;
    uint32_t free_head;
    char test[] = "TEST";
    char buffer[64];
    int len = htonl(sizeof(test));
    QDict *rsp;
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        }, {
            .iov_base = test,
            .iov_len = sizeof(test),
        },
    };
    int ret;

    req_addr = guest_alloc(alloc, 64);

    free_head = qvirtqueue_add(vq, req_addr, 64, true, false);
    qvirtqueue_kick(dev, vq, free_head);

    rsp = qmp("{ 'execute' : 'stop'}");
    qobject_unref(rsp);

    ret = iov_send(socket, iov, 2, 0, sizeof(len) + sizeof(test));
    g_assert_cmpint(ret, ==, sizeof(test) + sizeof(len));

    /* We could check the status, but this command is more importantly to
     * ensure the packet data gets queued in QEMU, before we do 'cont'.
     */
    rsp = qmp("{ 'execute' : 'query-status'}");
    qobject_unref(rsp);
    rsp = qmp("{ 'execute' : 'cont'}");
    qobject_unref(rsp);

    qvirtio_wait_used_elem(dev, vq, free_head, NULL, QVIRTIO_NET_TIMEOUT_US);
    memread(req_addr + VNET_HDR_SIZE, buffer, sizeof(test));
    g_assert_cmpstr(buffer, ==, "TEST");

    guest_free(alloc, req_addr);
}

static void send_recv_test(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioNet *net_if = obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *rx = net_if->queues[0];
    QVirtQueue *tx = net_if->queues[1];
    int *sv = data;

    rx_test(dev, t_alloc, rx, sv[0]);
    tx_test(dev, t_alloc, tx, sv[0]);
}

static void stop_cont_test(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtioNet *net_if = obj;
    QVirtioDevice *dev = net_if->vdev;
    QVirtQueue *rx = net_if->queues[0];
    int *sv = data;

    rx_stop_cont_test(dev, t_alloc, rx, sv[0]);
}

#endif

static void hotplug(void *obj, void *data, QGuestAllocator *t_alloc)
{
    const char *arch = qtest_get_arch();

    qtest_qmp_device_add("virtio-net-pci", "net1",
                         "{'addr': %s}", stringify(PCI_SLOT_HP));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qpci_unplug_acpi_device_test("net1", PCI_SLOT_HP);
    }

}

static void virtio_net_test_cleanup(void *sockets)
{
    int *sv = sockets;

    close(sv[0]);
    qos_invalidate_command_line();
    close(sv[1]);
    g_free(sv);
}

static void *virtio_net_test_setup(GString *cmd_line, void *arg)
{
    int ret;
    int *sv = g_new(int, 2);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ", sv[1]);

    g_test_queue_destroy(virtio_net_test_cleanup, sv);
    return sv;
}

static void register_virtio_net_test(void)
{
    QOSGraphTestOptions opts = {
        .before = virtio_net_test_setup,
    };

#ifndef _WIN32
    qos_add_test("basic", "virtio-net", send_recv_test, &opts);
    qos_add_test("rx_stop_cont", "virtio-net", stop_cont_test, &opts);
#endif
    qos_add_test("hotplug", "virtio-pci", hotplug, &opts);
}

libqos_init(register_virtio_net_test);
