/*
 *  Copyright (C) 2016-2017 Red Hat, Inc.
 *  Copyright (C) 2005  Anthony Liguori <anthony@codemonkey.ws>
 *
 *  Network Block Device Client Side
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

static int nbd_errno_to_system_errno(int err)
{
    int ret;
    switch (err) {
    case NBD_SUCCESS:
        ret = 0;
        break;
    case NBD_EPERM:
        ret = EPERM;
        break;
    case NBD_EIO:
        ret = EIO;
        break;
    case NBD_ENOMEM:
        ret = ENOMEM;
        break;
    case NBD_ENOSPC:
        ret = ENOSPC;
        break;
    case NBD_ESHUTDOWN:
        ret = ESHUTDOWN;
        break;
    default:
        TRACE("Squashing unexpected error %d to EINVAL", err);
        /* fallthrough */
    case NBD_EINVAL:
        ret = EINVAL;
        break;
    }
    return ret;
}

/* Definitions for opaque data types */

static QTAILQ_HEAD(, NBDExport) exports = QTAILQ_HEAD_INITIALIZER(exports);

/* That's all folks */

/* Basic flow for negotiation

   Server         Client
   Negotiate

   or

   Server         Client
   Negotiate #1
                  Option
   Negotiate #2

   ----

   followed by

   Server         Client
                  Request
   Response
                  Request
   Response
                  ...
   ...
                  Request (type == 2)

*/

/* Discard length bytes from channel.  Return -errno on failure, or
 * the amount of bytes consumed. */
static ssize_t drop_sync(QIOChannel *ioc, size_t size)
{
    ssize_t ret = 0;
    char small[1024];
    char *buffer;

    buffer = sizeof(small) > size ? small : g_malloc(MIN(65536, size));
    while (size > 0) {
        ssize_t count = read_sync(ioc, buffer, MIN(65536, size));

        if (count <= 0) {
            goto cleanup;
        }
        assert(count <= size);
        size -= count;
        ret += count;
    }

 cleanup:
    if (buffer != small) {
        g_free(buffer);
    }
    return ret;
}

/* Send an option request.
 *
 * The request is for option @opt, with @data containing @len bytes of
 * additional payload for the request (@len may be -1 to treat @data as
 * a C string; and @data may be NULL if @len is 0).
 * Return 0 if successful, -1 with errp set if it is impossible to
 * continue. */
static int nbd_send_option_request(QIOChannel *ioc, uint32_t opt,
                                   uint32_t len, const char *data,
                                   Error **errp)
{
    nbd_option req;
    QEMU_BUILD_BUG_ON(sizeof(req) != 16);

    if (len == -1) {
        req.length = len = strlen(data);
    }
    TRACE("Sending option request %" PRIu32" (%s), len %" PRIu32, opt,
          nbd_opt_lookup(opt), len);

    stq_be_p(&req.magic, NBD_OPTS_MAGIC);
    stl_be_p(&req.option, opt);
    stl_be_p(&req.length, len);

    if (write_sync(ioc, &req, sizeof(req)) != sizeof(req)) {
        error_setg(errp, "Failed to send option request header");
        return -1;
    }

    if (len && write_sync(ioc, (char *) data, len) != len) {
        error_setg(errp, "Failed to send option request data");
        return -1;
    }

    return 0;
}

/* Send NBD_OPT_ABORT as a courtesy to let the server know that we are
 * not going to attempt further negotiation. */
static void nbd_send_opt_abort(QIOChannel *ioc)
{
    /* Technically, a compliant server is supposed to reply to us; but
     * older servers disconnected instead. At any rate, we're allowed
     * to disconnect without waiting for the server reply, so we don't
     * even care if the request makes it to the server, let alone
     * waiting around for whether the server replies. */
    nbd_send_option_request(ioc, NBD_OPT_ABORT, 0, NULL, NULL);
}


/* Receive the header of an option reply, which should match the given
 * opt.  Read through the length field, but NOT the length bytes of
 * payload. Return 0 if successful, -1 with errp set if it is
 * impossible to continue. */
static int nbd_receive_option_reply(QIOChannel *ioc, uint32_t opt,
                                    nbd_opt_reply *reply, Error **errp)
{
    QEMU_BUILD_BUG_ON(sizeof(*reply) != 20);
    if (read_sync(ioc, reply, sizeof(*reply)) != sizeof(*reply)) {
        error_setg(errp, "failed to read option reply");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    be64_to_cpus(&reply->magic);
    be32_to_cpus(&reply->option);
    be32_to_cpus(&reply->type);
    be32_to_cpus(&reply->length);

    TRACE("Received option reply %" PRIx32" (%s), type %" PRIx32
          " (%s), len %" PRIu32,
          reply->option, nbd_opt_lookup(reply->option),
          reply->type, nbd_rep_lookup(reply->type), reply->length);

    if (reply->magic != NBD_REP_MAGIC) {
        error_setg(errp, "Unexpected option reply magic");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (reply->option != opt) {
        error_setg(errp, "Unexpected option type %x expected %x",
                   reply->option, opt);
        nbd_send_opt_abort(ioc);
        return -1;
    }
    return 0;
}

/* If reply represents success, return 1 without further action.
 * If reply represents an error, consume the optional payload of
 * the packet on ioc.  Then return 0 for unsupported (so the client
 * can fall back to other approaches), or -1 with errp set for other
 * errors.
 */
static int nbd_handle_reply_err(QIOChannel *ioc, nbd_opt_reply *reply,
                                Error **errp)
{
    char *msg = NULL;
    int result = -1;

    if (!(reply->type & (1 << 31))) {
        return 1;
    }

    if (reply->length) {
        if (reply->length > NBD_MAX_BUFFER_SIZE) {
            error_setg(errp, "server error 0x%" PRIx32
                       " (%s) message is too long",
                       reply->type, nbd_rep_lookup(reply->type));
            goto cleanup;
        }
        msg = g_malloc(reply->length + 1);
        if (read_sync(ioc, msg, reply->length) != reply->length) {
            error_setg(errp, "failed to read option error 0x%" PRIx32
                       " (%s) message",
                       reply->type, nbd_rep_lookup(reply->type));
            goto cleanup;
        }
        msg[reply->length] = '\0';
    }

    switch (reply->type) {
    case NBD_REP_ERR_UNSUP:
        TRACE("server doesn't understand request %" PRIx32
              " (%s), attempting fallback",
              reply->option, nbd_opt_lookup(reply->option));
        result = 0;
        goto cleanup;

    case NBD_REP_ERR_POLICY:
        error_setg(errp, "Denied by server for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_INVALID:
        error_setg(errp, "Invalid data length for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_PLATFORM:
        error_setg(errp, "Server lacks support for option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_TLS_REQD:
        error_setg(errp, "TLS negotiation required before option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_UNKNOWN:
        error_setg(errp, "Requested export not available for option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_SHUTDOWN:
        error_setg(errp, "Server shutting down before option %" PRIx32 " (%s)",
                   reply->option, nbd_opt_lookup(reply->option));
        break;

    case NBD_REP_ERR_BLOCK_SIZE_REQD:
        error_setg(errp, "Server requires INFO_BLOCK_SIZE for option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;

    default:
        error_setg(errp, "Unknown error code when asking for option %" PRIx32
                   " (%s)", reply->option, nbd_opt_lookup(reply->option));
        break;
    }

    if (msg) {
        error_append_hint(errp, "%s\n", msg);
    }

 cleanup:
    g_free(msg);
    if (result < 0) {
        nbd_send_opt_abort(ioc);
    }
    return result;
}

/* Process another portion of the NBD_OPT_LIST reply.  Set *@match if
 * the current reply matches @want or if the server does not support
 * NBD_OPT_LIST, otherwise leave @match alone.  Return 0 if iteration
 * is complete, positive if more replies are expected, or negative
 * with @errp set if an unrecoverable error occurred. */
static int nbd_receive_list(QIOChannel *ioc, const char *want, bool *match,
                            Error **errp)
{
    nbd_opt_reply reply;
    uint32_t len;
    uint32_t namelen;
    char name[NBD_MAX_NAME_SIZE + 1];
    int error;

    if (nbd_receive_option_reply(ioc, NBD_OPT_LIST, &reply, errp) < 0) {
        return -1;
    }
    error = nbd_handle_reply_err(ioc, &reply, errp);
    if (error <= 0) {
        /* The server did not support NBD_OPT_LIST, so set *match on
         * the assumption that any name will be accepted.  */
        *match = true;
        return error;
    }
    len = reply.length;

    if (reply.type == NBD_REP_ACK) {
        if (len != 0) {
            error_setg(errp, "length too long for option end");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        return 0;
    } else if (reply.type != NBD_REP_SERVER) {
        error_setg(errp, "Unexpected reply type %" PRIx32 " expected %x",
                   reply.type, NBD_REP_SERVER);
        nbd_send_opt_abort(ioc);
        return -1;
    }

    if (len < sizeof(namelen) || len > NBD_MAX_BUFFER_SIZE) {
        error_setg(errp, "incorrect option length %" PRIu32, len);
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (read_sync(ioc, &namelen, sizeof(namelen)) != sizeof(namelen)) {
        error_setg(errp, "failed to read option name length");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    namelen = be32_to_cpu(namelen);
    len -= sizeof(namelen);
    if (len < namelen) {
        error_setg(errp, "incorrect option name length");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (namelen != strlen(want)) {
        if (drop_sync(ioc, len) != len) {
            error_setg(errp, "failed to skip export name with wrong length");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        return 1;
    }

    assert(namelen < sizeof(name));
    if (read_sync(ioc, name, namelen) != namelen) {
        error_setg(errp, "failed to read export name");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    name[namelen] = '\0';
    len -= namelen;
    if (drop_sync(ioc, len) != len) {
        error_setg(errp, "failed to read export description");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (!strcmp(name, want)) {
        *match = true;
    }
    return 1;
}


/* Returns -1 if NBD_OPT_GO proves the export @wantname cannot be
 * used, 0 if NBD_OPT_GO is unsupported (fall back to NBD_OPT_LIST and
 * NBD_OPT_EXPORT_NAME in that case), and > 0 if the export is good to
 * go (with @info populated). */
static int nbd_opt_go(QIOChannel *ioc, const char *wantname,
                      NBDExportInfo *info, Error **errp)
{
    nbd_opt_reply reply;
    uint32_t len = strlen(wantname);
    uint16_t type;
    int error;
    char *buf;

    /* The protocol requires that the server send NBD_INFO_EXPORT with
     * a non-zero flags (at least NBD_FLAG_HAS_FLAGS must be set); so
     * flags still 0 is a witness of a broken server. */
    info->flags = 0;

    TRACE("Attempting NBD_OPT_GO for export '%s'", wantname);
    buf = g_malloc(2 + 4 + len + 1);
    stw_be_p(buf, 0); /* No requests, live with whatever server sends */
    stl_be_p(buf + 2, len);
    memcpy(buf + 6, wantname, len);
    if (nbd_send_option_request(ioc, NBD_OPT_GO, len + 6, buf, errp) < 0) {
        return -1;
    }

    TRACE("Reading export info");
    while (1) {
        if (nbd_receive_option_reply(ioc, NBD_OPT_GO, &reply, errp) < 0) {
            return -1;
        }
        error = nbd_handle_reply_err(ioc, &reply, errp);
        if (error <= 0) {
            return error;
        }
        len = reply.length;

        if (reply.type == NBD_REP_ACK) {
            /* Server is done sending info and moved into transmission
               phase, but make sure it sent flags */
            if (len) {
                error_setg(errp, "server sent invalid NBD_REP_ACK");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (!info->flags) {
                error_setg(errp, "broken server omitted NBD_INFO_EXPORT");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            TRACE("export is good to go");
            return 1;
        }
        if (reply.type != NBD_REP_INFO) {
            error_setg(errp, "unexpected reply type %" PRIx32 ", expected %x",
                       reply.type, NBD_REP_INFO);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (len < sizeof(type)) {
            error_setg(errp, "NBD_REP_INFO length %" PRIu32 " is too short",
                       len);
            nbd_send_opt_abort(ioc);
            return -1;
        }
        if (read_sync(ioc, &type, sizeof(type)) != sizeof(type)) {
            error_setg(errp, "failed to read info type");
            nbd_send_opt_abort(ioc);
            return -1;
        }
        len -= sizeof(type);
        be16_to_cpus(&type);
        switch (type) {
        case NBD_INFO_EXPORT:
            if (len != sizeof(info->size) + sizeof(info->flags)) {
                error_setg(errp, "remaining export info len %" PRIu32
                           " is unexpected size", len);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            if (read_sync(ioc, &info->size, sizeof(info->size)) !=
                sizeof(info->size)) {
                error_setg(errp, "failed to read info size");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be64_to_cpus(&info->size);
            if (read_sync(ioc, &info->flags, sizeof(info->flags)) !=
                sizeof(info->flags)) {
                error_setg(errp, "failed to read info flags");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            be16_to_cpus(&info->flags);
            TRACE("Size is %" PRIu64 ", export flags %" PRIx16,
                  info->size, info->flags);
            break;

        default:
            TRACE("ignoring unknown export info %" PRIu16 " (%s)", type,
                  nbd_info_lookup(type));
            if (drop_sync(ioc, len) != len) {
                error_setg(errp, "Failed to read info payload");
                nbd_send_opt_abort(ioc);
                return -1;
            }
            break;
        }
    }
}

/* Return -1 on failure, 0 if wantname is an available export. */
static int nbd_receive_query_exports(QIOChannel *ioc,
                                     const char *wantname,
                                     Error **errp)
{
    bool foundExport = false;

    TRACE("Querying export list for '%s'", wantname);
    if (nbd_send_option_request(ioc, NBD_OPT_LIST, 0, NULL, errp) < 0) {
        return -1;
    }

    TRACE("Reading available export names");
    while (1) {
        int ret = nbd_receive_list(ioc, wantname, &foundExport, errp);

        if (ret < 0) {
            /* Server gave unexpected reply */
            return -1;
        } else if (ret == 0) {
            /* Done iterating. */
            if (!foundExport) {
                error_setg(errp, "No export with name '%s' available",
                           wantname);
                nbd_send_opt_abort(ioc);
                return -1;
            }
            TRACE("Found desired export name '%s'", wantname);
            return 0;
        }
    }
}

static QIOChannel *nbd_receive_starttls(QIOChannel *ioc,
                                        QCryptoTLSCreds *tlscreds,
                                        const char *hostname, Error **errp)
{
    nbd_opt_reply reply;
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    TRACE("Requesting TLS from server");
    if (nbd_send_option_request(ioc, NBD_OPT_STARTTLS, 0, NULL, errp) < 0) {
        return NULL;
    }

    TRACE("Getting TLS reply from server");
    if (nbd_receive_option_reply(ioc, NBD_OPT_STARTTLS, &reply, errp) < 0) {
        return NULL;
    }

    if (reply.type != NBD_REP_ACK) {
        error_setg(errp, "Server rejected request to start TLS %" PRIx32,
                   reply.type);
        nbd_send_opt_abort(ioc);
        return NULL;
    }

    if (reply.length != 0) {
        error_setg(errp, "Start TLS response was not zero %" PRIu32,
                   reply.length);
        nbd_send_opt_abort(ioc);
        return NULL;
    }

    TRACE("TLS request approved, setting up TLS");
    tioc = qio_channel_tls_new_client(ioc, tlscreds, hostname, errp);
    if (!tioc) {
        return NULL;
    }
    qio_channel_set_name(QIO_CHANNEL(tioc), "nbd-client-tls");
    data.loop = g_main_loop_new(g_main_context_default(), FALSE);
    TRACE("Starting TLS handshake");
    qio_channel_tls_handshake(tioc,
                              nbd_tls_handshake,
                              &data,
                              NULL);

    if (!data.complete) {
        g_main_loop_run(data.loop);
    }
    g_main_loop_unref(data.loop);
    if (data.error) {
        error_propagate(errp, data.error);
        object_unref(OBJECT(tioc));
        return NULL;
    }

    return QIO_CHANNEL(tioc);
}


int nbd_receive_negotiate(QIOChannel *ioc, const char *name,
                          QCryptoTLSCreds *tlscreds, const char *hostname,
                          QIOChannel **outioc, NBDExportInfo *info,
                          Error **errp)
{
    char buf[256];
    uint64_t magic;
    int rc;
    bool zeroes = true;

    TRACE("Receiving negotiation tlscreds=%p hostname=%s.",
          tlscreds, hostname ? hostname : "<null>");

    rc = -EINVAL;

    if (outioc) {
        *outioc = NULL;
    }
    if (tlscreds && !outioc) {
        error_setg(errp, "Output I/O channel required for TLS");
        goto fail;
    }

    if (read_sync(ioc, buf, 8) != 8) {
        error_setg(errp, "Failed to read data");
        goto fail;
    }

    buf[8] = '\0';
    if (strlen(buf) == 0) {
        error_setg(errp, "Server connection closed unexpectedly");
        goto fail;
    }

    TRACE("Magic is %c%c%c%c%c%c%c%c",
          qemu_isprint(buf[0]) ? buf[0] : '.',
          qemu_isprint(buf[1]) ? buf[1] : '.',
          qemu_isprint(buf[2]) ? buf[2] : '.',
          qemu_isprint(buf[3]) ? buf[3] : '.',
          qemu_isprint(buf[4]) ? buf[4] : '.',
          qemu_isprint(buf[5]) ? buf[5] : '.',
          qemu_isprint(buf[6]) ? buf[6] : '.',
          qemu_isprint(buf[7]) ? buf[7] : '.');

    if (memcmp(buf, "NBDMAGIC", 8) != 0) {
        error_setg(errp, "Invalid magic received");
        goto fail;
    }

    if (read_sync(ioc, &magic, sizeof(magic)) != sizeof(magic)) {
        error_setg(errp, "Failed to read magic");
        goto fail;
    }
    magic = be64_to_cpu(magic);
    TRACE("Magic is 0x%" PRIx64, magic);

    if (magic == NBD_OPTS_MAGIC) {
        uint32_t clientflags = 0;
        uint16_t globalflags;
        bool fixedNewStyle = false;

        if (read_sync(ioc, &globalflags, sizeof(globalflags)) !=
            sizeof(globalflags)) {
            error_setg(errp, "Failed to read server flags");
            goto fail;
        }
        globalflags = be16_to_cpu(globalflags);
        TRACE("Global flags are %" PRIx32, globalflags);
        if (globalflags & NBD_FLAG_FIXED_NEWSTYLE) {
            fixedNewStyle = true;
            TRACE("Server supports fixed new style");
            clientflags |= NBD_FLAG_C_FIXED_NEWSTYLE;
        }
        if (globalflags & NBD_FLAG_NO_ZEROES) {
            zeroes = false;
            TRACE("Server supports no zeroes");
            clientflags |= NBD_FLAG_C_NO_ZEROES;
        }
        /* client requested flags */
        clientflags = cpu_to_be32(clientflags);
        if (write_sync(ioc, &clientflags, sizeof(clientflags)) !=
            sizeof(clientflags)) {
            error_setg(errp, "Failed to send clientflags field");
            goto fail;
        }
        if (tlscreds) {
            if (fixedNewStyle) {
                *outioc = nbd_receive_starttls(ioc, tlscreds, hostname, errp);
                if (!*outioc) {
                    goto fail;
                }
                ioc = *outioc;
            } else {
                error_setg(errp, "Server does not support STARTTLS");
                goto fail;
            }
        }
        if (!name) {
            TRACE("Using default NBD export name \"\"");
            name = "";
        }
        if (fixedNewStyle) {
            int result;

            /* Try NBD_OPT_GO first - if it works, we are done (it
             * also gives us a good message if the server requires
             * TLS).  If it is not available, fall back to
             * NBD_OPT_LIST for nicer error messages about a missing
             * export, then use NBD_OPT_EXPORT_NAME.  */
            result = nbd_opt_go(ioc, name, info, errp);
            if (result < 0) {
                goto fail;
            }
            if (result > 0) {
                return 0;
            }
            /* Check our desired export is present in the
             * server export list. Since NBD_OPT_EXPORT_NAME
             * cannot return an error message, running this
             * query gives us better error reporting if the
             * export name is not available.
             */
            if (nbd_receive_query_exports(ioc, name, errp) < 0) {
                goto fail;
            }
        }
        /* write the export name request */
        if (nbd_send_option_request(ioc, NBD_OPT_EXPORT_NAME, -1, name,
                                    errp) < 0) {
            goto fail;
        }

        /* Read the response */
        if (read_sync(ioc, &info->size, sizeof(info->size)) !=
            sizeof(info->size)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        be64_to_cpus(&info->size);

        if (read_sync(ioc, &info->flags, sizeof(info->flags)) !=
            sizeof(info->flags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        be16_to_cpus(&info->flags);
    } else if (magic == NBD_CLIENT_MAGIC) {
        uint32_t oldflags;

        if (name) {
            error_setg(errp, "Server does not support export names");
            goto fail;
        }
        if (tlscreds) {
            error_setg(errp, "Server does not support STARTTLS");
            goto fail;
        }

        if (read_sync(ioc, &info->size, sizeof(info->size)) !=
            sizeof(info->size)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        be64_to_cpus(&info->size);

        if (read_sync(ioc, &oldflags, sizeof(oldflags)) != sizeof(oldflags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        be32_to_cpus(&oldflags);
        if (oldflags & ~0xffff) {
            error_setg(errp, "Unexpected export flags %0x" PRIx32, oldflags);
            goto fail;
        }
        info->flags = oldflags;
    } else {
        error_setg(errp, "Bad magic received");
        goto fail;
    }

    TRACE("Size is %" PRIu64 ", export flags %" PRIx16,
          info->size, info->flags);
    if (zeroes && drop_sync(ioc, 124) != 124) {
        error_setg(errp, "Failed to read reserved block");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, QIOChannelSocket *sioc, NBDExportInfo *info)
{
    unsigned long sectors = info->size / BDRV_SECTOR_SIZE;
    if (info->size / BDRV_SECTOR_SIZE != sectors) {
        LOG("Export size %" PRId64 " too large for 32-bit kernel", info->size);
        return -E2BIG;
    }

    TRACE("Setting NBD socket");

    if (ioctl(fd, NBD_SET_SOCK, (unsigned long) sioc->fd) < 0) {
        int serrno = errno;
        LOG("Failed to set NBD socket");
        return -serrno;
    }

    TRACE("Setting block size to %lu", (unsigned long)BDRV_SECTOR_SIZE);

    if (ioctl(fd, NBD_SET_BLKSIZE, (unsigned long)BDRV_SECTOR_SIZE) < 0) {
        int serrno = errno;
        LOG("Failed setting NBD block size");
        return -serrno;
    }

    TRACE("Setting size to %lu block(s)", sectors);
    if (info->size % BDRV_SECTOR_SIZE) {
        TRACE("Ignoring trailing %d bytes of export",
              (int) (info->size % BDRV_SECTOR_SIZE));
    }

    if (ioctl(fd, NBD_SET_SIZE_BLOCKS, sectors) < 0) {
        int serrno = errno;
        LOG("Failed setting size (in blocks)");
        return -serrno;
    }

    if (ioctl(fd, NBD_SET_FLAGS, (unsigned long) info->flags) < 0) {
        if (errno == ENOTTY) {
            int read_only = (info->flags & NBD_FLAG_READ_ONLY) != 0;
            TRACE("Setting readonly attribute");

            if (ioctl(fd, BLKROSET, (unsigned long) &read_only) < 0) {
                int serrno = errno;
                LOG("Failed setting read-only attribute");
                return -serrno;
            }
        } else {
            int serrno = errno;
            LOG("Failed setting flags");
            return -serrno;
        }
    }

    TRACE("Negotiation ended");

    return 0;
}

int nbd_client(int fd)
{
    int ret;
    int serrno;

    TRACE("Doing NBD loop");

    ret = ioctl(fd, NBD_DO_IT);
    if (ret < 0 && errno == EPIPE) {
        /* NBD_DO_IT normally returns EPIPE when someone has disconnected
         * the socket via NBD_DISCONNECT.  We do not want to return 1 in
         * that case.
         */
        ret = 0;
    }
    serrno = errno;

    TRACE("NBD loop returned %d: %s", ret, strerror(serrno));

    TRACE("Clearing NBD queue");
    ioctl(fd, NBD_CLEAR_QUE);

    TRACE("Clearing NBD socket");
    ioctl(fd, NBD_CLEAR_SOCK);

    errno = serrno;
    return ret;
}

int nbd_disconnect(int fd)
{
    ioctl(fd, NBD_CLEAR_QUE);
    ioctl(fd, NBD_DISCONNECT);
    ioctl(fd, NBD_CLEAR_SOCK);
    return 0;
}

#else
int nbd_init(int fd, QIOChannelSocket *ioc, NBDExportInfo *info)
{
    return -ENOTSUP;
}

int nbd_client(int fd)
{
    return -ENOTSUP;
}
int nbd_disconnect(int fd)
{
    return -ENOTSUP;
}
#endif

ssize_t nbd_send_request(QIOChannel *ioc, NBDRequest *request)
{
    uint8_t buf[NBD_REQUEST_SIZE];
    ssize_t ret;

    TRACE("Sending request to server: "
          "{ .from = %" PRIu64", .len = %" PRIu32 ", .handle = %" PRIu64
          ", .flags = %" PRIx16 ", .type = %" PRIu16 " }",
          request->from, request->len, request->handle,
          request->flags, request->type);

    stl_be_p(buf, NBD_REQUEST_MAGIC);
    stw_be_p(buf + 4, request->flags);
    stw_be_p(buf + 6, request->type);
    stq_be_p(buf + 8, request->handle);
    stq_be_p(buf + 16, request->from);
    stl_be_p(buf + 24, request->len);

    ret = write_sync(ioc, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("writing to socket failed");
        return -EINVAL;
    }
    return 0;
}

ssize_t nbd_receive_reply(QIOChannel *ioc, NBDReply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE];
    uint32_t magic;
    ssize_t ret;

    ret = read_sync(ioc, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    if (ret != sizeof(buf)) {
        LOG("read failed");
        return -EINVAL;
    }

    /* Reply
       [ 0 ..  3]    magic   (NBD_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */

    magic = ldl_be_p(buf);
    reply->error  = ldl_be_p(buf + 4);
    reply->handle = ldq_be_p(buf + 8);

    reply->error = nbd_errno_to_system_errno(reply->error);

    if (reply->error == ESHUTDOWN) {
        /* This works even on mingw which lacks a native ESHUTDOWN */
        LOG("server shutting down");
        return -EINVAL;
    }
    TRACE("Got reply: { magic = 0x%" PRIx32 ", .error = % " PRId32
          ", handle = %" PRIu64" }",
          magic, reply->error, reply->handle);

    if (magic != NBD_REPLY_MAGIC) {
        LOG("invalid magic (got 0x%" PRIx32 ")", magic);
        return -EINVAL;
    }
    return 0;
}

