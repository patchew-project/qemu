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

    return true;
}
