/*
 * Communication channel between QEMU and remote device process
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include <poll.h>

#include "qemu/module.h"
#include "io/mpqemu-link.h"
#include "qemu/log.h"

GSourceFuncs gsrc_funcs;

/*
 * TODO: make all communications asynchronous and run in the main
 * loop or existing IOThread.
 */

static void mpqemu_link_inst_init(Object *obj)
{
    MPQemuLinkState *s = MPQEMU_LINK(obj);

    s->ctx = g_main_context_default();
    s->loop = g_main_loop_new(s->ctx, FALSE);
}

static const TypeInfo mpqemu_link_info = {
    .name = TYPE_MPQEMU_LINK,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(MPQemuLinkState),
    .instance_init = mpqemu_link_inst_init,
};

static void mpqemu_link_register_types(void)
{
    type_register_static(&mpqemu_link_info);
}

type_init(mpqemu_link_register_types)

MPQemuLinkState *mpqemu_link_create(void)
{
    return MPQEMU_LINK(object_new(TYPE_MPQEMU_LINK));
}

void mpqemu_link_finalize(MPQemuLinkState *s)
{
    g_main_loop_unref(s->loop);
    g_main_context_unref(s->ctx);
    g_main_loop_quit(s->loop);

    mpqemu_destroy_channel(s->com);
    mpqemu_destroy_channel(s->mmio);

    object_unref(OBJECT(s));
}

void mpqemu_msg_send(MPQemuMsg *msg, MPQemuChannel *chan)
{
    int rc;
    uint8_t *data = NULL;
    union {
        char control[CMSG_SPACE(REMOTE_MAX_FDS * sizeof(int))];
        struct cmsghdr align;
    } u;
    struct msghdr hdr;
    struct cmsghdr *chdr;
    int sock = chan->sock;
    QemuMutex *lock = &chan->send_lock;

    struct iovec iov = {
        .iov_base = (char *) msg,
        .iov_len = MPQEMU_MSG_HDR_SIZE,
    };

    memset(&hdr, 0, sizeof(hdr));
    memset(&u, 0, sizeof(u));

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;

    if (msg->num_fds > REMOTE_MAX_FDS) {
        qemu_log_mask(LOG_REMOTE_DEBUG, "%s: Max FDs exceeded\n", __func__);
        return;
    }

    if (msg->num_fds > 0) {
        size_t fdsize = msg->num_fds * sizeof(int);

        hdr.msg_control = &u;
        hdr.msg_controllen = sizeof(u);

        chdr = CMSG_FIRSTHDR(&hdr);
        chdr->cmsg_len = CMSG_LEN(fdsize);
        chdr->cmsg_level = SOL_SOCKET;
        chdr->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(chdr), msg->fds, fdsize);
        hdr.msg_controllen = CMSG_SPACE(fdsize);
    }

    qemu_mutex_lock(lock);

    do {
        rc = sendmsg(sock, &hdr, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc < 0) {
        qemu_log_mask(LOG_REMOTE_DEBUG, "%s - sendmsg rc is %d, errno is %d,"
                      " sock %d\n", __func__, rc, errno, sock);
        qemu_mutex_unlock(lock);
        return;
    }

    if (msg->bytestream) {
        data = msg->data2;
    } else {
        data = (uint8_t *)msg + MPQEMU_MSG_HDR_SIZE;
    }

    do {
        rc = write(sock, data, msg->size);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    qemu_mutex_unlock(lock);
}


int mpqemu_msg_recv(MPQemuMsg *msg, MPQemuChannel *chan)
{
    int rc;
    uint8_t *data;
    union {
        char control[CMSG_SPACE(REMOTE_MAX_FDS * sizeof(int))];
        struct cmsghdr align;
    } u;
    struct msghdr hdr;
    struct cmsghdr *chdr;
    size_t fdsize;
    int sock = chan->sock;
    QemuMutex *lock = &chan->recv_lock;

    struct iovec iov = {
        .iov_base = (char *) msg,
        .iov_len = MPQEMU_MSG_HDR_SIZE,
    };

    memset(&hdr, 0, sizeof(hdr));
    memset(&u, 0, sizeof(u));

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = &u;
    hdr.msg_controllen = sizeof(u);

    qemu_mutex_lock(lock);

    do {
        rc = recvmsg(sock, &hdr, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc < 0) {
        qemu_log_mask(LOG_REMOTE_DEBUG, "%s - recvmsg rc is %d, errno is %d,"
                      " sock %d\n", __func__, rc, errno, sock);
        qemu_mutex_unlock(lock);
        return rc;
    }

    msg->num_fds = 0;
    for (chdr = CMSG_FIRSTHDR(&hdr); chdr != NULL;
         chdr = CMSG_NXTHDR(&hdr, chdr)) {
        if ((chdr->cmsg_level == SOL_SOCKET) &&
            (chdr->cmsg_type == SCM_RIGHTS)) {
            fdsize = chdr->cmsg_len - CMSG_LEN(0);
            msg->num_fds = fdsize / sizeof(int);
            if (msg->num_fds > REMOTE_MAX_FDS) {
                /*
                 * TODO: Security issue detected. Sender never sends more
                 * than REMOTE_MAX_FDS. This condition should be signaled to
                 * the admin
                 */
                qemu_log_mask(LOG_REMOTE_DEBUG,
                              "%s: Max FDs exceeded\n", __func__);
                return -ERANGE;
            }

            memcpy(msg->fds, CMSG_DATA(chdr), fdsize);
            break;
        }
    }

    if (msg->bytestream) {
        if (!msg->size) {
            qemu_mutex_unlock(lock);
            return -EINVAL;
        }

        msg->data2 = calloc(1, msg->size);
        data = msg->data2;
    } else {
        data = (uint8_t *)&msg->data1;
    }

    if (msg->size) {
        do {
            rc = read(sock, data, msg->size);
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    }

    qemu_mutex_unlock(lock);

    return rc;
}

uint64_t wait_for_remote(int efd)
{
    struct pollfd pfd = { .fd = efd, .events = POLLIN };
    uint64_t val;
    int ret;

    ret = poll(&pfd, 1, 1000);

    switch (ret) {
    case 0:
        qemu_log_mask(LOG_REMOTE_DEBUG, "Error wait_for_remote: Timed out\n");
        /* TODO: Kick-off error recovery */
        return ULLONG_MAX;
    case -1:
        qemu_log_mask(LOG_REMOTE_DEBUG, "Poll error wait_for_remote: %s\n",
                      strerror(errno));
        return ULLONG_MAX;
    default:
        if (read(efd, &val, sizeof(val)) == -1) {
            qemu_log_mask(LOG_REMOTE_DEBUG, "Error wait_for_remote: %s\n",
                          strerror(errno));
            return ULLONG_MAX;
        }
    }

    val = (val == ULLONG_MAX) ? val : (val - 1);

    return val;
}

void notify_proxy(int efd, uint64_t val)
{
    val = (val == ULLONG_MAX) ? val : (val + 1);

    if (write(efd, &val, sizeof(val)) == -1) {
        qemu_log_mask(LOG_REMOTE_DEBUG, "Error notify_proxy: %s\n",
                      strerror(errno));
    }
}

static gboolean mpqemu_link_handler_prepare(GSource *gsrc, gint *timeout)
{
    g_assert(timeout);

    *timeout = -1;

    return FALSE;
}

static gboolean mpqemu_link_handler_check(GSource *gsrc)
{
    MPQemuChannel *chan = (MPQemuChannel *)gsrc;

    return chan->gpfd.events & chan->gpfd.revents;
}

static gboolean mpqemu_link_handler_dispatch(GSource *gsrc, GSourceFunc func,
                                             gpointer data)
{
    MPQemuLinkState *s = (MPQemuLinkState *)data;
    MPQemuChannel *chan = (MPQemuChannel *)gsrc;

    s->callback(chan->gpfd.revents, chan);

    if ((chan->gpfd.revents & G_IO_HUP) || (chan->gpfd.revents & G_IO_ERR)) {
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void mpqemu_link_set_callback(MPQemuLinkState *s, mpqemu_link_callback callback)
{
    s->callback = callback;
}

void mpqemu_init_channel(MPQemuLinkState *s, MPQemuChannel **chan, int fd)
{
    MPQemuChannel *src;

    gsrc_funcs = (GSourceFuncs){
        .prepare = mpqemu_link_handler_prepare,
        .check = mpqemu_link_handler_check,
        .dispatch = mpqemu_link_handler_dispatch,
        .finalize = NULL,
    };

    src = (MPQemuChannel *)g_source_new(&gsrc_funcs, sizeof(MPQemuChannel));

    src->sock = fd;
    qemu_mutex_init(&src->send_lock);
    qemu_mutex_init(&src->recv_lock);

    g_source_set_callback(&src->gsrc, NULL, (gpointer)s, NULL);
    src->gpfd.fd = fd;
    src->gpfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    g_source_add_poll(&src->gsrc, &src->gpfd);

    *chan = src;
}

void mpqemu_destroy_channel(MPQemuChannel *chan)
{
    g_source_unref(&chan->gsrc);
    close(chan->sock);
    qemu_mutex_destroy(&chan->send_lock);
    qemu_mutex_destroy(&chan->recv_lock);
}

void mpqemu_start_coms(MPQemuLinkState *s)
{

    g_assert(g_source_attach(&s->com->gsrc, s->ctx));
    g_assert(g_source_attach(&s->mmio->gsrc, s->ctx));

    g_main_loop_run(s->loop);
}

bool mpqemu_msg_valid(MPQemuMsg *msg)
{
    if (msg->cmd >= MAX) {
        return false;
    }

    if (msg->bytestream) {
        if (!msg->data2) {
            return false;
        }
    } else {
        if (msg->data2) {
            return false;
        }
    }

    /* Verify FDs. */
    if (msg->num_fds >= REMOTE_MAX_FDS) {
        return false;
    }
    if (msg->num_fds > 0) {
        for (int i = 0; i < msg->num_fds; i++) {
            if (fcntl(msg->fds[i], F_GETFL) == -1) {
                return false;
            }
        }
    }

    /* Verify ID size. */
    if (msg->id >= UINT64_MAX) {
        return false;
    }

    /* Verify message specific fields. */
    switch (msg->cmd) {
    case SYNC_SYSMEM:
        if (msg->num_fds == 0 || msg->bytestream != 0) {
            return false;
        }
        if (msg->size != sizeof(msg->data1)) {
            return false;
        }
        break;
    case PCI_CONFIG_WRITE:
    case PCI_CONFIG_READ:
        if (msg->size != sizeof(struct conf_data_msg)) {
            return false;
        }
        break;
    case BAR_WRITE:
    case BAR_READ:
    case SET_IRQFD:
    case MMIO_RETURN:
    case DEVICE_RESET:
    case RUNSTATE_SET:
        if (msg->size != sizeof(msg->data1)) {
            return false;
        }
        break;
    case PROXY_PING:
    case START_MIG_OUT:
    case START_MIG_IN:
        if (msg->size != 0) {
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}
