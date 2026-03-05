/*
 * QTest testcase for vhost-vdpa using VDUSE devices
 *
 * Based on vhost-user-test.c
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 * Copyright (c) 2026 - VDUSE adaptation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qemu/lockable.h"

#include "libqtest-single.h"
#include "qapi/error.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-net.h"
#include "hw/virtio/virtio-net.h"

#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_net.h"

#include "subprojects/libvduse/linux-headers/linux/vduse.h"
#include "subprojects/libvduse/libvduse.h"

#include <endian.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/vhost.h>

/* TODO fix this */
#include "subprojects/libvduse/libvduse.c"

#define QEMU_CMD_MEM    " -m %d -object memory-backend-file,id=mem,size=%dM," \
                        "mem-path=%s,share=on -numa node,memdev=mem"
#define QEMU_CMD_VDPA   " -netdev type=vhost-vdpa,x-svq=on,vhostdev=%s,id=hs0"
#define VDUSE_RECONNECT_LOG "vduse_reconnect.log"

static int NUM_RX_BUFS = 2;

typedef struct VdpaThread {
    GThread *thread;
    GMainLoop *loop;
    GMainContext *context;

    /* Guest memory that must be free at the end of the test */
    uint64_t qemu_mem_to_free;
} VdpaThread;

static void *vhost_vdpa_thread_function(void *data)
{
    GMainLoop *loop = data;
    g_main_loop_run(loop);
    return NULL;
}

static void vhost_vdpa_thread_init(VdpaThread *t)
{
    t->context = g_main_context_new();
    t->loop = g_main_loop_new(t->context, FALSE);
    t->thread = g_thread_new("vdpa-thread", vhost_vdpa_thread_function, t->loop);
}

static void vhost_vdpa_thread_cleanup(VdpaThread *t)
{
    g_main_loop_quit(t->loop);
    g_thread_join(t->thread);

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, TRUE);
    }

    g_main_loop_unref(t->loop);
    g_main_context_unref(t->context);
}

static void vhost_vdpa_thread_add_source_fd(VdpaThread *t, int fd,
                                            GUnixFDSourceFunc func, void *data)
{
    GSource *src = g_unix_fd_source_new(fd, G_IO_IN);
    g_source_set_callback(src, (GSourceFunc)func, data, NULL);
    g_source_attach(src, t->context);
    g_source_unref(src);
}

static void vhost_vdpa_add_rx_pkts(QGuestAllocator *alloc, QVirtioNet *net,
                                   VdpaThread *t)
{
    QTestState *qts = global_qtest;

    t->qemu_mem_to_free = guest_alloc(alloc, 64);

    for (int i = 0; i < NUM_RX_BUFS; i++) {
        uint32_t head = qvirtqueue_add(qts, net->queues[0],
                                       t->qemu_mem_to_free, 64,
                                       /* write */ false, /* next */ false);
        qvirtqueue_kick(qts, net->vdev, net->queues[0], head);
    }
}

/**
 * Send a descriptor or a chain of descriptors to the device, and optionally
 * and / or update the avail ring and avail_idx of the driver ring.
 *
 * @alloc: the guest allocator to allocate memory for the descriptors
 * @net: the virtio net device
 * @t: the vdpa thread to push the expected chain length if kick is true
 *
 * Returns the kick_id you can use to kick the device in a later call to this
 * function.
 */
static uint32_t vhost_vdpa_add_tx_pkt_descs(QGuestAllocator *alloc,
                                            QVirtioNet *net, VdpaThread *t)
{
    QTestState *qts = global_qtest;
    uint32_t req_addr;

    /* TODO: Actually free this. RFC, is actually needed? */
    req_addr = guest_alloc(alloc, 64);
    g_assert_cmpint(req_addr, >, 0);

    return qvirtqueue_add(qts, net->queues[1], req_addr, 64, /* write */ false,
                          /* next */ false);
}

static void vhost_vdpa_kick_tx_desc(VdpaThread *t, QVirtioNet *net,
                                    uint32_t kick_id)
{
    QTestState *qts = global_qtest;

    qvirtqueue_kick(qts, net->vdev, net->queues[1], kick_id);
}

static void vhost_vdpa_get_tx_pkt(QGuestAllocator *alloc, QVirtioNet *net,
                                  uint32_t desc_idx, VdpaThread *t)
{
    g_autofree struct VduseVirtqElement *elem = NULL;
    int64_t timeout = 5 * G_TIME_SPAN_SECOND;
    QTestState *qts = global_qtest;
    vring_desc_t desc;
    int64_t end_time_us;
    uint32_t len;

    end_time_us = g_get_monotonic_time() + timeout;
    qvirtio_wait_used_elem(qts, net->vdev, net->queues[1], desc_idx, &len,
                           timeout);
    g_assert_cmpint(g_get_monotonic_time(), <, end_time_us);
    g_assert_cmpint(len, ==, 0);

    qtest_memread(qts, net->queues[1]->desc + sizeof(desc)*desc_idx, &desc,
                  sizeof(desc));
    /* We know we're version 1 so always little endian */
    guest_free(alloc, le64toh(desc.addr));
}

typedef struct TestServer {
    gchar *vduse_name;
    gchar *vdpa_dev_path;
    gchar *tmpfs;
    int vq_read_num;
    VduseDev *vdev;
    VdpaThread vdpa_thread;
    QemuMutex data_mutex;
    QemuCond data_cond;
    bool ready;
} TestServer;

static bool test_read_first_byte(int dev_fd, uint64_t addr)
{
    struct vduse_iotlb_entry entry;
    int fd;
    void *mmap_addr;

    entry.start = addr;
    entry.last = addr + 1;

    fd = ioctl(dev_fd, VDUSE_IOTLB_GET_FD, &entry);
    if (fd < 0) {
        g_test_message("Failed to get fd for iova 0x%" PRIx64 ": %s",
                       addr, strerror(errno));
        return false;
    }

    mmap_addr = mmap(0, 1, PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_addr == MAP_FAILED) {
        close(fd);
        g_test_message("Failed to mmap fd for iova 0x%" PRIx64 ": %s",
                       addr, strerror(errno));
        goto close_fd;
    }

    *(volatile  uint8_t *)mmap_addr;
    munmap(mmap_addr, 1);

close_fd:
    close(fd);

    return true;
}

static void vduse_read_guest_mem_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
    TestServer *s = vduse_dev_get_priv(dev);
    int dev_fd = vduse_dev_get_fd(dev);
    struct vduse_vq_info vq_info;
    int ret;

    g_test_message("Enabling queue %d", vq->index);

    /* Get VQ info to retrieve ring addresses */
    vq_info.index = vq->index;
    ret = ioctl(dev_fd, VDUSE_VQ_GET_INFO, &vq_info);
    if (ret < 0 || !vq_info.ready) {
        return;
    }

    test_read_first_byte(dev_fd, vq_info.desc_addr);
    test_read_first_byte(dev_fd, vq_info.driver_addr);
    test_read_first_byte(dev_fd, vq_info.device_addr);

    QEMU_LOCK_GUARD(&s->data_mutex);
    s->vq_read_num++;
    if (s->vq_read_num == 2) {
        /* Notify the test that we have read the rings for both queues */
        qemu_cond_broadcast(&s->data_cond);
    }
}

static void vduse_read_guest_mem_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
    /* Queue disabled */
}

static const VduseOps vduse_read_guest_mem_ops = {
    .enable_queue = vduse_read_guest_mem_enable_queue,
    .disable_queue = vduse_read_guest_mem_disable_queue,
};

static gboolean vhost_vdpa_rxtx_handle_tx(int fd, GIOCondition condition,
                                          void *data)
{
    VduseVirtq *vq = data;

    eventfd_read(fd, (eventfd_t[]){0});
    do {
        g_autofree VduseVirtqElement *elem = NULL;

        elem = vduse_queue_pop(vq, sizeof(*elem));
        if (!elem) {
            break;
        }

        g_test_message("Got element with %d buffers", elem->out_num);
        g_assert_cmpint(elem->in_num, ==, 0);

        vduse_queue_push(vq, elem, 0);
        vduse_queue_notify(vq);
    } while (true);

    return G_SOURCE_CONTINUE;
}

static void vduse_rxtx_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
    TestServer *s = vduse_dev_get_priv(dev);

    g_test_message("Enabling queue %d", vq->index);

    if (vq->index == 1) {
        /* This is the tx queue, add a source to handle it */
        vhost_vdpa_thread_add_source_fd(&s->vdpa_thread,
                                        vduse_queue_get_fd(vq),
                                        vhost_vdpa_rxtx_handle_tx, vq);
    }
}

static void vduse_rxtx_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
    /* Queue disabled */
}

static const VduseOps vduse_rxtx_ops = {
    .enable_queue = vduse_rxtx_enable_queue,
    .disable_queue = vduse_rxtx_disable_queue,
};

static gboolean vduse_dev_handler_source_fd(int fd, GIOCondition condition,
                                            void *data)
{
    TestServer *s = data;
    int r;

    if (poll(&(struct pollfd){.fd = fd, .events = POLLIN}, 1, 0) <= 0) {
        return G_SOURCE_CONTINUE /* Spurious */;
    }

    r = vduse_dev_handler(s->vdev);
    assert (r == 0);
    return G_SOURCE_CONTINUE;
}

typedef enum {
    VDPA_DEV_ADD,
    VDPA_DEV_DEL,
} vdpa_cmd_t;

/* TODO: Issue proper nl commands */
static int netlink_vdpa_device_do(vdpa_cmd_t cmd, const char *vduse_name)
{
    g_autoptr(GError) err = NULL;
    g_auto(GStrv) argv = g_strdupv(
        (cmd == VDPA_DEV_ADD) ?
            (char **)(const char *[]){"vdpa", "dev", "add", "name", vduse_name,
                            "mgmtdev", "vduse", NULL} :
            (char **)(const char *[]){"vdpa", "dev", "del", vduse_name, NULL});
    GSpawnFlags flags = G_SPAWN_DEFAULT | G_SPAWN_SEARCH_PATH |
                        G_SPAWN_STDIN_FROM_DEV_NULL |
                        G_SPAWN_STDOUT_TO_DEV_NULL |
                        G_SPAWN_STDERR_TO_DEV_NULL;
    if (cmd == VDPA_DEV_DEL) {
        /* TODO: del blocks in read() for the write_err_and_exit, or just for
         * the child to properly close child_err_report_pipe. But, either way,
         * it causes the test to hang if we don't set this flag.
         *
         * It seems run under gdb step by step also makes the parent able to
         * continue, so probably a race condition?
         *
         * glib2-devel-2.84.4.
         */
        flags |= G_SPAWN_LEAVE_DESCRIPTORS_OPEN;
    }
    gint wait_status = 0;

    if (!g_spawn_sync(/* working_dir */ NULL, argv, /* envp */ NULL, flags,
                      /* child_setup */ NULL, /* user_data */ NULL,
                      /* standard_output */ NULL, /* standard_error */ NULL,
                      &wait_status, &err)) {
        g_test_message("Failed to execute command: %s", err->message);
        return -1;
    }

    assert(WIFEXITED(wait_status));
    if (WEXITSTATUS(wait_status) != 0) {
        g_test_message("Command failed with exit code: %d",
                       WEXITSTATUS(wait_status));
        return wait_status;
    }

    return WEXITSTATUS(wait_status);
}

static char *vhost_find_device(const char *name)
{
    /* Find vhost-vdpa device name */
    g_autoptr(GDir) dir = NULL;
    g_autoptr(GError) err = NULL;
    g_autofree char *sys_path = g_strdup_printf("/sys/devices/virtual/vduse/%s/%s",
                                                name,
                                                name);
    dir = g_dir_open(sys_path, 0, &err);
    if (!dir) {
        g_test_message("Failed to open sys path %s: %s", sys_path, err->message);
        return NULL;
    }

    for (const char *entry; (entry = g_dir_read_name(dir)) != NULL; ) {
        if (g_str_has_prefix(entry, "vhost-vdpa-")) {
            return g_strdup_printf("/dev/%s", entry);
        }
    }

    return NULL;
}

static bool test_setup_reconnect_log(VduseDev *vdev, const char *tmpfs)
{
    g_autofree char *filename = NULL;
    g_autoptr(GError) err = NULL;
    int fd, r;
    bool ok;

    filename = g_build_filename(tmpfs, "vhost-vdpa-test-XXXXXX", NULL);
    fd = g_mkstemp_full(filename, 0, 0600);
    if (fd < 0) {
        g_test_message("Failed to create temporary file for reconnect log: %s",
                       g_strerror(errno));
        return false;
    }

    /* TODO: Properly handle errors here */
    r = vduse_set_reconnect_log_file(vdev, filename);
    assert(r == 0);
    r = unlink(filename);
    assert(r == 0);
    ok = g_close(fd, &err);
    assert(ok == TRUE);

    return ok;
}

static TestServer *test_server_new(const gchar *name, const VduseOps *ops)
{
    TestServer *server = g_new0(TestServer, 1);
    g_autoptr(GError) err = NULL;
    g_autofree char *tmpfs = NULL;
    char config[sizeof(struct virtio_net_config)] = {0};
    uint64_t features;

    vhost_vdpa_thread_init(&server->vdpa_thread);

    server->vduse_name = g_strdup_printf("vdpa-test-%s", name);

    qemu_mutex_init(&server->data_mutex);
    qemu_cond_init(&server->data_cond);

    /* Disabling NOTIFY_ON_EMPTY as SVQ does not support it */
    features = (vduse_get_virtio_features() & ~(1ULL << VIRTIO_F_NOTIFY_ON_EMPTY)) |
               (1ULL << VIRTIO_NET_F_MAC);

    server->vdev = vduse_dev_create(server->vduse_name,
                                    VIRTIO_ID_NET,
                                    0x1AF4, /* PCI vendor ID */
                                    features,
                                    2, /* num_queues */
                                    sizeof(config),
                                    config,
                                    ops,
                                    server);

    if (!server->vdev) {
        return server;
    }

    vhost_vdpa_thread_add_source_fd(&server->vdpa_thread, server->vdev->fd,
                                    vduse_dev_handler_source_fd, server);

    tmpfs = g_dir_make_tmp("vhost-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert_nonnull(tmpfs);
    server->tmpfs = g_steal_pointer(&tmpfs);

    test_setup_reconnect_log(server->vdev, server->tmpfs);
    vduse_dev_setup_queue(server->vdev, 0, VIRTQUEUE_MAX_SIZE);
    vduse_dev_setup_queue(server->vdev, 1, VIRTQUEUE_MAX_SIZE);

    if (netlink_vdpa_device_do(VDPA_DEV_ADD, server->vduse_name) != 0) {
        g_test_message("Failed to add vdpa device");
        return server;
    }
    server->vdpa_dev_path = vhost_find_device(server->vduse_name);
    if (!server->vdpa_dev_path) {
        return server;
    }

    server->ready = true;

    return server;
}

static void test_server_free(TestServer *server)
{
    g_test_message("About to call vdpa del device");

    netlink_vdpa_device_do(VDPA_DEV_DEL, server->vduse_name);

    /* finish the helper thread and dispatch pending sources */
    vhost_vdpa_thread_cleanup(&server->vdpa_thread);

    if (server->vdev) {
        vduse_dev_destroy(server->vdev);
    }

    g_free(server->vduse_name);
    g_free(server->vdpa_dev_path);
    g_free(server->tmpfs);

    qemu_cond_destroy(&server->data_cond);
    qemu_mutex_destroy(&server->data_mutex);
    g_free(server);
}

static void wait_for_vqs(TestServer *s)
{
    gint64 end_time_us;

    QEMU_LOCK_GUARD(&s->data_mutex);
    end_time_us = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (s->vq_read_num < 2) {
        if (!qemu_cond_timedwait(&s->data_cond, &s->data_mutex,
                                 end_time_us - g_get_monotonic_time())) {
            /* timeout has passed */
            g_assert_cmpint(s->vq_read_num, ==, 2);
            break;
        }
    }
}

static void test_wait(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *server = arg;

    wait_for_vqs(server);
}

static void vhost_vdpa_test_cleanup(void *s)
{
    TestServer *server = s;

    /* Cannot delete vdpa dev until QEMU stops using it. */
    qtest_kill_qemu(global_qtest);
    test_server_free(server);
}

static void *vhost_vdpa_test_setup(GString *cmd_line, void *arg)
{
    TestServer *server = test_server_new("vdpa-memfile", arg);

    if (!server->ready) {
        g_test_skip("Failed to create VDUSE device");
        test_server_free(server);
        return NULL;
    }

    g_string_append_printf(cmd_line, QEMU_CMD_MEM, 256, 256, server->tmpfs);
    g_string_append_printf(cmd_line, QEMU_CMD_VDPA, server->vdpa_dev_path);
    g_test_message("cmdline: %s", cmd_line->str);

    g_test_queue_destroy(vhost_vdpa_test_cleanup, server);

    return server;
}

static void vhost_vdpa_tx_test(void *obj, void *arg, QGuestAllocator *alloc)
{
    TestServer *server = arg;
    QVirtioNet *net = obj;
    uint32_t free_head;

    /* Add some rx packets so SVQ must clean them at the end of QEMU run */
    vhost_vdpa_add_rx_pkts(alloc, net, &server->vdpa_thread);

    free_head = vhost_vdpa_add_tx_pkt_descs(alloc, net, &server->vdpa_thread);
    vhost_vdpa_kick_tx_desc(&server->vdpa_thread, net, free_head);
    vhost_vdpa_get_tx_pkt(alloc, net, free_head, &server->vdpa_thread);
}

static void register_vhost_vdpa_test(void)
{
    /* TODO: void * discards const qualifier */
    QOSGraphTestOptions opts = {
        .before = vhost_vdpa_test_setup,
        .subprocess = true,
        .arg = (void *)&vduse_read_guest_mem_ops,
    };

    qos_add_test("vhost-vdpa/read-guest-mem/memfile",
                 "virtio-net",
                 test_wait, &opts);

    opts.arg = (void *)&vduse_rxtx_ops;
    qos_add_test("vhost-vdpa/rxtx",
                 "virtio-net",
                 vhost_vdpa_tx_test, &opts);
}
libqos_init(register_vhost_vdpa_test);
