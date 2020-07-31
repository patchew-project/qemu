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

void mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
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
        error_propagate(errp, local_err);
    }

    return;
}

static int mpqemu_readv(QIOChannel *ioc, struct iovec *iov, int **fds,
                        size_t *nfds, Error **errp)
{
    struct iovec *l_iov = iov;
    size_t *l_nfds = nfds;
    unsigned int niov = 1;
    int **l_fds = fds;
    size_t size, len;
    Error *local_err = NULL;

    size = iov->iov_len;

    while (size > 0) {
        len = qio_channel_readv_full(ioc, l_iov, niov, l_fds, l_nfds,
                                     &local_err);

        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }
            continue;
        }

        if (len <= 0) {
            error_propagate(errp, local_err);
            return -EIO;
        }

        l_fds = NULL;
        l_nfds = NULL;

        size -= len;

        (void)iov_discard_front(&l_iov, &niov, len);
    }

    return iov->iov_len;
}

void mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    Error *local_err = NULL;
    int *fds = NULL;
    struct iovec hdr, data;
    size_t nfds = 0;

    hdr.iov_base = msg;
    hdr.iov_len = MPQEMU_MSG_HDR_SIZE;

    if (mpqemu_readv(ioc, &hdr, &fds, &nfds, &local_err) < 0) {
        error_propagate(errp, local_err);
        return;
    }

    if (msg->size > MPQEMU_MSG_DATA_MAX) {
        error_setg(errp, "The message size is more than MPQEMU_MSG_DATA_MAX %d",
                     MPQEMU_MSG_DATA_MAX);
        return;
    }

    data.iov_base = g_malloc0(msg->size);
    data.iov_len = msg->size;

    if (mpqemu_readv(ioc, &data, NULL, NULL, &local_err) < 0) {
        error_propagate(errp, local_err);
        g_free(data.iov_base);
        return;
    }

    if (msg->bytestream) {
        msg->data2 = data.iov_base;
        data.iov_base = NULL;
    } else if (msg->size > sizeof(msg->data1)) {
        error_setg(errp, "Invalid size for message");
        g_free(data.iov_base);
    } else {
        memcpy((void *)&msg->data1, data.iov_base, msg->size);
        g_free(data.iov_base);
    }

    msg->num_fds = nfds;
    if (nfds) {
        memcpy(msg->fds, fds, nfds * sizeof(int));
    }
}

/* Use in proxy only as it clobbers fd handlers. */
static void coroutine_fn mpqemu_msg_send_co(void *data)
{
    MPQemuRequest *req = (MPQemuRequest *)data;
    MPQemuMsg msg_reply = {0};
    Error *local_err = NULL;

    if (!req->ioc) {
        error_report("No channel available to send command %d",
                     req->msg->cmd);
        req->finished = true;
        req->error = -EINVAL;
        return;
    }

    req->co = qemu_coroutine_self();
    mpqemu_msg_send(req->msg, req->ioc, &local_err);
    if (local_err) {
        error_report("ERROR: failed to send command to remote %d, ",
                     req->msg->cmd);
        req->finished = true;
        req->error = -EINVAL;
        return;
    }

    mpqemu_msg_recv(&msg_reply, req->ioc, &local_err);
    if (local_err) {
        error_report("ERROR: failed to get a reply for command %d, "
                     "errno %s",
                     req->msg->cmd, strerror(errno));
        req->error = -EIO;
    } else {
        if (!mpqemu_msg_valid(&msg_reply) || msg_reply.cmd != RET_MSG) {
            error_report("ERROR: Invalid reply received for command %d",
                         req->msg->cmd);
            req->error = -EINVAL;
        } else {
            req->ret = msg_reply.data1.u64;
        }
    }
    req->finished = true;
}

/*
 * Create if needed and enter co-routine to send the message to the
 * remote channel ioc and wait for the reply.
 * Returns the value from the reply message, sets the error on failure.
 */

uint64_t mpqemu_msg_send_and_await_reply(MPQemuMsg *msg, QIOChannel *ioc,
                                  Error **errp)
{
    MPQemuRequest req = {0};
    uint64_t ret = UINT64_MAX;

    req.ioc = ioc;
    if (!req.ioc) {
        error_setg(errp, "Channel is set to NULL");
        return ret;
    }

    req.msg = msg;
    req.ret = 0;
    req.finished = false;

    req.co = qemu_coroutine_create(mpqemu_msg_send_co, &req);
    qemu_coroutine_enter(req.co);

    while (!req.finished) {
        aio_poll(qemu_get_aio_context(), true);
    }
    if (req.error) {
        error_setg(errp, "Error exchanging message with remote process, "
                        "error %d", req.error);
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
        if (msg->data1.u64 != 0) {
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

void mpqemu_msg_cleanup(MPQemuMsg *msg)
{
    if (msg->data2) {
        free(msg->data2);
    }
}
