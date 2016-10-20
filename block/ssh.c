/*
 * Secure Shell (ssh) backend for QEMU.
 *
 * Copyright (C) 2013 Red Hat Inc., Richard W.M. Jones <rjones@redhat.com>
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

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/uri.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qstring.h"

/* DEBUG_SSH=1 enables the DPRINTF (debugging printf) statements in
 * this block driver code.
 *
 * TRACE_LIBSSH=<level> enables tracing in libssh itself.
 * The meaning of <level> is described here:
 * http://api.libssh.org/master/group__libssh__log.html
 */
#define DEBUG_SSH     0
#define TRACE_LIBSSH  0 /* see: SSH_LOG_* */

#define DPRINTF(fmt, ...)                           \
    do {                                            \
        if (DEBUG_SSH) {                            \
            fprintf(stderr, "ssh: %-15s " fmt "\n", \
                    __func__, ##__VA_ARGS__);       \
        }                                           \
    } while (0)

typedef struct BDRVSSHState {
    /* Coroutine. */
    CoMutex lock;

    /* SSH connection. */
    ssh_session session;              /* ssh session */
    sftp_session sftp;                /* sftp session */
    sftp_file sftp_handle;            /* sftp remote file handle */

    /* File attributes at open.  We try to keep the .size field
     * updated if it changes (eg by writing at the end of the file).
     */
    sftp_attributes attrs;

    /* Used to warn if 'flush' is not supported. */
    bool unsafe_flush_warning;
} BDRVSSHState;

static void ssh_state_init(BDRVSSHState *s)
{
    memset(s, 0, sizeof *s);
    qemu_co_mutex_init(&s->lock);
}

static void ssh_state_free(BDRVSSHState *s)
{
    if (s->attrs) {
        sftp_attributes_free(s->attrs);
    }
    if (s->sftp_handle) {
        sftp_close(s->sftp_handle);
    }
    if (s->sftp) {
        sftp_free(s->sftp);
    }
    if (s->session) {
        ssh_disconnect(s->session);
        ssh_free(s->session);
    }
}

static void GCC_FMT_ATTR(3, 4)
session_error_setg(Error **errp, BDRVSSHState *s, const char *fs, ...)
{
    va_list args;
    char *msg;

    va_start(args, fs);
    msg = g_strdup_vprintf(fs, args);
    va_end(args);

    if (s->session) {
        const char *ssh_err;
        int ssh_err_code;

        /* This is not an errno.  See <libssh/libssh.h>. */
        ssh_err = ssh_get_error(s->session);
        ssh_err_code = ssh_get_error_code(s->session);
        error_setg(errp, "%s: %s (libssh error code: %d)",
                   msg, ssh_err, ssh_err_code);
    } else {
        error_setg(errp, "%s", msg);
    }
    g_free(msg);
}

static void GCC_FMT_ATTR(3, 4)
sftp_error_setg(Error **errp, BDRVSSHState *s, const char *fs, ...)
{
    va_list args;
    char *msg;

    va_start(args, fs);
    msg = g_strdup_vprintf(fs, args);
    va_end(args);

    if (s->sftp) {
        int sftp_err_code;

        /* This is not an errno.  See <libssh/sftp.h>. */
        sftp_err_code = sftp_get_error(s->sftp);

        error_setg(errp,
                   "%s (sftp error code: %d)",
                   msg, sftp_err_code);
    } else {
        error_setg(errp, "%s", msg);
    }
    g_free(msg);
}

static void GCC_FMT_ATTR(2, 3)
sftp_error_report(BDRVSSHState *s, const char *fs, ...)
{
    va_list args;

    va_start(args, fs);
    error_vprintf(fs, args);

    if ((s)->sftp) {
        int sftp_err_code;

        /* This is not an errno.  See <libssh/sftp.h>. */
        sftp_err_code = sftp_get_error(s->sftp);

        error_printf(": (sftp error code: %d)",
                     sftp_err_code);
    }

    va_end(args);
    error_printf("\n");
}

static int parse_uri(const char *filename, QDict *options, Error **errp)
{
    URI *uri = NULL;
    QueryParams *qp;
    int i;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    if (strcmp(uri->scheme, "ssh") != 0) {
        error_setg(errp, "URI scheme must be 'ssh'");
        goto err;
    }

    if (!uri->server || strcmp(uri->server, "") == 0) {
        error_setg(errp, "missing hostname in URI");
        goto err;
    }

    if (!uri->path || strcmp(uri->path, "") == 0) {
        error_setg(errp, "missing remote path in URI");
        goto err;
    }

    qp = query_params_parse(uri->query);
    if (!qp) {
        error_setg(errp, "could not parse query parameters");
        goto err;
    }

    if(uri->user && strcmp(uri->user, "") != 0) {
        qdict_put(options, "user", qstring_from_str(uri->user));
    }

    qdict_put(options, "host", qstring_from_str(uri->server));

    if (uri->port) {
        qdict_put(options, "port", qint_from_int(uri->port));
    }

    qdict_put(options, "path", qstring_from_str(uri->path));

    /* Pick out any query parameters that we understand, and ignore
     * the rest.
     */
    for (i = 0; i < qp->n; ++i) {
        if (strcmp(qp->p[i].name, "host_key_check") == 0) {
            qdict_put(options, "host_key_check",
                      qstring_from_str(qp->p[i].value));
        }
    }

    query_params_free(qp);
    uri_free(uri);
    return 0;

 err:
    if (uri) {
      uri_free(uri);
    }
    return -EINVAL;
}

static void ssh_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    if (qdict_haskey(options, "user") ||
        qdict_haskey(options, "host") ||
        qdict_haskey(options, "port") ||
        qdict_haskey(options, "path") ||
        qdict_haskey(options, "host_key_check")) {
        error_setg(errp, "user, host, port, path, host_key_check cannot be used at the same time as a file option");
        return;
    }

    parse_uri(filename, options, errp);
}

static int check_host_key_knownhosts(BDRVSSHState *s,
                                     const char *host, int port, Error **errp)
{
    int ret;
    int state;

    state = ssh_is_server_known(s->session);

    switch (state) {
    case SSH_SERVER_KNOWN_OK:
        /* OK */
        break;
    case SSH_SERVER_KNOWN_CHANGED:
        ret = -EINVAL;
        session_error_setg(errp, s,
                      "host key does not match the one in known_hosts");
        goto out;
    case SSH_SERVER_FOUND_OTHER:
        ret = -EINVAL;
        session_error_setg(errp, s,
                      "host key for this server not found, another type "
                      "exists");
        goto out;
    case SSH_SERVER_FILE_NOT_FOUND:
        ret = -EINVAL;
        session_error_setg(errp, s, "known_hosts file not found");
        goto out;
    case SSH_SERVER_NOT_KNOWN:
        ret = -EINVAL;
        session_error_setg(errp, s, "no host key was found in known_hosts");
        goto out;
    case SSH_SERVER_ERROR:
        ret = -EINVAL;
        session_error_setg(errp, s, "server error");
        goto out;
    default:
        ret = -EINVAL;
        session_error_setg(errp, s, "error while checking for known server");
        goto out;
    }

    /* known_hosts checking successful. */
    ret = 0;

 out:
    return ret;
}

static unsigned hex2decimal(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }

    return -1;
}

/* Compare the binary fingerprint (hash of host key) with the
 * host_key_check parameter.
 */
static int compare_fingerprint(const unsigned char *fingerprint, size_t len,
                               const char *host_key_check)
{
    unsigned c;

    while (len > 0) {
        while (*host_key_check == ':')
            host_key_check++;
        if (!qemu_isxdigit(host_key_check[0]) ||
            !qemu_isxdigit(host_key_check[1]))
            return 1;
        c = hex2decimal(host_key_check[0]) * 16 +
            hex2decimal(host_key_check[1]);
        if (c - *fingerprint != 0)
            return c - *fingerprint;
        fingerprint++;
        len--;
        host_key_check += 2;
    }
    return *host_key_check - '\0';
}

static int
check_host_key_hash(BDRVSSHState *s, const char *hash,
                    enum ssh_publickey_hash_type type, size_t fingerprint_len,
                    Error **errp)
{
    int r;
    ssh_key pubkey;
    unsigned char *server_hash;
    size_t server_hash_len;

    r = ssh_get_publickey(s->session, &pubkey);
    if (r < 0) {
        session_error_setg(errp, s, "failed to read remote host key");
        return -EINVAL;
    }

    r = ssh_get_publickey_hash(pubkey, type, &server_hash, &server_hash_len);
    ssh_key_free(pubkey);
    if (r < 0) {
        session_error_setg(errp, s,
                           "failed reading the hash of the server SSH key");
        return -EINVAL;
    }

    if (compare_fingerprint(server_hash, server_hash_len, hash) != 0) {
        ssh_clean_pubkey_hash(&server_hash);
        error_setg(errp, "remote host key does not match host_key_check '%s'",
                   hash);
        return -EPERM;
    }

    ssh_clean_pubkey_hash(&server_hash);

    return 0;
}

static int check_host_key(BDRVSSHState *s, const char *host, int port,
                          const char *host_key_check, Error **errp)
{
    /* host_key_check=no */
    if (strcmp(host_key_check, "no") == 0) {
        return 0;
    }

    /* host_key_check=md5:xx:yy:zz:... */
    if (strncmp(host_key_check, "md5:", 4) == 0) {
        return check_host_key_hash(s, &host_key_check[4],
                                   SSH_PUBLICKEY_HASH_MD5, 16, errp);
    }

    /* host_key_check=sha1:xx:yy:zz:... */
    if (strncmp(host_key_check, "sha1:", 5) == 0) {
        return check_host_key_hash(s, &host_key_check[5],
                                   SSH_PUBLICKEY_HASH_SHA1, 20, errp);
    }

    /* host_key_check=yes */
    if (strcmp(host_key_check, "yes") == 0) {
        return check_host_key_knownhosts(s, host, port, errp);
    }

    error_setg(errp, "unknown host_key_check setting (%s)", host_key_check);
    return -EINVAL;
}

static int authenticate(BDRVSSHState *s, const char *user, Error **errp)
{
    int r, ret;
    int method;

    r = ssh_userauth_none(s->session, NULL);
    if (r == SSH_AUTH_ERROR) {
        ret = -EPERM;
        session_error_setg(errp, s, "failed to call ssh_userauth_none");
        goto out;
    }

    method = ssh_userauth_list(s->session, NULL);

    /* Try to authenticate with publickey, using the ssh-agent
     * if available.
     */
    if (method & SSH_AUTH_METHOD_PUBLICKEY) {
        r = ssh_userauth_publickey_auto(s->session, NULL, NULL);
        if (r == SSH_AUTH_ERROR) {
            ret = -EINVAL;
            error_setg(errp, "failed to authenticate using publickey "
                             "authentication");
            goto out;
        } else if (r == SSH_AUTH_SUCCESS) {
            /* Authenticated! */
            ret = 0;
            goto out;
        }
    }

    ret = -EPERM;
    error_setg(errp, "failed to authenticate using publickey authentication "
               "and the identities held by your ssh-agent");

 out:
    return ret;
}

static QemuOptsList ssh_runtime_opts = {
    .name = "ssh",
    .head = QTAILQ_HEAD_INITIALIZER(ssh_runtime_opts.head),
    .desc = {
        {
            .name = "host",
            .type = QEMU_OPT_STRING,
            .help = "Host to connect to",
        },
        {
            .name = "port",
            .type = QEMU_OPT_NUMBER,
            .help = "Port to connect to",
        },
        {
            .name = "path",
            .type = QEMU_OPT_STRING,
            .help = "Path of the image on the host",
        },
        {
            .name = "user",
            .type = QEMU_OPT_STRING,
            .help = "User as which to connect",
        },
        {
            .name = "host_key_check",
            .type = QEMU_OPT_STRING,
            .help = "Defines how and what to check the host key against",
        },
    },
};

static int connect_to_ssh(BDRVSSHState *s, QDict *options,
                          int ssh_flags, int creat_mode, Error **errp)
{
    int r, ret;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    const char *host, *user, *path, *host_key_check;
    unsigned int port;

    opts = qemu_opts_create(&ssh_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto err;
    }

    host = qemu_opt_get(opts, "host");
    if (!host) {
        ret = -EINVAL;
        error_setg(errp, "No hostname was specified");
        goto err;
    }

    port = qemu_opt_get_number(opts, "port", 22);

    path = qemu_opt_get(opts, "path");
    if (!path) {
        ret = -EINVAL;
        error_setg(errp, "No path was specified");
        goto err;
    }

    user = qemu_opt_get(opts, "user");
    if (!user) {
        user = g_get_user_name();
        if (!user) {
            error_setg_errno(errp, errno, "Can't get user name");
            ret = -errno;
            goto err;
        }
    }

    host_key_check = qemu_opt_get(opts, "host_key_check");
    if (!host_key_check) {
        host_key_check = "yes";
    }

    /* Create SSH session. */
    s->session = ssh_new();
    if (!s->session) {
        goto err;
    }

    /* Make sure we are in blocking mode during the connection and
     * authentication phases.
     */
    ssh_set_blocking(s->session, 1);

    r = ssh_options_set(s->session, SSH_OPTIONS_USER, user);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to set the user in the libssh session");
        goto err;
    }

    r = ssh_options_set(s->session, SSH_OPTIONS_HOST, host);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to set the host in the libssh session");
        goto err;
    }

    if (port > 0) {
        r = ssh_options_set(s->session, SSH_OPTIONS_PORT, &port);
        if (r < 0) {
            ret = -EINVAL;
            session_error_setg(errp, s,
                               "failed to set the port in the libssh session");
            goto err;
        }
    }

    /* Read ~/.ssh/config. */
    r = ssh_options_parse_config(s->session, NULL);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to parse ~/.ssh/config");
        goto err;
    }

    /* Connect. */
    r = ssh_connect(s->session);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to establish SSH session");
        goto err;
    }

    /* Check the remote host's key against known_hosts. */
    ret = check_host_key(s, host, port, host_key_check, errp);
    if (ret < 0) {
        goto err;
    }

    /* Authenticate. */
    ret = authenticate(s, user, errp);
    if (ret < 0) {
        goto err;
    }

    /* Start SFTP. */
    s->sftp = sftp_new(s->session);
    if (!s->sftp) {
        session_error_setg(errp, s, "failed to create sftp handle");
        ret = -EINVAL;
        goto err;
    }

    r = sftp_init(s->sftp);
    if (r < 0) {
        session_error_setg(errp, s, "failed to initialize sftp handle");
        ret = -EINVAL;
        goto err;
    }

    /* Open the remote file. */
    DPRINTF("opening file %s flags=0x%x creat_mode=0%o",
            path, ssh_flags, creat_mode);
    s->sftp_handle = sftp_open(s->sftp, path, ssh_flags, creat_mode);
    if (!s->sftp_handle) {
        session_error_setg(errp, s, "failed to open remote file '%s'", path);
        ret = -EINVAL;
        goto err;
    }

    /* Make sure the SFTP file is handled in blocking mode. */
    sftp_file_set_blocking(s->sftp_handle);

    qemu_opts_del(opts);

    s->attrs = sftp_fstat(s->sftp_handle);
    if (!s->attrs) {
        sftp_error_setg(errp, s, "failed to read file attributes");
        return -EINVAL;
    }

    return 0;

 err:
    if (s->attrs) {
        sftp_attributes_free(s->attrs);
    }
    s->attrs = NULL;
    if (s->sftp_handle) {
        sftp_close(s->sftp_handle);
    }
    s->sftp_handle = NULL;
    if (s->sftp) {
        sftp_free(s->sftp);
    }
    s->sftp = NULL;
    if (s->session) {
        ssh_disconnect(s->session);
        ssh_free(s->session);
    }
    s->session = NULL;

    qemu_opts_del(opts);

    return ret;
}

static int ssh_file_open(BlockDriverState *bs, QDict *options, int bdrv_flags,
                         Error **errp)
{
    BDRVSSHState *s = bs->opaque;
    int ret;
    int ssh_flags;

    ssh_state_init(s);

    ssh_flags = 0;
    if (bdrv_flags & BDRV_O_RDWR) {
        ssh_flags |= O_RDWR;
    } else {
        ssh_flags |= O_RDONLY;
    }

    /* Start up SSH. */
    ret = connect_to_ssh(s, options, ssh_flags, 0, errp);
    if (ret < 0) {
        goto err;
    }

    /* Go non-blocking. */
    ssh_set_blocking(s->session, 0);

    return 0;

 err:

    return ret;
}

static QemuOptsList ssh_create_opts = {
    .name = "ssh-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(ssh_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static int ssh_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int r, ret;
    int64_t total_size = 0;
    QDict *uri_options = NULL;
    BDRVSSHState s;
    ssize_t r2;
    char c[1] = { '\0' };

    ssh_state_init(&s);

    /* Get desired file size. */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);
    DPRINTF("total_size=%" PRIi64, total_size);

    uri_options = qdict_new();
    r = parse_uri(filename, uri_options, errp);
    if (r < 0) {
        ret = r;
        goto out;
    }

    r = connect_to_ssh(&s, uri_options,
                       O_RDWR | O_CREAT | O_TRUNC,
                       0644, errp);
    if (r < 0) {
        ret = r;
        goto out;
    }

    if (total_size > 0) {
        sftp_seek64(s.sftp_handle, total_size - 1);
        r2 = sftp_write(s.sftp_handle, c, 1);
        if (r2 < 0) {
            sftp_error_setg(errp, &s, "truncate failed");
            ret = -EINVAL;
            goto out;
        }
        s.attrs->size = total_size;
    }

    ret = 0;

 out:
    ssh_state_free(&s);
    if (uri_options != NULL) {
        QDECREF(uri_options);
    }
    return ret;
}

static void ssh_close(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;

    ssh_state_free(s);
}

static int ssh_has_zero_init(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    /* Assume false, unless we can positively prove it's true. */
    int has_zero_init = 0;

    if (s->attrs->type == SSH_FILEXFER_TYPE_REGULAR) {
        has_zero_init = 1;
    }

    return has_zero_init;
}

/* A non-blocking call returned EAGAIN, so yield, ensuring the
 * handlers are set up so that we'll be rescheduled when there is an
 * interesting event on the socket.
 */
static coroutine_fn void co_yield(BDRVSSHState *s, BlockDriverState *bs)
{
    qemu_coroutine_yield();
}

static coroutine_fn int ssh_read(BDRVSSHState *s, BlockDriverState *bs,
                                 int64_t offset, size_t size,
                                 QEMUIOVector *qiov)
{
    ssize_t r;
    size_t got;
    char *buf, *end_of_vec;
    struct iovec *i;

    DPRINTF("offset=%" PRIi64 " size=%zu", offset, size);

    sftp_seek64(s->sftp_handle, offset);

    /* This keeps track of the current iovec element ('i'), where we
     * will write to next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    for (got = 0; got < size; ) {
    again:
        DPRINTF("sftp_read buf=%p size=%zu (actual size=%zu)",
                buf, end_of_vec - buf, MIN(end_of_vec - buf, 16384));
        /* The size of SFTP packets is limited to 32K bytes, so limit
         * the amount of data requested to 16K, as libssh currently
         * does not handle multiple requests on its own:
         * https://red.libssh.org/issues/58
         */
        r = sftp_read(s->sftp_handle, buf, MIN(end_of_vec - buf, 16384));
        DPRINTF("sftp_read returned %zd", r);

        if (r == SSH_AGAIN) {
            co_yield(s, bs);
            goto again;
        }
        if (r == SSH_EOF) {
            /* EOF: Short read so pad the buffer with zeroes and return it. */
            qemu_iovec_memset(qiov, got, 0, size - got);
            return 0;
        }
        if (r < 0) {
            sftp_error_report(s, "read failed");
            return -EIO;
        }

        got += r;
        buf += r;
        if (buf >= end_of_vec && got < size) {
            i++;
            buf = i->iov_base;
            end_of_vec = i->iov_base + i->iov_len;
        }
    }

    return 0;
}

static coroutine_fn int ssh_co_readv(BlockDriverState *bs,
                                     int64_t sector_num,
                                     int nb_sectors, QEMUIOVector *qiov)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_read(s, bs, sector_num * BDRV_SECTOR_SIZE,
                   nb_sectors * BDRV_SECTOR_SIZE, qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static int ssh_write(BDRVSSHState *s, BlockDriverState *bs,
                     int64_t offset, size_t size,
                     QEMUIOVector *qiov)
{
    ssize_t r;
    size_t written;
    char *buf, *end_of_vec;
    struct iovec *i;

    DPRINTF("offset=%" PRIi64 " size=%zu", offset, size);

    sftp_seek64(s->sftp_handle, offset);

    /* This keeps track of the current iovec element ('i'), where we
     * will read from next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    for (written = 0; written < size; ) {
    again:
        DPRINTF("sftp_write buf=%p size=%zu", buf, end_of_vec - buf);
        r = sftp_write(s->sftp_handle, buf, end_of_vec - buf);
        DPRINTF("sftp_write returned %zd", r);

        if (r == SSH_AGAIN) {
            co_yield(s, bs);
            goto again;
        }
        if (r < 0) {
            sftp_error_report(s, "write failed");
            return -EIO;
        }

        written += r;
        buf += r;
        if (buf >= end_of_vec && written < size) {
            i++;
            buf = i->iov_base;
            end_of_vec = i->iov_base + i->iov_len;
        }

        if (offset + written > s->attrs->size) {
            s->attrs->size = offset + written;
        }
    }

    return 0;
}

static coroutine_fn int ssh_co_writev(BlockDriverState *bs,
                                      int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *qiov)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_write(s, bs, sector_num * BDRV_SECTOR_SIZE,
                    nb_sectors * BDRV_SECTOR_SIZE, qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static void unsafe_flush_warning(BDRVSSHState *s, const char *what)
{
    if (!s->unsafe_flush_warning) {
        char *host;
        unsigned int port;

        ssh_options_get(s->session, SSH_OPTIONS_HOST, &host);
        ssh_options_get_port(s->session, &port);

        error_report("warning: ssh server %s:%u does not support fsync",
                     host, port);
        if (what) {
            error_report("to support fsync, you need %s", what);
        }
        ssh_string_free_char(host);
        s->unsafe_flush_warning = true;
    }
}

#ifdef HAS_LIBSSH_SFTP_FSYNC

static coroutine_fn int ssh_flush(BDRVSSHState *s, BlockDriverState *bs)
{
    int r;

    DPRINTF("fsync");

    if (!sftp_extension_supported(s->sftp, "fsync@openssh.com", "1")) {
        unsafe_flush_warning(s, "OpenSSH >= 6.3");
        return 0;
    }
 again:
    r = sftp_fsync(s->sftp_handle);
    if (r == SSH_AGAIN) {
        co_yield(s, bs);
        goto again;
    }
    if (r < 0) {
        sftp_error_report(s, "fsync failed");
        return -EIO;
    }

    return 0;
}

static coroutine_fn int ssh_co_flush(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_flush(s, bs);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

#else /* !HAS_LIBSSH_SFTP_FSYNC */

static coroutine_fn int ssh_co_flush(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;

    unsafe_flush_warning(s, "libssh >= 0.8.0");
    return 0;
}

#endif /* !HAS_LIBSSH_SFTP_FSYNC */

static int64_t ssh_getlength(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    int64_t length;

    /* Note we cannot make a libssh call here. */
    length = (int64_t) s->attrs->size;
    DPRINTF("length=%" PRIi64, length);

    return length;
}

static BlockDriver bdrv_ssh = {
    .format_name                  = "ssh",
    .protocol_name                = "ssh",
    .instance_size                = sizeof(BDRVSSHState),
    .bdrv_parse_filename          = ssh_parse_filename,
    .bdrv_file_open               = ssh_file_open,
    .bdrv_create                  = ssh_create,
    .bdrv_close                   = ssh_close,
    .bdrv_has_zero_init           = ssh_has_zero_init,
    .bdrv_co_readv                = ssh_co_readv,
    .bdrv_co_writev               = ssh_co_writev,
    .bdrv_getlength               = ssh_getlength,
    .bdrv_co_flush_to_disk        = ssh_co_flush,
    .create_opts                  = &ssh_create_opts,
};

static void bdrv_ssh_init(void)
{
    int r;

    r = ssh_init();
    if (r != 0) {
        fprintf(stderr, "libssh initialization failed, %d\n", r);
        exit(EXIT_FAILURE);
    }

#if TRACE_LIBSSH != 0
    ssh_set_log_level(TRACE_LIBSSH);
#endif

    bdrv_register(&bdrv_ssh);
}

block_init(bdrv_ssh_init);
