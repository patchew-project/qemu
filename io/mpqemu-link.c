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
#include "io/channel.h"

void mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    bool iolock = qemu_mutex_iothread_locked();
    Error *local_err = NULL;
    struct iovec send[2];
    int *fds = NULL;
    size_t nfds = 0;

    send[0].iov_base = msg;
    send[0].iov_len = MPQEMU_MSG_HDR_SIZE;

    send[1].iov_base = (void *)&msg->data;
    send[1].iov_len = msg->size;

    if (msg->num_fds) {
        nfds = msg->num_fds;
        fds = msg->fds;
    }

    if (iolock) {
        qemu_mutex_unlock_iothread();
    }

    (void)qio_channel_writev_full_all(ioc, send, G_N_ELEMENTS(send), fds, nfds,
                                      &local_err);

    if (iolock) {
        qemu_mutex_lock_iothread();
    }

    if (errp) {
        error_propagate(errp, local_err);
    } else if (local_err) {
        error_report_err(local_err);
    }

    return;
}

static ssize_t mpqemu_read(QIOChannel *ioc, void *buf, size_t len, int **fds,
                           size_t *nfds, Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    bool iolock = qemu_mutex_iothread_locked();
    struct iovec *iovp = &iov;
    Error *local_err = NULL;
    unsigned int niov = 1;
    size_t *l_nfds = nfds;
    int **l_fds = fds;
    ssize_t bytes = 0;
    size_t size;

    iov.iov_base = buf;
    iov.iov_len = len;
    size = iov.iov_len;

    while (size > 0) {
        bytes = qio_channel_readv_full(ioc, iovp, niov, l_fds, l_nfds,
                                       &local_err);

        if (bytes == QIO_CHANNEL_ERR_BLOCK) {
            if (iolock) {
                qemu_mutex_unlock_iothread();
            }

            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }

            if (iolock) {
                qemu_mutex_lock_iothread();
            }
            continue;
        }

        if (bytes <= 0) {
            error_propagate(errp, local_err);
            return -EIO;
        }

        l_fds = NULL;
        l_nfds = NULL;

        size -= bytes;

        (void)iov_discard_front(&iovp, &niov, bytes);
    }

    return len - size;
}

void mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    Error *local_err = NULL;
    int *fds = NULL;
    size_t nfds = 0;
    ssize_t len;

    len = mpqemu_read(ioc, (void *)msg, MPQEMU_MSG_HDR_SIZE, &fds, &nfds,
                      &local_err);
    if (len < 0) {
        goto fail;
    } else if (len != MPQEMU_MSG_HDR_SIZE) {
        error_setg(&local_err, "Message header corrupted");
        goto fail;
    }

    if (msg->size > sizeof(msg->data)) {
        error_setg(&local_err, "Invalid size for message");
        goto fail;
    }

    if (mpqemu_read(ioc, (void *)&msg->data, msg->size, NULL, NULL,
                    &local_err) < 0) {
        goto fail;
    }

    msg->num_fds = nfds;
    if (nfds) {
        memcpy(msg->fds, fds, nfds * sizeof(int));
    }

fail:
    if (errp) {
        error_propagate(errp, local_err);
    } else if (local_err) {
        error_report_err(local_err);
    }
}

uint64_t mpqemu_msg_send_and_await_reply(MPQemuMsg *msg, PCIProxyDev *pdev,
                                         Error **errp)
{
    MPQemuMsg msg_reply = {0};
    uint64_t ret = UINT64_MAX;
    Error *local_err = NULL;

    qemu_mutex_unlock_iothread();
    qemu_mutex_lock(&pdev->io_mutex);

    mpqemu_msg_send(msg, pdev->ioc, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto exit_send;
    }

    mpqemu_msg_recv(&msg_reply, pdev->ioc, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto exit_send;
    }

    if (!mpqemu_msg_valid(&msg_reply) || msg_reply.cmd != RET_MSG) {
        error_setg(errp, "ERROR: Invalid reply received for command %d",
                         msg->cmd);
        goto exit_send;
    } else {
        ret = msg_reply.data.u64;
    }

 exit_send:
    qemu_mutex_unlock(&pdev->io_mutex);
    qemu_mutex_lock_iothread();

    return ret;
}

static void coroutine_fn mpqemu_msg_send_co(void *data)
{
    MPQemuRequest *req = (MPQemuRequest *)data;
    Error *local_err = NULL;

    mpqemu_msg_send(req->msg, req->ioc, &local_err);
    if (local_err) {
        error_report("ERROR: failed to send command to remote %d, ",
                     req->msg->cmd);
        req->finished = true;
        req->error = -EINVAL;
        return;
    }

    req->finished = true;
}

void mpqemu_msg_send_in_co(MPQemuRequest *req, QIOChannel *ioc,
                                  Error **errp)
{
    Coroutine *co;

    if (!req->ioc) {
        if (errp) {
            error_setg(errp, "Channel is set to NULL");
        } else {
            error_report("Channel is set to NULL");
        }
        return;
    }

    req->error = 0;
    req->finished = false;

    co = qemu_coroutine_create(mpqemu_msg_send_co, req);
    qemu_coroutine_enter(co);

    while (!req->finished) {
        aio_poll(qemu_get_aio_context(), true);
    }

    if (req->error) {
        if (errp) {
            error_setg(errp, "Error sending message to proxy, "
                             "error %d", req->error);
        } else {
            error_report("Error sending message to proxy, "
                         "error %d", req->error);
        }
    }

    return;
}

static void coroutine_fn mpqemu_msg_recv_co(void *data)
{
    MPQemuRequest *req = (MPQemuRequest *)data;
    Error *local_err = NULL;

    mpqemu_msg_recv(req->msg, req->ioc, &local_err);
    if (local_err) {
        error_report("ERROR: failed to send command to remote %d, ",
                     req->msg->cmd);
        req->finished = true;
        req->error = -EINVAL;
        return;
    }

    req->finished = true;
}

void mpqemu_msg_recv_in_co(MPQemuRequest *req, QIOChannel *ioc,
                               Error **errp)
{
    Coroutine *co;

    if (!req->ioc) {
        if (errp) {
            error_setg(errp, "Channel is set to NULL");
        } else {
            error_report("Channel is set to NULL");
        }
        return;
    }

    req->error = 0;
    req->finished = false;

    co = qemu_coroutine_create(mpqemu_msg_recv_co, req);
    qemu_coroutine_enter(co);

    while (!req->finished) {
        aio_poll(qemu_get_aio_context(), true);
    }

    if (req->error) {
        if (errp) {
            error_setg(errp, "Error sending message to proxy, "
                             "error %d", req->error);
        } else {
            error_report("Error sending message to proxy, "
                         "error %d", req->error);
        }
    }

    return;
}

bool mpqemu_msg_valid(MPQemuMsg *msg)
{
    if (msg->cmd >= MAX && msg->cmd < 0) {
        return false;
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
        if (msg->num_fds == 0 || msg->size != sizeof(SyncSysmemMsg)) {
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}
