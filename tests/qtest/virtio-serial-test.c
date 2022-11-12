/*
 * QTest testcase for VirtIO Serial
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "libqos/virtio-serial.h"
#include "standard-headers/linux/virtio_console.h"
#include "qemu/iov.h"

static void virtio_serial_test_cleanup(void *sockets)
{
    int *sv = sockets;

    close(sv[0]);
    qos_invalidate_command_line();
    close(sv[1]);
    g_free(sv);
}

static void *virtio_serial_test_setup(GString *cmd_line, void *arg)
{
    int ret;
    int *sv = g_new(int, 3);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sv);
    g_assert_cmpint(ret, !=, -1);

    g_string_append_printf(
        cmd_line,
        " -chardev socket,fd=%d,id=virtioserial0",
        sv[1]);

    sv[2] = arg ? 1 : 0;
    g_test_queue_destroy(virtio_serial_test_cleanup, sv);
    return sv;
}

/* Tests only initialization so far. TODO: Replace with functional tests */
static void virtio_serial_nop(void *obj, void *data, QGuestAllocator *alloc)
{
    /* no operation */
}

static void tx_test(
    QVirtioDevice *dev,
    QGuestAllocator *alloc,
    QVirtQueue *vq,
    int socket)
{
    QTestState *qts = global_qtest;
    uint64_t req_addr;
    uint64_t free_head;
    char test[] = "TEST";
    char buffer[5];
    struct iovec iov[] = {
        {
            .iov_base = buffer,
            .iov_len = strlen(test)
        }
    };
    int ret;

    req_addr = guest_alloc(alloc, 4);
    qtest_memwrite(qts, req_addr, test, strlen(test));

    free_head = qvirtqueue_add(qts, vq, req_addr, 4, false, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    ret = iov_recv(socket, iov, 1, 0, strlen(test));
    g_assert_cmpint(ret, ==, strlen(test));

    buffer[strlen(test)] = '\0';
    g_assert_cmpstr(buffer, ==, test);

    guest_free(alloc, req_addr);
}

static void rx_test(
    QVirtioDevice *dev,
    QGuestAllocator *alloc,
    QVirtQueue *vq,
    int socket)
{
    QTestState *qts = global_qtest;
    uint64_t req_addr;
    uint64_t free_head;
    char test[] = "TEST";
    char buffer[5];
    struct iovec iov[] = {
        {
            .iov_base = test,
            .iov_len = strlen(test)
        }
    };
    int ret;

    req_addr = guest_alloc(alloc, 4);

    free_head = qvirtqueue_add(qts, vq, req_addr, 4, true, false);
    qvirtqueue_kick(qts, dev, vq, free_head);

    ret = iov_send(socket, iov, 1, 0, strlen(test));
    g_assert_cmpint(ret, ==, strlen(test));

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL, 5 * 1000 * 1000);
    qtest_memread(qts, req_addr, buffer, strlen(test));

    buffer[strlen(test)] = '\0';
    g_assert_cmpstr(buffer, ==, test);

    guest_free(alloc, req_addr);
}

static void send_recv_test(void *obj, void *data, QGuestAllocator *alloc)
{
    QVirtioSerial *serial_if = obj;
    QVirtioDevice *dev = serial_if->vdev;
    uint32_t port_open_addr, port_open_free_head;
    int *sv = data;

    /*
     * the first port is always virtconsole due to backwards compatibility
     * consideraitons so we must use the multiport feature to add the correct
     * port
     */
    QVirtQueue *rx = serial_if->queues[sv[2] == 0 ? 0 : 4];
    QVirtQueue *tx = serial_if->queues[sv[2] == 0 ? 1 : 5];
    QVirtQueue *control_tx = serial_if->queues[3];

    port_open_addr = guest_alloc(alloc, 8);

    qtest_writel(global_qtest, port_open_addr + 0, sv[2]);
    qtest_writew(global_qtest, port_open_addr + 4, VIRTIO_CONSOLE_PORT_READY);
    qtest_writew(global_qtest, port_open_addr + 6, 1);
    port_open_free_head = qvirtqueue_add(
        global_qtest,
        control_tx,
        port_open_addr,
        8,
        false,
        false);
    qvirtqueue_kick(
        global_qtest,
        dev,
        control_tx,
        port_open_free_head);

    qtest_writel(global_qtest, port_open_addr + 0, sv[2]);
    qtest_writew(global_qtest, port_open_addr + 4, VIRTIO_CONSOLE_PORT_OPEN);
    qtest_writew(global_qtest, port_open_addr + 6, 1);
    port_open_free_head = qvirtqueue_add(
        global_qtest,
        control_tx,
        port_open_addr,
        8,
        false,
        false);
    qvirtqueue_kick(
        global_qtest,
        dev,
        control_tx,
        port_open_free_head);

    guest_free(alloc, port_open_addr);

    tx_test(dev, alloc, tx, sv[0]);
    rx_test(dev, alloc, rx, sv[0]);
}

static void serial_hotplug(void *obj, void *data, QGuestAllocator *alloc)
{
    qtest_qmp_device_add(global_qtest, "virtserialport", "hp-port", "{}");
    qtest_qmp_device_del(global_qtest, "hp-port");
}

static void register_virtio_serial_test(void)
{
    QOSGraphTestOptions opts = { };

    opts.before = virtio_serial_test_setup;

    opts.arg = (gpointer)0;
    opts.edge.before_cmd_line =
        "-device virtconsole,bus=vser0.0,chardev=virtioserial0";
    qos_add_test("console-nop", "virtio-serial", virtio_serial_nop, &opts);
    qos_add_test(
        "console-send-recv",
        "virtio-serial",
        send_recv_test,
        &opts);

    opts.arg = (gpointer)1;
    opts.edge.before_cmd_line =
        "-device virtserialport,bus=vser0.0,chardev=virtioserial0";
    qos_add_test("serialport-nop", "virtio-serial", virtio_serial_nop, &opts);

    qos_add_test(
        "serialport-send-recv",
        "virtio-serial",
        send_recv_test,
        &opts);

    qos_add_test("hotplug", "virtio-serial", serial_hotplug, NULL);
}
libqos_init(register_virtio_serial_test);
