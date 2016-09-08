/*
 * cryptodev-linux backend support
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include <sys/ioctl.h>
#include <fcntl.h>
#include <crypto/cryptodev.h>

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"
#include "qemu/error-report.h"

#include "crypto/crypto.h"
#include "crypto/crypto-clients.h"
#include "standard-headers/linux/virtio_crypto.h"


#define CRYPTO_CHARDEV_PATH "/dev/crypto"


typedef struct CryptodevLinuxSession {
    struct session_op *sess;
    uint8_t direction; /* encryption or decryption */
    uint8_t type; /* cipher? hash? aead? */
    QTAILQ_ENTRY(CryptodevLinuxSession) next;
} CryptodevLinuxSession;

typedef struct CryptodevLinuxState {
    CryptoClientState cc;
    int fd;
    bool read_poll;
    bool write_poll;
    bool enabled;
    QTAILQ_HEAD(, CryptodevLinuxSession) sessions;
} CryptodevLinuxState;

static int
cryptodev_linux_open(int *gfd, Error **errp)
{
    int fd = -1;

    /* Open the crypto device */
    fd = open(CRYPTO_CHARDEV_PATH, O_RDWR, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Cannot open %s",
                         CRYPTO_CHARDEV_PATH);
        return -1;
    }

    /* Set close-on-exec (not really neede here) */
    if (fcntl(fd, F_SETFD, 1) == -1) {
        perror("fcntl(F_SETFD)");
        error_setg_errno(errp, errno,
                        "Failed to fcntl(F_SETFD) %s",
                         CRYPTO_CHARDEV_PATH);
        close(fd);
        return -1;
    }

    *gfd = fd;

    return 0;
}

static int
cryptodev_linux_handle_cipher_sess(CryptodevLinuxState *s,
                              CryptoSymSessionInfo *session_info,
                              struct session_op *sess,
                              uint8_t *direction)
{
    switch (session_info->cipher_alg) {
    case VIRTIO_CRYPTO_CIPHER_AES_CBC:
        sess->cipher = CRYPTO_AES_CBC;
        break;
    default:
        error_report("Unsupported cipher alg: %u",
                     session_info->cipher_alg);
        return -1;
    }
    /* Get crypto session for assinged algorithm */
    sess->keylen = session_info->key_len;
    sess->key = session_info->cipher_key;
    *direction = session_info->direction;
    return 0;
}

static int
cryptodev_linux_handle_hash_sess(CryptodevLinuxState *s,
                              CryptoSymSessionInfo *session_info,
                              struct session_op *sess)
{
    switch (session_info->hash_alg) {
    case VIRTIO_CRYPTO_HASH_SHA1:
        sess->mac = CRYPTO_SHA1_HMAC;
        break;
    default:
        error_report("Unsupported hash alg: %u",
                     session_info->hash_alg);
        return -1;
    }

    sess->mackeylen = session_info->auth_key_len;
    sess->mackey = session_info->auth_key;
    return 0;
}

static int
cryptodev_linux_handle_chaining_sess(CryptodevLinuxState *s,
                              CryptoSymSessionInfo *session_info,
                              struct session_op *sess,
                              uint8_t *direction)
{
    int ret;

    ret = cryptodev_linux_handle_cipher_sess(s, session_info,
                                             sess, direction);
    if (ret == 0) {
        ret = cryptodev_linux_handle_hash_sess(s,
                                         session_info, sess);
    }
    return ret;
}

static int
cryptodev_linux_create_session(CryptoClientState *cc,
                               CryptoSymSessionInfo *session_info,
                               uint64_t *session_id)
{
    CryptodevLinuxState *s = DO_UPCAST(CryptodevLinuxState, cc, cc);
    int fd = s->fd;
    int ret = -1;
    CryptodevLinuxSession *session = NULL;
    uint8_t direction = 0;
    struct session_op *sess;
#ifdef CIOCGSESSINFO
    struct session_info_op siop;
#endif

    sess = g_new0(struct session_op, 1);
    /* Setup session Parameters */
    switch (session_info->op_type) {
    case VIRTIO_CRYPTO_SYM_OP_CIPHER:
        ret = cryptodev_linux_handle_cipher_sess(s, session_info,
                                               sess, &direction);
        if (ret < 0) {
            goto err;
        }
        break;
    case VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING:
        ret = cryptodev_linux_handle_chaining_sess(s, session_info,
                                          sess, &direction);
        if (ret < 0) {
            goto err;
        }
        break;
    default:
        error_report("Unsupported type: %u", session_info->op_type);
        goto err;
    }

    if (ioctl(fd, CIOCGSESSION, sess)) {
        perror("ioctl(CIOCGSESSION)");
        ret = -1;
        goto err;
    }

#ifdef CIOCGSESSINFO
    siop.ses = sess->ses;
    if (ioctl(fd, CIOCGSESSINFO, &siop)) {
        perror("ioctl(CIOCGSESSINFO)");
        ret = -1;
        goto err;
    }
    printf("got %s with driver %s\n",
          siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);
#endif

    *session_id = sess->ses;

    session = g_new0(CryptodevLinuxSession, 1);
    session->sess = sess;
    session->type = session_info->op_type;
    switch (direction) {
    case VIRTIO_CRYPTO_OP_ENCRYPT:
        session->direction = COP_ENCRYPT;
        break;
    case VIRTIO_CRYPTO_OP_DECRYPT:
        session->direction = COP_DECRYPT;
        break;
    default:
        error_report("Unsupported direction: %u", direction);
        goto err;
    }

    QTAILQ_INSERT_TAIL(&s->sessions, session, next);

    return 0;
err:
    g_free(sess);
    g_free(session);
    return ret;
}

static CryptodevLinuxSession *
cryptodev_linux_find_session(CryptodevLinuxState *s,
                             uint64_t session_id)
{
    CryptodevLinuxSession *session, *tmp;

    QTAILQ_FOREACH_SAFE(session, &s->sessions, next, tmp) {
        if (session->sess->ses == session_id) {
            return session;
        }
    }

    return NULL;
}

static int
cryptodev_linux_close_session(CryptoClientState *cc,
                               uint64_t session_id)
{
    CryptodevLinuxState *s = DO_UPCAST(CryptodevLinuxState, cc, cc);
    int fd = s->fd;
    CryptodevLinuxSession *session;

    session = cryptodev_linux_find_session(s, session_id);
    if (session == NULL) {
        error_report("Cannot find the session: %" PRIu64 "",
                      session_id);
        return -1;
    }

    if (ioctl(fd, CIOCFSESSION, &session_id)) {
        perror("ioctl(CIOCFSESSION)");
        return -1;
    }

    QTAILQ_REMOVE(&s->sessions, session, next);
    g_free(session->sess);
    g_free(session);
    return 0;
}

static int
cryptodev_linux_handle_cipher_op(CryptoSymOpInfo *op_info,
                                   CryptodevLinuxSession *session,
                                   int fd)
{
    struct crypt_op cryp;

    cryp.ses = op_info->session_id;
    cryp.len = op_info->src_len;
    cryp.src = op_info->src;
    cryp.dst = op_info->dst;
    cryp.iv = op_info->iv;
    cryp.op = session->direction;

    if (ioctl(fd, CIOCCRYPT, &cryp)) {
        perror("ioctl(CIOCCRYPT)");
        return -1;
    }

    return 1;
}

static int
cryptodev_linux_handle_chaining_op(CryptoSymOpInfo *op_info,
                                   CryptodevLinuxSession *session,
                                   int fd)
{
    struct crypt_auth_op cao;

    cao.ses = op_info->session_id;
    cao.len = op_info->src_len;
    cao.src = op_info->src;
    cao.dst = op_info->dst;
    cao.iv = op_info->iv;
    cao.op = session->direction;

    if (op_info->aad_len > 0) {
        cao.auth_len = op_info->aad_len;
        cao.auth_src = op_info->aad_data;
    }

    /* We only support TLS mode at present, the hash result is
       stored at the end of cipher text, the frontend driver
       should allocate enough memory. */
    cao.flags = COP_FLAG_AEAD_TLS_TYPE;

    if (ioctl(fd, CIOCAUTHCRYPT, &cao)) {
        perror("ioctl(CIOCCRYPT)");
        return -1;
    }

    return 1;
}


static int
cryptodev_linux_do_sym_op(CryptoClientState *cc,
                              CryptoSymOpInfo *op_info)
{
    CryptodevLinuxState *s = DO_UPCAST(CryptodevLinuxState, cc, cc);
    CryptodevLinuxSession *session;

    session = cryptodev_linux_find_session(s, op_info->session_id);
    if (session == NULL) {
        error_report("Cannot find the session: %" PRIu64 "",
                      op_info->session_id);
        return -VIRTIO_CRYPTO_OP_INVSESS;
    }

    switch (session->type) {
    case VIRTIO_CRYPTO_SYM_OP_CIPHER:
        return cryptodev_linux_handle_cipher_op(op_info, session, s->fd);
    case VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING:
        return cryptodev_linux_handle_chaining_op(op_info, session, s->fd);
    default:
        error_report("Unsupported type: %u", session->type);
        return -1;
    }
}

static void
cryptodev_linux_cleanup(CryptoClientState *cc)
{
    CryptodevLinuxState *s = DO_UPCAST(CryptodevLinuxState, cc, cc);
    CryptodevLinuxSession *session;
    uint64_t session_id;

    QTAILQ_FOREACH(session, &s->sessions, next) {
        session_id = session->sess->ses;
        if (ioctl(s->fd, CIOCFSESSION, &session_id)) {
            perror("ioctl(CIOCFSESSION)");
        }
        g_free(session->sess);
        g_free(session);
    }

    close(s->fd);
    s->fd = -1;
    s->enabled = false;
}

static void
cryptodev_linux_poll(CryptoClientState *cc, bool enable)
{

}

static CryptoClientInfo crypto_cryptodev_info = {
    .type = CRYPTO_CLIENT_OPTIONS_KIND_CRYPTODEV_LINUX,
    .size = sizeof(CryptodevLinuxState),
    .create_session = cryptodev_linux_create_session,
    .close_session = cryptodev_linux_close_session,
    .do_sym_op = cryptodev_linux_do_sym_op,
    .cleanup = cryptodev_linux_cleanup,
    .poll = cryptodev_linux_poll,
};

int crypto_init_cryptodev_linux(const CryptoClientOptions *opts,
                                const char *name,
                                CryptoClientState *peer, Error **errp)
{
    const CryptodevLinuxOptions *cryptodev;
    int fd, ret;
    CryptoClientState *cc;
    CryptodevLinuxState *s;

    assert(opts->type == CRYPTO_CLIENT_OPTIONS_KIND_CRYPTODEV_LINUX);

    cryptodev = opts->u.cryptodev_linux.data;
    if (cryptodev->has_fd) {
        if (cryptodev->fd < 0) {
            error_setg(errp, "Invaild fd: %" PRId64 "", cryptodev->fd);
            return -1;
        } else {
            fd = cryptodev->fd;
        }
    } else {
        ret = cryptodev_linux_open(&fd, errp);
        if (ret < 0) {
            return -1;
        }
    }

    cc = new_crypto_client(&crypto_cryptodev_info, peer,
                           "cryptodev-linux", name);

    cc->crypto_services = 1u << VIRTIO_CRYPTO_SERVICE_CIPHER |
                         1u << VIRTIO_CRYPTO_SERVICE_HASH |
                         1u << VIRTIO_CRYPTO_SERVICE_AEAD;
    cc->cipher_algo_l = 1u << VIRTIO_CRYPTO_CIPHER_AES_CBC;
    cc->hash_algo = 1u << VIRTIO_CRYPTO_HASH_SHA1;

    /* the cryptodev backend is ready for work */
    cc->ready = true;
    s = DO_UPCAST(CryptodevLinuxState, cc, cc);

    s->fd = fd;
    s->enabled = true;
    QTAILQ_INIT(&s->sessions);

    return 0;
}
