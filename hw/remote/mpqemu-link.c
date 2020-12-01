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
#include "hw/remote/mpqemu-link.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/*
 * Send message over the ioc QIOChannel.
 * This function is safe to call from:
 * - From main loop in co-routine context. Will block the main loop if not in
 *   co-routine context;
 * - From vCPU thread with no co-routine context and if the channel is not part
 *   of the main loop handling;
 * - From IOThread within co-routine context, outside of co-routine context
 *   will block IOThread;
 */
void mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    bool iolock = qemu_mutex_iothread_locked();
    bool iothread = qemu_get_current_aio_context() == qemu_get_aio_context() ?
                    false : true;
    Error *local_err = NULL;
    struct iovec send[2] = {0};
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
    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    if (iothread) {
        assert(qemu_in_coroutine());
    }
    /*
     * Skip unlocking/locking iothread when in IOThread running
     * in co-routine context. Co-routine context is asserted above
     * for IOThread case.
     * Also skip this while in a co-routine in the main context.
     */
    if (iolock && !iothread && !qemu_in_coroutine()) {
        qemu_mutex_unlock_iothread();
    }

    (void)qio_channel_writev_full_all(ioc, send, G_N_ELEMENTS(send), fds, nfds,
                                      &local_err);

    if (iolock && !iothread && !qemu_in_coroutine()) {
        /* See above comment why skip locking here. */
        qemu_mutex_lock_iothread();
    }

    if (errp) {
        error_propagate(errp, local_err);
    } else if (local_err) {
        error_report_err(local_err);
    }

    return;
}

/*
 * Read message from the ioc QIOChannel.
 * This function is safe to call from:
 * - From main loop in co-routine context. Will block the main loop if not in
 *   co-routine context;
 * - From vCPU thread with no co-routine context and if the channel is not part
 *   of the main loop handling;
 * - From IOThread within co-routine context, outside of co-routine context
 *   will block IOThread;
 */
static ssize_t mpqemu_read(QIOChannel *ioc, void *buf, size_t len, int **fds,
                           size_t *nfds, Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    bool iolock = qemu_mutex_iothread_locked();
    bool iothread = qemu_get_current_aio_context() == qemu_get_aio_context()
                        ? false : true;
    struct iovec *iovp = &iov;
    Error *local_err = NULL;
    unsigned int niov = 1;
    size_t *l_nfds = nfds;
    int **l_fds = fds;
    ssize_t bytes = 0;
    size_t size;

    size = iov.iov_len;

    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    if (iothread) {
        assert(qemu_in_coroutine());
    }

    while (size > 0) {
        bytes = qio_channel_readv_full(ioc, iovp, niov, l_fds, l_nfds,
                                       &local_err);
        if (bytes == QIO_CHANNEL_ERR_BLOCK) {
            /*
             * Skip unlocking/locking iothread when in IOThread running
             * in co-routine context. Co-routine context is asserted above
             * for IOThread case.
             * Also skip this while in a co-routine in the main context.
             */
            if (iolock && !iothread && !qemu_in_coroutine()) {
                qemu_mutex_unlock_iothread();
            }
            if (qemu_in_coroutine()) {
                qio_channel_yield(ioc, G_IO_IN);
            } else {
                qio_channel_wait(ioc, G_IO_IN);
            }
            /* See above comment why skip locking here. */
            if (iolock && !iothread && !qemu_in_coroutine()) {
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
    if (!local_err) {
        if (len == -EIO) {
            error_setg(&local_err, "Connection closed.");
            goto fail;
        }
        if (len < 0) {
            error_setg(&local_err, "Message length is less than 0");
            goto fail;
        }
        if (len != MPQEMU_MSG_HDR_SIZE) {
            error_setg(&local_err, "Message header corrupted");
            goto fail;
        }
    } else {
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
    if (nfds > G_N_ELEMENTS(msg->fds)) {
        error_setg(&local_err,
                   "Overflow error: received %zu fds, more than max of %d fds",
                   nfds, REMOTE_MAX_FDS);
        goto fail;
    } else if (nfds) {
        memcpy(msg->fds, fds, nfds * sizeof(int));
    }

fail:
    while (local_err && nfds) {
        close(fds[nfds - 1]);
        nfds--;
    }

    g_free(fds);

    if (errp) {
        error_propagate(errp, local_err);
    } else if (local_err) {
        error_report_err(local_err);
    }
}

bool mpqemu_msg_valid(MPQemuMsg *msg)
{
    if (msg->cmd >= MPQEMU_CMD_MAX && msg->cmd < 0) {
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
