/*
 *  Copyright (C) 2016 Red Hat, Inc.
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

/* Discard length bytes from channel.
 * Return 0 on success and -errno on fail.
 */
static int drop_sync(QIOChannel *ioc, size_t size)
{
    ssize_t ret;
    char small[1024];
    char *buffer;

    buffer = sizeof(small) > size ? small : g_malloc(MIN(65536, size));
    while (size > 0) {
        ret = read_sync(ioc, buffer, MIN(65536, size));
        if (ret == 0) {
            ret = -EIO;
        }
        if (ret < 0) {
            break;
        }

        assert(ret <= size);
        size -= ret;
    }

    if (buffer != small) {
        g_free(buffer);
    }

    return ret < 0 ? ret : 0;
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
    TRACE("Sending option request %" PRIu32", len %" PRIu32, opt, len);

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

    TRACE("Received option reply %" PRIx32", type %" PRIx32", len %" PRIu32,
          reply->option, reply->type, reply->length);

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
            error_setg(errp, "server's error message is too long");
            goto cleanup;
        }
        msg = g_malloc(reply->length + 1);
        if (read_sync(ioc, msg, reply->length) != reply->length) {
            error_setg(errp, "failed to read option error message");
            goto cleanup;
        }
        msg[reply->length] = '\0';
    }

    switch (reply->type) {
    case NBD_REP_ERR_UNSUP:
        TRACE("server doesn't understand request %" PRIx32
              ", attempting fallback", reply->option);
        result = 0;
        goto cleanup;

    case NBD_REP_ERR_POLICY:
        error_setg(errp, "Denied by server for option %" PRIx32,
                   reply->option);
        break;

    case NBD_REP_ERR_INVALID:
        error_setg(errp, "Invalid data length for option %" PRIx32,
                   reply->option);
        break;

    case NBD_REP_ERR_PLATFORM:
        error_setg(errp, "Server lacks support for option %" PRIx32,
                   reply->option);
        break;

    case NBD_REP_ERR_TLS_REQD:
        error_setg(errp, "TLS negotiation required before option %" PRIx32,
                   reply->option);
        break;

    case NBD_REP_ERR_SHUTDOWN:
        error_setg(errp, "Server shutting down before option %" PRIx32,
                   reply->option);
        break;

    default:
        error_setg(errp, "Unknown error code when asking for option %" PRIx32,
                   reply->option);
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
        if (drop_sync(ioc, len) < 0) {
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
    if (drop_sync(ioc, len) < 0) {
        error_setg(errp, "failed to read export description");
        nbd_send_opt_abort(ioc);
        return -1;
    }
    if (!strcmp(name, want)) {
        *match = true;
    }
    return 1;
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

static int nbd_receive_simple_option(QIOChannel *ioc, int opt,
                                     bool abort_on_notsup, Error **errp)
{
    nbd_opt_reply reply;

    TRACE("Requesting '%s' option from server", nbd_opt_name(opt));
    if (nbd_send_option_request(ioc, opt, 0, NULL, errp) < 0) {
        return -1;
    }

    TRACE("Getting '%s' option reply from server", nbd_opt_name(opt));
    if (nbd_receive_option_reply(ioc, opt, &reply, errp) < 0) {
        return -1;
    }

    if (reply.type != NBD_REP_ACK) {
        error_setg(errp, "Server rejected request for '%s' option: %" PRIx32,
                   nbd_opt_name(opt), reply.type);
        if (abort_on_notsup) {
            nbd_send_opt_abort(ioc);
        }
        return -1;
    }

    if (reply.length != 0) {
        error_setg(errp, "'%s' option response was not zero %" PRIu32,
                   nbd_opt_name(opt), reply.length);
        if (abort_on_notsup) {
            nbd_send_opt_abort(ioc);
        }
        return -1;
    }

    TRACE("%s 'option' approved", nbd_opt_name(opt));
    return 0;
}

static QIOChannel *nbd_receive_starttls(QIOChannel *ioc,
                                        QCryptoTLSCreds *tlscreds,
                                        const char *hostname, Error **errp)
{
    QIOChannelTLS *tioc;
    struct NBDTLSHandshakeData data = { 0 };

    if (nbd_receive_simple_option(ioc, NBD_OPT_STARTTLS, true, errp) < 0) {
        return NULL;
    }

    TRACE("Setting up TLS");
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

static int nbd_receive_query_meta_context(QIOChannel *ioc, const char *export,
                                          const char *context, bool *ok,
                                          Error **errp)
{
    int ret;
    nbd_opt_reply reply;
    size_t export_len = strlen(export);
    size_t context_len = strlen(context);
    size_t data_len = 4 + export_len + 4 + 4 + context_len;

    char *data = g_malloc(data_len);
    char *p = data;
    int nb_reps = 0;

    *ok = false;
    stl_be_p(p, export_len);
    memcpy(p += 4, export, export_len);
    stl_be_p(p += export_len, 1);
    stl_be_p(p += 4, context_len);
    memcpy(p += 4, context, context_len);

    TRACE("Requesting set_meta_context option from server");
    ret = nbd_send_option_request(ioc, NBD_OPT_SET_META_CONTEXT, data_len, data,
                                errp);
    if (ret < 0) {
        goto out;
    }

    while (true) {
        uint32_t context_id;
        char *context_name;
        size_t len;

        ret = nbd_receive_option_reply(ioc, NBD_OPT_SET_META_CONTEXT, &reply,
                                       errp);
        if (ret < 0) {
            goto out;
        }

        ret = nbd_handle_reply_err(ioc, &reply, errp);
        if (ret <= 0) {
            goto out;
        }

        if (reply.type != NBD_REP_META_CONTEXT) {
            break;
        }

        if (read_sync(ioc, &context_id, sizeof(context_id)) !=
            sizeof(context_id))
        {
            ret = -EIO;
            goto out;
        }

        be32_to_cpus(&context_id);

        len = reply.length - sizeof(context_id);
        context_name = g_malloc(len + 1);
        if (read_sync(ioc, context_name, len) != len) {

            ret = -EIO;
            goto out;
        }
        context_name[len] = '\0';

        TRACE("set meta: %u %s", context_id, context_name);

        nb_reps++;
    }

    *ok = nb_reps == 1 && reply.type == NBD_REP_ACK;

out:
    g_free(data);
    return ret;
}

static int nbd_receive_query_bitmap(QIOChannel *ioc, const char *export,
                                    const char *bitmap, bool *ok, Error **errp)
{
    char *context = g_strdup_printf("%s:%s", NBD_META_NS_BITMAPS, bitmap);
    int ret = nbd_receive_query_meta_context(ioc, export, context, ok, errp);

    g_free(context);

    return ret;
}

int nbd_receive_negotiate(QIOChannel *ioc, const char *name, uint16_t *flags,
                          QCryptoTLSCreds *tlscreds, const char *hostname,
                          QIOChannel **outioc,
                          off_t *size, bool *structured_reply,
                          const char *bitmap_name, bool *bitmap_ok,
                          Error **errp)
{
    char buf[256];
    uint64_t magic, s;
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
            /* Check our desired export is present in the
             * server export list. Since NBD_OPT_EXPORT_NAME
             * cannot return an error message, running this
             * query gives us good error reporting if the
             * server required TLS
             */
            if (nbd_receive_query_exports(ioc, name, errp) < 0) {
                goto fail;
            }

            if (structured_reply != NULL) {
                *structured_reply =
                    nbd_receive_simple_option(ioc, NBD_OPT_STRUCTURED_REPLY,
                                              false, NULL) == 0;
            }

            if (!!structured_reply && *structured_reply && !!bitmap_name) {
                int ret;
                assert(!!bitmap_ok);
                ret = nbd_receive_query_bitmap(ioc, name, bitmap_name,
                                               bitmap_ok, errp) == 0;
                if (ret < 0) {
                    goto fail;
                }
            }
        }
        /* write the export name request */
        if (nbd_send_option_request(ioc, NBD_OPT_EXPORT_NAME, -1, name,
                                    errp) < 0) {
            goto fail;
        }

        /* Read the response */
        if (read_sync(ioc, &s, sizeof(s)) != sizeof(s)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        *size = be64_to_cpu(s);

        if (read_sync(ioc, flags, sizeof(*flags)) != sizeof(*flags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        be16_to_cpus(flags);

        if (!!structured_reply && *structured_reply &&
            !(*flags & NBD_CMD_FLAG_DF))
        {
            error_setg(errp, "Structured reply is negotiated, "
                             "but DF flag is not.");
            goto fail;
        }
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

        if (read_sync(ioc, &s, sizeof(s)) != sizeof(s)) {
            error_setg(errp, "Failed to read export length");
            goto fail;
        }
        *size = be64_to_cpu(s);
        TRACE("Size is %" PRIu64, *size);

        if (read_sync(ioc, &oldflags, sizeof(oldflags)) != sizeof(oldflags)) {
            error_setg(errp, "Failed to read export flags");
            goto fail;
        }
        be32_to_cpus(&oldflags);
        if (oldflags & ~0xffff) {
            error_setg(errp, "Unexpected export flags %0x" PRIx32, oldflags);
            goto fail;
        }
        *flags = oldflags;
    } else {
        error_setg(errp, "Bad magic received");
        goto fail;
    }

    TRACE("Size is %" PRIu64 ", export flags %" PRIx16, *size, *flags);
    if (zeroes && drop_sync(ioc, 124) < 0) {
        error_setg(errp, "Failed to read reserved block");
        goto fail;
    }
    rc = 0;

fail:
    return rc;
}

#ifdef __linux__
int nbd_init(int fd, QIOChannelSocket *sioc, uint16_t flags, off_t size)
{
    unsigned long sectors = size / BDRV_SECTOR_SIZE;
    if (size / BDRV_SECTOR_SIZE != sectors) {
        LOG("Export size %lld too large for 32-bit kernel", (long long) size);
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
    if (size % BDRV_SECTOR_SIZE) {
        TRACE("Ignoring trailing %d bytes of export",
              (int) (size % BDRV_SECTOR_SIZE));
    }

    if (ioctl(fd, NBD_SET_SIZE_BLOCKS, sectors) < 0) {
        int serrno = errno;
        LOG("Failed setting size (in blocks)");
        return -serrno;
    }

    if (ioctl(fd, NBD_SET_FLAGS, (unsigned long) flags) < 0) {
        if (errno == ENOTTY) {
            int read_only = (flags & NBD_FLAG_READ_ONLY) != 0;
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
int nbd_init(int fd, QIOChannelSocket *ioc, uint16_t flags, off_t size)
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

static inline int read_sync_check(QIOChannel *ioc, void *buffer, size_t size)
{
    ssize_t ret;

    ret = read_sync(ioc, buffer, size);
    if (ret < 0) {
        return ret;
    }
    if (ret != size) {
        LOG("read failed");
        return -EIO;
    }

    return 0;
}

/* nbd_receive_simple_reply
 * Read simple reply except magic field (which should be already read)
 */
static int nbd_receive_simple_reply(QIOChannel *ioc, NBDReply *reply)
{
    uint8_t buf[NBD_REPLY_SIZE - 4];
    ssize_t ret;

    ret = read_sync_check(ioc, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    /* Reply
       [ 0 ..  3]    magic   (NBD_SIMPLE_REPLY_MAGIC)
       [ 4 ..  7]    error   (0 == no error)
       [ 7 .. 15]    handle
     */

    reply->error  = ldl_be_p(buf);
    reply->handle = ldq_be_p(buf + 4);

    return 0;
}

/* nbd_receive_structured_reply_chunk
 * Read structured reply chunk except magic field (which should be already read)
 * Data for NBD_REPLY_TYPE_OFFSET_DATA is not read too.
 * Length field of reply out parameter corresponds to unread part of reply.
 */
static int nbd_receive_structured_reply_chunk(QIOChannel *ioc, NBDReply *reply)
{
    NBDStructuredReplyChunk chunk;
    ssize_t ret;
    uint16_t message_size;

    ret = read_sync_check(ioc, (uint8_t *)&chunk + sizeof(chunk.magic),
                          sizeof(chunk) - sizeof(chunk.magic));
    if (ret < 0) {
        return ret;
    }

    reply->flags = be16_to_cpu(chunk.flags);
    reply->type = be16_to_cpu(chunk.type);
    reply->handle = be64_to_cpu(chunk.handle);
    reply->length = be32_to_cpu(chunk.length);

    switch (reply->type) {
    case NBD_REPLY_TYPE_NONE:
        break;
    case NBD_REPLY_TYPE_OFFSET_DATA:
    case NBD_REPLY_TYPE_OFFSET_HOLE:
        ret = read_sync_check(ioc, &reply->offset, sizeof(reply->offset));
        if (ret < 0) {
            return ret;
        }
        be64_to_cpus(&reply->offset);
        reply->length -= sizeof(reply->offset);
        break;
    case NBD_REPLY_TYPE_ERROR:
    case NBD_REPLY_TYPE_ERROR_OFFSET:
        ret = read_sync_check(ioc, &reply->error, sizeof(reply->error));
        if (ret < 0) {
            return ret;
        }
        be32_to_cpus(&reply->error);

        ret = read_sync_check(ioc, &message_size, sizeof(message_size));
        if (ret < 0) {
            return ret;
        }
        be16_to_cpus(&message_size);

        if (message_size > 0) {
            /* TODO: provide error message to user */
            ret = drop_sync(ioc, message_size);
            if (ret < 0) {
                return ret;
            }
        }

        if (reply->type == NBD_REPLY_TYPE_ERROR_OFFSET) {
            /* drop 64bit offset */
            ret = drop_sync(ioc, 8);
            if (ret < 0) {
                return ret;
            }
        }
        break;
    default:
        if (reply->type & (1 << 15)) {
            /* unknown error */
            ret = drop_sync(ioc, reply->length);
            if (ret < 0) {
                return ret;
            }

            reply->error = NBD_EINVAL;
            reply->length = 0;
        } else {
            /* unknown non-error reply type */
            return -EINVAL;
        }
    }

    return 0;
}

int nbd_receive_reply(QIOChannel *ioc, NBDReply *reply)
{
    uint32_t magic;
    int ret;

    ret = read_sync_check(ioc, &magic, sizeof(magic));
    if (ret < 0) {
        return ret;
    }

    be32_to_cpus(&magic);

    switch (magic) {
    case NBD_SIMPLE_REPLY_MAGIC:
        reply->simple = true;
        ret = nbd_receive_simple_reply(ioc, reply);
        break;
    case NBD_STRUCTURED_REPLY_MAGIC:
        reply->simple = false;
        ret = nbd_receive_structured_reply_chunk(ioc, reply);
        break;
    default:
        LOG("invalid magic (got 0x%" PRIx32 ")", magic);
        return -EINVAL;
    }

    if (ret < 0) {
        return ret;
    }

    reply->error = nbd_errno_to_system_errno(reply->error);

    if (reply->error == ESHUTDOWN) {
        /* This works even on mingw which lacks a native ESHUTDOWN */
        LOG("server shutting down");
        return -EINVAL;
    }
    TRACE("Got reply: { magic = 0x%" PRIx32 ", .error = % " PRId32
          ", handle = %" PRIu64" }",
          magic, reply->error, reply->handle);

    return 0;
}
