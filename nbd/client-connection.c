/*
 * QEMU Block driver for  NBD
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "block/nbd.h"

#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"

struct NBDClientConnection {
    /* Initialization constants, never change */
    SocketAddress *saddr; /* address to connect to */
    QCryptoTLSCreds *tlscreds;
    NBDExportInfo initial_info;
    bool do_negotiation;
    bool do_retry;

    /* Used only by connection thread, no need in locking the mutex */
    bool has_prev_info;
    NBDExportInfo prev_info;

    QemuMutex mutex;

    /*
     * @sioc and @err represent a connection attempt.  While running
     * is true, they are only used by the connection thread, and mutex
     * locking is not needed.  Once the thread finishes,
     * nbd_co_establish_connection then steals these pointers while
     * under the mutex.
     */
    NBDExportInfo updated_info;
    QIOChannelSocket *sioc;
    QIOChannel *ioc;
    Error *err;

    /* All further fields are accessed only under mutex */
    bool running; /* thread is running now */
    bool detached; /* thread is detached and should cleanup the state */

    /*
     * wait_co: if non-NULL, which coroutine to wake in
     * nbd_co_establish_connection() after yield()
     */
    Coroutine *wait_co;
};

/*
 * The function isn't protected by any mutex, only call it when the client
 * connection attempt has not yet started.
 */
void nbd_client_connection_enable_retry(NBDClientConnection *conn)
{
    conn->do_retry = true;
}

NBDClientConnection *nbd_client_connection_new(const SocketAddress *saddr,
                                               bool do_negotiation,
                                               const char *export_name,
                                               const char *x_dirty_bitmap,
                                               QCryptoTLSCreds *tlscreds)
{
    NBDClientConnection *conn = g_new(NBDClientConnection, 1);

    object_ref(OBJECT(tlscreds));
    *conn = (NBDClientConnection) {
        .saddr = QAPI_CLONE(SocketAddress, saddr),
        .tlscreds = tlscreds,
        .do_negotiation = do_negotiation,

        .initial_info.request_sizes = true,
        .initial_info.structured_reply = true,
        .initial_info.base_allocation = true,
        .initial_info.x_dirty_bitmap = g_strdup(x_dirty_bitmap),
        .initial_info.name = g_strdup(export_name ?: "")
    };

    qemu_mutex_init(&conn->mutex);

    return conn;
}

static void nbd_client_connection_do_free(NBDClientConnection *conn)
{
    if (conn->sioc) {
        qio_channel_close(QIO_CHANNEL(conn->sioc), NULL);
        object_unref(OBJECT(conn->sioc));
    }
    error_free(conn->err);
    qapi_free_SocketAddress(conn->saddr);
    object_unref(OBJECT(conn->tlscreds));
    g_free(conn->initial_info.x_dirty_bitmap);
    g_free(conn->initial_info.name);
    g_free(conn);
}

/*
 * Connect to @addr and do NBD negotiation if @info is not null. If @tlscreds
 * are given @outioc is returned. @outioc is provided only on success.  The call
 * may be cancelled from other thread by simply qio_channel_shutdown(sioc).
 */
static int nbd_connect(QIOChannelSocket *sioc, SocketAddress *addr,
                       NBDExportInfo *info, QCryptoTLSCreds *tlscreds,
                       QIOChannel **outioc, Error **errp)
{
    int ret;

    if (outioc) {
        *outioc = NULL;
    }

    ret = qio_channel_socket_connect_sync(sioc, addr, errp);
    if (ret < 0) {
        return ret;
    }

    qio_channel_set_delay(QIO_CHANNEL(sioc), false);

    if (!info) {
        return 0;
    }

    ret = nbd_receive_negotiate(NULL, QIO_CHANNEL(sioc), tlscreds,
                                tlscreds ? addr->u.inet.host : NULL,
                                outioc, info, errp);
    if (ret < 0) {
        /*
         * nbd_receive_negotiate() may setup tls ioc and return it even on
         * failure path. In this case we should use it instead of original
         * channel.
         */
        if (outioc && *outioc) {
            qio_channel_close(QIO_CHANNEL(*outioc), NULL);
            object_unref(OBJECT(*outioc));
            *outioc = NULL;
        } else {
            qio_channel_close(QIO_CHANNEL(sioc), NULL);
        }

        return ret;
    }

    return 0;
}

static bool nbd_is_new_info_compatible(NBDExportInfo *old, NBDExportInfo *new,
                                       Error **errp)
{
    uint32_t dropped_flags;

    if (old->structured_reply && !new->structured_reply) {
        error_setg(errp, "Server options degrade after reconnect: "
                   "structured_reply is not supported anymore");
        return false;
    }

    if (old->base_allocation && !new->base_allocation) {
        error_setg(errp, "Server options degrade after reconnect: "
                   "base_allocation is not supported anymore");
        return false;
    }

    if (old->size != new->size) {
        error_setg(errp, "NBD export size changed after reconnect");
        return false;
    }

    /*
     * No worry if rotational status changed. But other flags are feature flags,
     * they should not degrade.
     */
    dropped_flags = (old->flags & ~new->flags) & ~NBD_FLAG_ROTATIONAL;
    if (dropped_flags) {
        error_setg(errp, "Server options degrade after reconnect: flags 0x%"
                   PRIx32 " are not reported anymore", dropped_flags);
        return false;
    }

    if (new->min_block > old->min_block) {
        error_setg(errp, "Server requires more strict min_block after "
                   "reconnect: %" PRIu32 " instead of %" PRIu32,
                   new->min_block, old->min_block);
        return false;
    }
    if (new->min_block && (old->min_block % new->min_block)) {
        error_setg(errp, "Server requires new min_block %" PRIu32
                   " after reconnect, incompatible with old one %" PRIu32,
                   new->min_block, old->min_block);
        return false;
    }

    if (new->max_block < old->max_block) {
        error_setg(errp, "Server requires more strict max_block after "
                   "reconnect: %" PRIu32 " instead of %" PRIu32,
                   new->max_block, old->max_block);
        return false;
    }

    if (old->context_id != new->context_id) {
        error_setg(errp, "Meta context id changed after reconnect");
        return false;
    }

    return true;
}

static void *connect_thread_func(void *opaque)
{
    NBDClientConnection *conn = opaque;
    int ret;
    bool do_free;
    uint64_t timeout = 1;
    uint64_t max_timeout = 16;

    qemu_mutex_lock(&conn->mutex);
    while (!conn->detached) {
        assert(!conn->sioc);
        conn->sioc = qio_channel_socket_new();

        qemu_mutex_unlock(&conn->mutex);

        error_free(conn->err);
        conn->err = NULL;
        conn->updated_info = conn->initial_info;

        ret = nbd_connect(conn->sioc, conn->saddr,
                          conn->do_negotiation ? &conn->updated_info : NULL,
                          conn->tlscreds, &conn->ioc, &conn->err);

        if (ret == 0) {
            if (conn->has_prev_info &&
                !nbd_is_new_info_compatible(&conn->prev_info,
                                            &conn->updated_info, &conn->err))
            {
                NBDRequest request = { .type = NBD_CMD_DISC };
                QIOChannel *ioc = conn->ioc ?: QIO_CHANNEL(conn->sioc);

                nbd_send_request(ioc, &request);
                qio_channel_close(ioc, NULL);

                object_unref(OBJECT(conn->ioc));
                conn->ioc = NULL;

                ret = -EINVAL;
            } else {
                conn->prev_info = conn->updated_info;
                conn->has_prev_info = true;
            }
        }

        /*
         * conn->updated_info will finally be returned to the user. Clear the
         * pointers to our internally allocated strings, which are IN parameters
         * of nbd_receive_negotiate() and therefore nbd_connect(). Caller
         * shoudn't be interested in these fields.
         */
        conn->updated_info.x_dirty_bitmap = NULL;
        conn->updated_info.name = NULL;

        qemu_mutex_lock(&conn->mutex);

        if (ret < 0) {
            object_unref(OBJECT(conn->sioc));
            conn->sioc = NULL;
            if (conn->do_retry && !conn->detached) {
                qemu_mutex_unlock(&conn->mutex);

                sleep(timeout);
                if (timeout < max_timeout) {
                    timeout *= 2;
                }

                qemu_mutex_lock(&conn->mutex);
                continue;
            }
        }

        break;
    }

    /* mutex is locked */

    assert(conn->running);
    conn->running = false;
    if (conn->wait_co) {
        aio_co_wake(conn->wait_co);
        conn->wait_co = NULL;
    }
    do_free = conn->detached;

    qemu_mutex_unlock(&conn->mutex);

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }

    return NULL;
}

void nbd_client_connection_release(NBDClientConnection *conn)
{
    bool do_free = false;

    if (!conn) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        assert(!conn->detached);
        if (conn->running) {
            conn->detached = true;
        } else {
            do_free = true;
        }
        if (conn->sioc) {
            qio_channel_shutdown(QIO_CHANNEL(conn->sioc),
                                 QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
    }

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }
}

/*
 * Get a new connection in context of @conn:
 *   if the thread is running, wait for completion
 *   if the thread already succeeded in the background, and user didn't get the
 *     result, just return it now
 *   otherwise the thread is not running, so start a thread and wait for
 *     completion
 *
 * If @blocking is false, don't wait for the thread, return immediately.
 *
 * If @info is not NULL, also do nbd-negotiation after successful connection.
 * In this case info is used only as out parameter, and is fully initialized by
 * nbd_co_establish_connection(). "IN" fields of info as well as related only to
 * nbd_receive_export_list() would be zero (see description of NBDExportInfo in
 * include/block/nbd.h).
 */
QIOChannel *coroutine_fn
nbd_co_establish_connection(NBDClientConnection *conn, NBDExportInfo *info,
                            bool blocking, Error **errp)
{
    QemuThread thread;

    if (conn->do_negotiation) {
        assert(info);
    }

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        /*
         * Don't call nbd_co_establish_connection() in several coroutines in
         * parallel. Only one call at once is supported.
         */
        assert(!conn->wait_co);

        if (!conn->running) {
            if (conn->sioc) {
                /* Previous attempt finally succeeded in background */
                if (conn->do_negotiation) {
                    memcpy(info, &conn->updated_info, sizeof(*info));
                    if (conn->ioc) {
                        /* TLS channel now has own reference to parent */
                        object_unref(OBJECT(conn->sioc));
                        conn->sioc = NULL;

                        return g_steal_pointer(&conn->ioc);
                    }
                }

                assert(!conn->ioc);

                return QIO_CHANNEL(g_steal_pointer(&conn->sioc));
            }

            conn->running = true;
            error_free(conn->err);
            conn->err = NULL;
            qemu_thread_create(&thread, "nbd-connect",
                               connect_thread_func, conn, QEMU_THREAD_DETACHED);
        }

        if (!blocking) {
            error_setg(errp, "No connection at the moment");
            return NULL;
        }

        conn->wait_co = qemu_coroutine_self();
    }

    /*
     * We are going to wait for connect-thread finish, but
     * nbd_co_establish_connection_cancel() can interrupt.
     */
    qemu_coroutine_yield();

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        if (conn->running) {
            /*
             * The connection attempt was canceled and the coroutine resumed
             * before the connection thread finished its job.  Report the
             * attempt as failed, but leave the connection thread running,
             * to reuse it for the next connection attempt.
             */
            error_setg(errp, "Connection attempt cancelled by other operation");
            return NULL;
        } else {
            error_propagate(errp, conn->err);
            conn->err = NULL;
            if (!conn->sioc) {
                return NULL;
            }
            if (conn->do_negotiation) {
                memcpy(info, &conn->updated_info, sizeof(*info));
                if (conn->ioc) {
                    /* TLS channel now has own reference to parent */
                    object_unref(OBJECT(conn->sioc));
                    conn->sioc = NULL;

                    return g_steal_pointer(&conn->ioc);
                }
            }

            assert(!conn->ioc);

            return QIO_CHANNEL(g_steal_pointer(&conn->sioc));
        }
    }

    abort(); /* unreachable */
}

/*
 * nbd_co_establish_connection_cancel
 * Cancel nbd_co_establish_connection() asynchronously.
 *
 * Note that this function neither directly stops the thread nor closes the
 * socket, but rather safely wakes nbd_co_establish_connection() which is
 * sleeping in yield()
 */
void nbd_co_establish_connection_cancel(NBDClientConnection *conn)
{
    Coroutine *wait_co;

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        wait_co = g_steal_pointer(&conn->wait_co);
    }

    if (wait_co) {
        aio_co_wake(wait_co);
    }
}
