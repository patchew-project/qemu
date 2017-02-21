/*
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device Common Code
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "nbd-internal.h"

ssize_t nbd_wr_syncv(QIOChannel *ioc,
                     struct iovec *iov,
                     size_t niov,
                     size_t length,
                     bool do_read)
{
    ssize_t done = 0;
    Error *local_err = NULL;
    struct iovec *local_iov = g_new(struct iovec, niov);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = niov;

    nlocal_iov = iov_copy(local_iov, nlocal_iov, iov, niov, 0, length);

    while (nlocal_iov > 0) {
        ssize_t len;
        if (do_read) {
            len = qio_channel_readv(ioc, local_iov, nlocal_iov, &local_err);
        } else {
            len = qio_channel_writev(ioc, local_iov, nlocal_iov, &local_err);
        }
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            if (qemu_in_coroutine()) {
                /* XXX figure out if we can create a variant on
                 * qio_channel_yield() that works with AIO contexts
                 * and consider using that in this branch */
                qemu_coroutine_yield();
            } else if (done) {
                /* XXX this is needed by nbd_reply_ready.  */
                qio_channel_wait(ioc,
                                 do_read ? G_IO_IN : G_IO_OUT);
            } else {
                return -EAGAIN;
            }
            continue;
        }
        if (len < 0) {
            TRACE("I/O error: %s", error_get_pretty(local_err));
            error_free(local_err);
            /* XXX handle Error objects */
            done = -EIO;
            goto cleanup;
        }

        if (do_read && len == 0) {
            break;
        }

        iov_discard_front(&local_iov, &nlocal_iov, len);
        done += len;
    }

 cleanup:
    g_free(local_iov_head);
    return done;
}


void nbd_tls_handshake(QIOTask *task,
                       void *opaque)
{
    struct NBDTLSHandshakeData *data = opaque;

    if (qio_task_propagate_error(task, &data->error)) {
        TRACE("TLS failed %s", error_get_pretty(data->error));
    }
    data->complete = true;
    g_main_loop_quit(data->loop);
}


const char *nbd_opt_lookup(uint32_t opt)
{
    switch (opt) {
    case NBD_OPT_EXPORT_NAME:
        return "export name";
    case NBD_OPT_ABORT:
        return "abort";
    case NBD_OPT_LIST:
        return "list";
    case NBD_OPT_STARTTLS:
        return "starttls";
    case NBD_OPT_INFO:
        return "info";
    case NBD_OPT_GO:
        return "go";
    default:
        return "<unknown>";
    }
}


const char *nbd_rep_lookup(uint32_t rep)
{
    switch (rep) {
    case NBD_REP_ACK:
        return "ack";
    case NBD_REP_SERVER:
        return "server";
    case NBD_REP_INFO:
        return "info";
    case NBD_REP_ERR_UNSUP:
        return "unsupported";
    case NBD_REP_ERR_POLICY:
        return "denied by policy";
    case NBD_REP_ERR_INVALID:
        return "invalid";
    case NBD_REP_ERR_PLATFORM:
        return "platform lacks support";
    case NBD_REP_ERR_TLS_REQD:
        return "TLS required";
    case NBD_REP_ERR_UNKNOWN:
        return "export unknown";
    case NBD_REP_ERR_SHUTDOWN:
        return "server shutting down";
    case NBD_REP_ERR_BLOCK_SIZE_REQD:
        return "block size required";
    default:
        return "<unknown>";
    }
}


const char *nbd_info_lookup(uint16_t info)
{
    switch (info) {
    case NBD_INFO_EXPORT:
        return "export";
    case NBD_INFO_NAME:
        return "name";
    case NBD_INFO_DESCRIPTION:
        return "description";
    case NBD_INFO_BLOCK_SIZE:
        return "block size";
    default:
        return "<unknown>";
    }
}
