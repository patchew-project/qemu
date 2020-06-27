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

#include "qemu/module.h"
#include "io/mpqemu-link.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "io/channel-socket.h"

void mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc)
{
    Error *local_err = NULL;
    struct iovec send[2];
    int *fds = NULL;
    size_t nfds = 0;

    send[0].iov_base = msg;
    send[0].iov_len = MPQEMU_MSG_HDR_SIZE;

    send[1].iov_base = msg->bytestream ? msg->data2 : (void *)&msg->data1;
    send[1].iov_len = msg->size;

    if (msg->num_fds) {
        nfds = msg->num_fds;
        fds = msg->fds;
    }

    (void)qio_channel_writev_full_all(ioc, send, G_N_ELEMENTS(send), fds, nfds,
                                      &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

static int mpqemu_readv(QIOChannel *ioc, struct iovec *iov, int **fds,
                        size_t *nfds, Error **errp)
{
    size_t size, len;

    size = iov->iov_len;

    while (size > 0) {
        len = qio_channel_readv_full(ioc, iov, 1, fds, nfds, errp);

        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }
            continue;
        }

        if (len <= 0) {
            return -EIO;
        }

        size -= len;
    }

    return iov->iov_len;
}

int mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc)
{
    Error *local_err = NULL;
    int *fds = NULL;
    struct iovec hdr, data;
    size_t nfds = 0;

    hdr.iov_base = g_malloc0(MPQEMU_MSG_HDR_SIZE);
    hdr.iov_len = MPQEMU_MSG_HDR_SIZE;

    if (mpqemu_readv(ioc, &hdr, &fds, &nfds, &local_err) < 0) {
        return -EIO;
    }

    memcpy(msg, hdr.iov_base, hdr.iov_len);

    free(hdr.iov_base);
    if (msg->size > MPQEMU_MSG_DATA_MAX) {
        error_report("The message size is more than MPQEMU_MSG_DATA_MAX %d",
                     MPQEMU_MSG_DATA_MAX);
        return -EINVAL;
    }

    data.iov_base = g_malloc0(msg->size);
    data.iov_len = msg->size;

    if (mpqemu_readv(ioc, &data, NULL, NULL, &local_err) < 0) {
        return -EIO;
    }

    if (msg->bytestream) {
        msg->data2 = calloc(1, msg->size);
        memcpy(msg->data2, data.iov_base, msg->size);
    } else {
        memcpy((void *)&msg->data1, data.iov_base, msg->size);
    }

    free(data.iov_base);

    if (nfds) {
        msg->num_fds = nfds;
        memcpy(msg->fds, fds, nfds * sizeof(int));
    }

    return 0;
}

/* Use in proxy only as it clobbers fd handlers. */
static void coroutine_fn mpqemu_msg_send_co(void *data)
{
    MPQemuRequest *req = (MPQemuRequest *)data;
    MPQemuMsg msg_reply = {0};
    long ret = -EINVAL;

    if (!req->sioc) {
        error_report("No channel available to send command %d",
                     req->msg->cmd);
        atomic_mb_set(&req->finished, true);
        req->error = -EINVAL;
        return;
    }

    req->co = qemu_coroutine_self();
    mpqemu_msg_send(req->msg, QIO_CHANNEL(req->sioc));

    yield_until_fd_readable(req->sioc->fd);

    ret = mpqemu_msg_recv(&msg_reply, QIO_CHANNEL(req->sioc));
    if (ret < 0) {
        error_report("ERROR: failed to get a reply for command %d, \
                     errno %s, ret is %ld",
                     req->msg->cmd, strerror(errno), ret);
        req->error = -errno;
    } else {
        if (!mpqemu_msg_valid(&msg_reply) || msg_reply.cmd != RET_MSG) {
            error_report("ERROR: Invalid reply received for command %d",
                         req->msg->cmd);
            req->error = -EINVAL;
        } else {
            req->ret = msg_reply.data1.u64;
        }
    }
    atomic_mb_set(&req->finished, true);
}

/*
 * Create if needed and enter co-routine to send the message to the
 * remote channel ioc and wait for the reply.
 * Resturns the value from the reply message, sets the error on failure.
 */

uint64_t mpqemu_msg_send_reply_co(MPQemuMsg *msg, QIOChannel *ioc,
                                  Error **errp)
{
    MPQemuRequest req = {0};
    uint64_t ret = UINT64_MAX;

    req.sioc = QIO_CHANNEL_SOCKET(ioc);
    if (!req.sioc) {
        return ret;
    }

    req.msg = msg;
    req.ret = 0;
    req.finished = false;

    if (!req.co) {
        req.co = qemu_coroutine_create(mpqemu_msg_send_co, &req);
    }

    qemu_coroutine_enter(req.co);
    while (!req.finished) {
        aio_poll(qemu_get_aio_context(), false);
    }
    if (req.error) {
        error_setg(errp, "Error exchanging message with remote process, "\
                        "socket %d, error %d", req.sioc->fd, req.error);
    }
    ret = req.ret;

    return ret;
}

bool mpqemu_msg_valid(MPQemuMsg *msg)
{
    if (msg->cmd >= MAX && msg->cmd < 0) {
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
     /* Verify message specific fields. */
    switch (msg->cmd) {
    case SYNC_SYSMEM:
        if (msg->num_fds == 0 || msg->bytestream) {
            return false;
        }
        if (msg->size != sizeof(msg->data1)) {
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}
