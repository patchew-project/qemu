/*
 * vhost-user-blk sample application
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Felipe Franciosi <felipe@nutanix.com>
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-blk.h"
#include "contrib/libvhost-user/libvhost-user.h"

#include <glib.h>

/* Small compat shim from glib 2.32 */
#ifndef G_SOURCE_CONTINUE
#define G_SOURCE_CONTINUE TRUE
#endif
#ifndef G_SOURCE_REMOVE
#define G_SOURCE_REMOVE FALSE
#endif

/* 1 IO queue with 128 entries */
#define VIRTIO_BLK_QUEUE_NUM 128
/* And this is the final byte of request*/
#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

struct vhost_blk_dev {
    VuDev vu_dev;
    int server_sock;
    int blk_fd;
    char *blk_name;
    GMainLoop *loop;
    GTree *fdmap;   /* fd -> gsource context id */
};

struct vhost_blk_request {
    VuVirtqElement *elem;
    int64_t sector_num;
    size_t size;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr *out;
    struct vhost_blk_dev *vdev_blk;
    struct VuVirtq *vq;
};

/**  refer util/iov.c  **/
static size_t vu_blk_iov_size(const struct iovec *iov,
                              const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

/** glib event loop integration for libvhost-user and misc callbacks **/

QEMU_BUILD_BUG_ON((int)G_IO_IN != (int)VU_WATCH_IN);
QEMU_BUILD_BUG_ON((int)G_IO_OUT != (int)VU_WATCH_OUT);
QEMU_BUILD_BUG_ON((int)G_IO_PRI != (int)VU_WATCH_PRI);
QEMU_BUILD_BUG_ON((int)G_IO_ERR != (int)VU_WATCH_ERR);
QEMU_BUILD_BUG_ON((int)G_IO_HUP != (int)VU_WATCH_HUP);

typedef struct vu_blk_gsrc {
    GSource parent;
    struct vhost_blk_dev *vdev_blk;
    GPollFD gfd;
    vu_watch_cb vu_cb;
} vu_blk_gsrc_t;

static gint vu_blk_fdmap_compare(gconstpointer a, gconstpointer b)
{
    return (b > a) - (b < a);
}

static gboolean vu_blk_gsrc_prepare(GSource *src, gint *timeout)
{
    assert(timeout);

    *timeout = -1;
    return FALSE;
}

static gboolean vu_blk_gsrc_check(GSource *src)
{
    vu_blk_gsrc_t *vu_blk_src = (vu_blk_gsrc_t *)src;

    assert(vu_blk_src);

    return vu_blk_src->gfd.revents & vu_blk_src->gfd.events;
}

static gboolean vu_blk_gsrc_dispatch(GSource *src,
                                     GSourceFunc cb, gpointer data)
{
    struct vhost_blk_dev *vdev_blk;
    vu_blk_gsrc_t *vu_blk_src = (vu_blk_gsrc_t *)src;

    assert(vu_blk_src);
    assert(!(vu_blk_src->vu_cb && cb));

    vdev_blk = vu_blk_src->vdev_blk;

    assert(vdev_blk);

    if (cb) {
        return cb(data);
    }
    if (vu_blk_src->vu_cb) {
        vu_blk_src->vu_cb(&vdev_blk->vu_dev, vu_blk_src->gfd.revents, data);
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs vu_blk_gsrc_funcs = {
    vu_blk_gsrc_prepare,
    vu_blk_gsrc_check,
    vu_blk_gsrc_dispatch,
    NULL
};

static int vu_blk_gsrc_new(struct vhost_blk_dev *vdev_blk, int fd,
                           GIOCondition cond, vu_watch_cb vu_cb,
                           GSourceFunc gsrc_cb, gpointer data)
{
    GSource *vu_blk_gsrc;
    vu_blk_gsrc_t *vu_blk_src;
    guint id;

    assert(vdev_blk);
    assert(fd >= 0);
    assert(vu_cb || gsrc_cb);
    assert(!(vu_cb && gsrc_cb));

    vu_blk_gsrc = g_source_new(&vu_blk_gsrc_funcs, sizeof(vu_blk_gsrc_t));
    if (!vu_blk_gsrc) {
        fprintf(stderr, "Error creating GSource for new watch\n");
        return -1;
    }
    vu_blk_src = (vu_blk_gsrc_t *)vu_blk_gsrc;

    vu_blk_src->vdev_blk = vdev_blk;
    vu_blk_src->gfd.fd = fd;
    vu_blk_src->gfd.events = cond;
    vu_blk_src->vu_cb = vu_cb;

    g_source_add_poll(vu_blk_gsrc, &vu_blk_src->gfd);
    g_source_set_callback(vu_blk_gsrc, gsrc_cb, data, NULL);
    id = g_source_attach(vu_blk_gsrc, NULL);
    assert(id);
    g_source_unref(vu_blk_gsrc);

    g_tree_insert(vdev_blk->fdmap, (gpointer)(uintptr_t)fd,
                                    (gpointer)(uintptr_t)id);

    return 0;
}

static void vu_blk_panic_cb(VuDev *vu_dev, const char *buf)
{
    struct vhost_blk_dev *vdev_blk;

    assert(vu_dev);

    vdev_blk = (struct vhost_blk_dev *)((uintptr_t)vu_dev -
               offsetof(struct vhost_blk_dev, vu_dev));

    if (buf) {
        fprintf(stderr, "vu_blk_panic_cb: %s\n", buf);
    }

    if (vdev_blk) {
        assert(vdev_blk->loop);
        g_main_loop_quit(vdev_blk->loop);
    }
}

static void vu_blk_add_watch_cb(VuDev *vu_dev, int fd, int vu_evt,
                                vu_watch_cb cb, void *pvt) {
    struct vhost_blk_dev *vdev_blk;
    guint id;

    assert(vu_dev);
    assert(fd >= 0);
    assert(cb);

    vdev_blk = (struct vhost_blk_dev *)((uintptr_t)vu_dev -
                offsetof(struct vhost_blk_dev, vu_dev));
    if (!vdev_blk) {
        vu_blk_panic_cb(vu_dev, NULL);
        return;
    }

    id = (guint)(uintptr_t)g_tree_lookup(vdev_blk->fdmap,
                                         (gpointer)(uintptr_t)fd);
    if (id) {
        GSource *vu_blk_src = g_main_context_find_source_by_id(NULL, id);
        assert(vu_blk_src);
        g_source_destroy(vu_blk_src);
        (void)g_tree_remove(vdev_blk->fdmap, (gpointer)(uintptr_t)fd);
    }

    if (vu_blk_gsrc_new(vdev_blk, fd, vu_evt, cb, NULL, pvt)) {
        vu_blk_panic_cb(vu_dev, NULL);
    }
}

static void vu_blk_del_watch_cb(VuDev *vu_dev, int fd)
{
    struct vhost_blk_dev *vdev_blk;
    guint id;

    assert(vu_dev);
    assert(fd >= 0);

    vdev_blk = (struct vhost_blk_dev *)((uintptr_t)vu_dev -
               offsetof(struct vhost_blk_dev, vu_dev));
    if (!vdev_blk) {
        vu_blk_panic_cb(vu_dev, NULL);
        return;
    }

    id = (guint)(uintptr_t)g_tree_lookup(vdev_blk->fdmap,
                                         (gpointer)(uintptr_t)fd);
    if (id) {
        GSource *vu_blk_src = g_main_context_find_source_by_id(NULL, id);
        assert(vu_blk_src);
        g_source_destroy(vu_blk_src);
        (void)g_tree_remove(vdev_blk->fdmap, (gpointer)(uintptr_t)fd);
    }
}

static void vu_blk_req_complete(struct vhost_blk_request *req)
{
    VuDev *vu_dev = &req->vdev_blk->vu_dev;

    /* IO size with 1 extra status byte */
    vu_queue_push(vu_dev, req->vq, req->elem,
                  req->size + 1);
    vu_queue_notify(vu_dev, req->vq);

    if (req->elem) {
        free(req->elem);
    }
    if (req) {
        free(req);
    }
}

static int vu_blk_open(const char *file_name)
{
    int fd;

    fd = open(file_name, O_RDWR | O_DIRECT);
    if (fd < 0) {
        fprintf(stderr, "Cannot open file %s, %s\n", file_name,
                strerror(errno));
        return -1;
    }

    return fd;
}

static void vu_blk_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static ssize_t
vu_blk_readv(struct vhost_blk_request *req, struct iovec *iov, uint32_t iovcnt)
{
    struct vhost_blk_dev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    req->size = vu_blk_iov_size(iov, iovcnt);
    rc = preadv(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "Block %s, Sector %"PRIu64", Size %lu Read Failed\n",
                vdev_blk->blk_name, req->sector_num, req->size);
        return -1;
    }

    return rc;
}

static ssize_t
vu_blk_writev(struct vhost_blk_request *req, struct iovec *iov, uint32_t iovcnt)
{
    struct vhost_blk_dev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    req->size = vu_blk_iov_size(iov, iovcnt);
    rc = pwritev(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "Block %s, Sector %"PRIu64", Size %lu Write Failed\n",
                vdev_blk->blk_name, req->sector_num, req->size);
        return -1;
    }

    return rc;
}

static void
vu_blk_flush(struct vhost_blk_request *req)
{
    struct vhost_blk_dev *vdev_blk = req->vdev_blk;

    if (vdev_blk->blk_fd) {
        fsync(vdev_blk->blk_fd);
    }
}


static int vu_virtio_blk_process_req(struct vhost_blk_dev *vdev_blk,
                                     VuVirtq *vq)
{
    VuVirtqElement *elem;
    uint32_t type;
    unsigned in_num;
    unsigned out_num;
    struct vhost_blk_request *req;

    elem = vu_queue_pop(&vdev_blk->vu_dev, vq, sizeof(VuVirtqElement));
    if (!elem) {
        return -1;
    }

    /* refer virtio_blk.c */
    if (elem->out_num < 1 || elem->in_num < 1) {
        fprintf(stderr, "Invalid descriptor\n");
        return -1;
    }

    req = calloc(1, sizeof(*req));
    assert(req);
    req->vdev_blk = vdev_blk;
    req->vq = vq;
    req->elem = elem;

    in_num = elem->in_num;
    out_num = elem->out_num;

    if (elem->out_sg[0].iov_len < sizeof(struct virtio_blk_outhdr)) {
        fprintf(stderr, "Invalid outhdr size\n");
        free(req);
        return -1;
    }
    req->out = (struct virtio_blk_outhdr *)elem->out_sg[0].iov_base;
    out_num--;

    if (elem->in_sg[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        fprintf(stderr, "Invalid inhdr size\n");
        free(req);
        return -1;
    }
    req->in = (struct virtio_blk_inhdr *)elem->in_sg[in_num - 1].iov_base;
    in_num--;

    type = le32_to_cpu(req->out->type);
    switch (type & ~(VIRTIO_BLK_T_OUT | VIRTIO_BLK_T_BARRIER)) {
        case VIRTIO_BLK_T_IN: {
            ssize_t ret = 0;
            bool is_write = type & VIRTIO_BLK_T_OUT;
            req->sector_num = le64_to_cpu(req->out->sector);
            if (is_write) {
                assert(out_num != 0);
                ret  = vu_blk_writev(req, &elem->out_sg[1], out_num);
            } else {
                assert(in_num != 0);
                ret = vu_blk_readv(req, &elem->in_sg[0], in_num);
            }
            if (ret >= 0) {
                req->in->status = VIRTIO_BLK_S_OK;
            } else {
                req->in->status = VIRTIO_BLK_S_IOERR;
            }
            vu_blk_req_complete(req);
            break;
        }
        case VIRTIO_BLK_T_FLUSH: {
            vu_blk_flush(req);
            req->in->status = VIRTIO_BLK_S_OK;
            vu_blk_req_complete(req);
            break;
        }
        case VIRTIO_BLK_T_GET_ID: {
            size_t size = MIN(vu_blk_iov_size(&elem->in_sg[0], in_num),
                              VIRTIO_BLK_ID_BYTES);
            snprintf(elem->in_sg[0].iov_base, size, "%s", "vhost_user_blk");
            req->in->status = VIRTIO_BLK_S_OK;
            req->size = elem->in_sg[0].iov_len;
            vu_blk_req_complete(req);
            break;
        }
        default: {
            req->in->status = VIRTIO_BLK_S_UNSUPP;
            vu_blk_req_complete(req);
            break;
        }
    }

    return 0;
}

static void vu_blk_process_vq(VuDev *vu_dev, int idx)
{
    struct vhost_blk_dev *vdev_blk;
    VuVirtq *vq;
    int ret;

    if ((idx < 0) || (idx >= VHOST_MAX_NR_VIRTQUEUE)) {
        fprintf(stderr, "VQ Index out of range: %d\n", idx);
        vu_blk_panic_cb(vu_dev, NULL);
        return;
    }


    vdev_blk = (struct vhost_blk_dev *)((uintptr_t)vu_dev -
                offsetof(struct vhost_blk_dev, vu_dev));
    assert(vdev_blk);

    vq = vu_get_queue(vu_dev, idx);
    assert(vq);

    while (1) {
        ret = vu_virtio_blk_process_req(vdev_blk, vq);
        if (ret) {
            break;
        }
    }
}

static void vu_blk_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    if ((idx < 0) || (idx >= VHOST_MAX_NR_VIRTQUEUE)) {
        fprintf(stderr, "VQ Index out of range: %d\n", idx);
        vu_blk_panic_cb(vu_dev, NULL);
        return;
    }

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vu_blk_process_vq : NULL);
}

static uint64_t
vu_blk_get_features(VuDev *dev)
{
    return 1ull << VIRTIO_BLK_F_SIZE_MAX |
           1ull << VIRTIO_BLK_F_SEG_MAX |
           1ull << VIRTIO_BLK_F_TOPOLOGY |
           1ull << VIRTIO_BLK_F_BLK_SIZE |
           1ull << VIRTIO_F_VERSION_1 |
           1ull << VHOST_USER_F_PROTOCOL_FEATURES;
}

static const VuDevIface vu_blk_iface = {
    .get_features = vu_blk_get_features,
    .queue_set_started = vu_blk_queue_set_started,
};

static gboolean vu_blk_vhost_cb(gpointer data)
{
    VuDev *vu_dev = (VuDev *)data;

    assert(vu_dev);

    if (!vu_dispatch(vu_dev) != 0) {
        fprintf(stderr, "Error processing vhost message\n");
        vu_blk_panic_cb(vu_dev, NULL);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static int unix_sock_new(char *unix_fn)
{
    int sock;
    struct sockaddr_un un;
    size_t len;

    assert(unix_fn);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock <= 0) {
        perror("socket");
        return -1;
    }

    un.sun_family = AF_UNIX;
    (void)snprintf(un.sun_path, sizeof(un.sun_path), "%s", unix_fn);
    len = sizeof(un.sun_family) + strlen(un.sun_path);

    (void)unlink(unix_fn);
    if (bind(sock, (struct sockaddr *)&un, len) < 0) {
        perror("bind");
        goto fail;
    }

    if (listen(sock, 1) < 0) {
        perror("listen");
        goto fail;
    }

    return sock;

fail:
    (void)close(sock);

    return -1;
}

static int vdev_blk_run(struct vhost_blk_dev *vdev_blk)
{
    int cli_sock;
    int ret = 0;

    assert(vdev_blk);
    assert(vdev_blk->server_sock >= 0);
    assert(vdev_blk->loop);

    cli_sock = accept(vdev_blk->server_sock, (void *)0, (void *)0);
    if (cli_sock < 0) {
        perror("accept");
        return -1;
    }

    vu_init(&vdev_blk->vu_dev,
            cli_sock,
            vu_blk_panic_cb,
            vu_blk_add_watch_cb,
            vu_blk_del_watch_cb,
            &vu_blk_iface);

    if (vu_blk_gsrc_new(vdev_blk, cli_sock, G_IO_IN, NULL, vu_blk_vhost_cb,
                     &vdev_blk->vu_dev)) {
        ret = -1;
        goto out;
    }

    g_main_loop_run(vdev_blk->loop);

out:
    vu_deinit(&vdev_blk->vu_dev);
    return ret;
}

static void vdev_blk_deinit(struct vhost_blk_dev *vdev_blk)
{
    if (!vdev_blk) {
        return;
    }

    if (vdev_blk->server_sock >= 0) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);

        if (getsockname(vdev_blk->server_sock, (struct sockaddr *)&ss,
                        &sslen) == 0) {
            struct sockaddr_un *su = (struct sockaddr_un *)&ss;
            (void)unlink(su->sun_path);
        }

        (void)close(vdev_blk->server_sock);
        vdev_blk->server_sock = -1;
    }

    if (vdev_blk->loop) {
        g_main_loop_unref(vdev_blk->loop);
        vdev_blk->loop = NULL;
    }

    if (vdev_blk->blk_fd) {
        vu_blk_close(vdev_blk->blk_fd);
    }
}

static struct vhost_blk_dev *
vdev_blk_new(char *unix_fn, char *blk_file)
{
    struct vhost_blk_dev *vdev_blk = NULL;

    vdev_blk = calloc(1, sizeof(struct vhost_blk_dev));
    if (!vdev_blk) {
        fprintf(stderr, "calloc: %s", strerror(errno));
        return NULL;
    }

    vdev_blk->server_sock = unix_sock_new(unix_fn);
    if (vdev_blk->server_sock < 0) {
        goto err;
    }

    vdev_blk->loop = g_main_loop_new(NULL, FALSE);
    if (!vdev_blk->loop) {
        fprintf(stderr, "Error creating glib event loop");
        goto err;
    }

    vdev_blk->fdmap = g_tree_new(vu_blk_fdmap_compare);
    if (!vdev_blk->fdmap) {
        fprintf(stderr, "Error creating glib tree for fdmap");
        goto err;
    }

    vdev_blk->blk_fd = vu_blk_open(blk_file);
    if (vdev_blk->blk_fd  < 0) {
        fprintf(stderr, "Error open block device %s\n", blk_file);
        goto err;
    }
    vdev_blk->blk_name = blk_file;

    return vdev_blk;

err:
    vdev_blk_deinit(vdev_blk);
    free(vdev_blk);

    return NULL;
}

int main(int argc, char **argv)
{
    int opt;
    char *unix_socket = NULL;
    char *blk_file = NULL;
    struct vhost_blk_dev *vdev_blk = NULL;

    while ((opt = getopt(argc, argv, "b:h:s:")) != -1) {
        switch (opt) {
        case 'b':
            blk_file = strdup(optarg);
            break;
        case 's':
            unix_socket = strdup(optarg);
            break;
        case 'h':
        default:
            printf("Usage: %s [-b block device or file, -s UNIX domain socket]"
                   " | [ -h ]\n", argv[0]);
            break;
        }
    }

    if (!unix_socket || !blk_file) {
        printf("Usage: %s [-b block device or file, -s UNIX domain socket] |"
               " [ -h ]\n", argv[0]);
        return -1;
    }

    vdev_blk = vdev_blk_new(unix_socket, blk_file);
    if (!vdev_blk) {
        goto err;
    }

    if (vdev_blk_run(vdev_blk) != 0) {
        goto err;
    }

err:
    if (vdev_blk) {
        vdev_blk_deinit(vdev_blk);
        free(vdev_blk);
    }
    if (unix_socket) {
        free(unix_socket);
    }
    if (blk_file) {
        free(blk_file);
    }

    return 0;
}

