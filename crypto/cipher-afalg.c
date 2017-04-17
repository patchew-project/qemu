/*
 * QEMU Crypto af_alg-backend cipher support
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "crypto/cipher.h"
#include "cipherpriv.h"


static char *
qcrypto_afalg_cipher_format_name(QCryptoCipherAlgorithm alg,
                                 QCryptoCipherMode mode,
                                 Error **errp)
{
    char *name;
    const char *alg_name = NULL;
    const char *mode_name = NULL;
    int ret;

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        alg_name = "aes";
        break;
    case QCRYPTO_CIPHER_ALG_CAST5_128:
        alg_name = "cast5";
        break;
    case QCRYPTO_CIPHER_ALG_SERPENT_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_192:
    case QCRYPTO_CIPHER_ALG_SERPENT_256:
        alg_name = "serpent";
        break;
    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
    case QCRYPTO_CIPHER_ALG_TWOFISH_192:
    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
        alg_name = "twofish";
        break;

    default:
        error_setg(errp, "Unsupported cipher algorithm %d", alg);
        return NULL;
    }

    mode_name = QCryptoCipherMode_lookup[mode];

    name = (char *)g_new0(int8_t, SALG_NAME_LEN_MAX);
    ret = snprintf(name, SALG_NAME_LEN_MAX, "%s(%s)", mode_name,
                   alg_name);
    if (ret < 0 || ret >= SALG_NAME_LEN_MAX) {
        error_setg(errp, "Build ciphername(name='%s',mode='%s') failed",
                   alg_name, mode_name);
        g_free(name);
        return NULL;
    }

    return name;
}

QCryptoAFAlg *
qcrypto_afalg_cipher_ctx_new(QCryptoCipherAlgorithm alg,
                             QCryptoCipherMode mode,
                             const uint8_t *key,
                             size_t nkey, Error **errp)
{
    QCryptoAFAlg *afalg;
    size_t except_niv = 0;
    char *name;

    name = qcrypto_afalg_cipher_format_name(alg, mode, errp);
    if (!name) {
        return NULL;
    }

    afalg = qcrypto_afalg_comm_alloc(AFALG_TYPE_CIPHER, name, errp);
    if (!afalg) {
        goto error;
    }

    /* setkey */
    if (qemu_setsockopt(afalg->tfmfd, SOL_ALG, ALG_SET_KEY, key,
                        nkey) != 0) {
        error_setg_errno(errp, errno, "Set key failed");
        goto error;
    }

    /* prepare msg header */
    afalg->msg = g_new0(struct msghdr, 1);
    afalg->msg->msg_controllen += CMSG_SPACE(ALG_OPTYPE_LEN);
    except_niv = qcrypto_cipher_get_iv_len(alg, mode);
    if (except_niv) {
        afalg->msg->msg_controllen += CMSG_SPACE(ALG_MSGIV_LEN(except_niv));
    }
    afalg->msg->msg_control = g_new0(uint8_t, afalg->msg->msg_controllen);

    /* We use 1st msghdr for crypto-info and 2nd msghdr for IV-info */
    afalg->cmsg = CMSG_FIRSTHDR(afalg->msg);
    afalg->cmsg->cmsg_level = SOL_ALG;
    afalg->cmsg->cmsg_type = ALG_SET_OP;
    afalg->cmsg->cmsg_len = CMSG_SPACE(ALG_OPTYPE_LEN);

cleanup:
    g_free(name);
    return afalg;

error:
    qcrypto_afalg_comm_free(afalg);
    afalg = NULL;
    goto cleanup;
}

static int
qcrypto_afalg_cipher_setiv(QCryptoCipher *cipher,
                           const uint8_t *iv,
                           size_t niv, Error **errp)
{
    struct af_alg_iv *alg_iv = NULL;
    QCryptoAFAlg *afalg = cipher->opaque;

    /* move ->cmsg to next msghdr, for IV-info */
    afalg->cmsg = CMSG_NXTHDR(afalg->msg, afalg->cmsg);

    /* build setiv msg */
    afalg->cmsg->cmsg_level = SOL_ALG;
    afalg->cmsg->cmsg_type = ALG_SET_IV;
    afalg->cmsg->cmsg_len = CMSG_SPACE(ALG_MSGIV_LEN(niv));
    alg_iv = (struct af_alg_iv *)CMSG_DATA(afalg->cmsg);
    alg_iv->ivlen = niv;
    memcpy(alg_iv->iv, iv, niv);

    return 0;
}

static int
qcrypto_afalg_cipher_op(QCryptoAFAlg *afalg,
                        const void *in, void *out,
                        size_t len, bool do_encrypt,
                        Error **errp)
{
    uint32_t *type = NULL;
    struct iovec iov;
    size_t ret, done = 0;
    uint32_t origin_contorllen;

    origin_contorllen = afalg->msg->msg_controllen;
    /* movev ->cmsg to first header, for crypto-info */
    afalg->cmsg = CMSG_FIRSTHDR(afalg->msg);

    /* build encrypt msg */
    afalg->msg->msg_iov = &iov;
    afalg->msg->msg_iovlen = 1;
    type = (uint32_t *)CMSG_DATA(afalg->cmsg);
    if (do_encrypt) {
        *type = ALG_OP_ENCRYPT;
    } else {
        *type = ALG_OP_DECRYPT;
    }

    do {
        iov.iov_base = (void *)in + done;
        iov.iov_len = len - done;

        /* send info to AF_ALG core */
        ret = sendmsg(afalg->opfd, afalg->msg, 0);
        if (ret == -1) {
            error_setg_errno(errp, errno, "Send data to AF_ALG core failed");
            return -1;
        }

        /* encrypto && get result */
        if (ret != read(afalg->opfd, out, ret)) {
            error_setg_errno(errp, errno, "Get result from AF_ALG core failed");
            return -1;
        }

        /* do not update IV for following chunks */
        afalg->msg->msg_controllen = 0;
        done += ret;
    } while (done < len);

    afalg->msg->msg_controllen = origin_contorllen;

    return 0;
}

static int
qcrypto_afalg_cipher_encrypt(QCryptoCipher *cipher,
                             const void *in, void *out,
                             size_t len, Error **errp)
{
    return qcrypto_afalg_cipher_op(cipher->opaque, in, out,
                                   len, true, errp);
}

static int
qcrypto_afalg_cipher_decrypt(QCryptoCipher *cipher,
                             const void *in, void *out,
                             size_t len, Error **errp)
{
    return qcrypto_afalg_cipher_op(cipher->opaque, in, out,
                                   len, false, errp);
}

static void qcrypto_afalg_comm_ctx_free(QCryptoCipher *cipher)
{
    qcrypto_afalg_comm_free(cipher->opaque);
}

struct QCryptoCipherDriver qcrypto_cipher_afalg_driver = {
    .cipher_encrypt = qcrypto_afalg_cipher_encrypt,
    .cipher_decrypt = qcrypto_afalg_cipher_decrypt,
    .cipher_setiv = qcrypto_afalg_cipher_setiv,
    .cipher_free = qcrypto_afalg_comm_ctx_free,
};
