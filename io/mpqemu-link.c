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

    return true;
}
