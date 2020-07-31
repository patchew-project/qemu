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

    return true;
}

void mpqemu_msg_cleanup(MPQemuMsg *msg)
{
    if (msg->data2) {
        free(msg->data2);
    }
}
