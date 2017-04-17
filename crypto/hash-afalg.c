/*
 * QEMU Crypto af_alg-backend hash/hmac support
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
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "crypto/hmac.h"
#include "hashpriv.h"
#include "hmacpriv.h"

static char *
qcrypto_afalg_hash_format_name(QCryptoHashAlgorithm alg,
                               bool is_hmac,
                               Error **errp)
{
    char *name;
    const char *alg_name = NULL;
    int ret;

    switch (alg) {
    case QCRYPTO_HASH_ALG_MD5:
        alg_name = "md5";
        break;
    case QCRYPTO_HASH_ALG_SHA1:
        alg_name = "sha1";
        break;
    case QCRYPTO_HASH_ALG_SHA224:
        alg_name = "sha224";
        break;
    case QCRYPTO_HASH_ALG_SHA256:
        alg_name = "sha256";
        break;
    case QCRYPTO_HASH_ALG_SHA384:
        alg_name = "sha384";
        break;
    case QCRYPTO_HASH_ALG_SHA512:
        alg_name = "sha512";
        break;
    case QCRYPTO_HASH_ALG_RIPEMD160:
        alg_name = "rmd160";
        break;

    default:
        error_setg(errp, "Unsupported hash algorithm %d", alg);
        return NULL;
    }

    name = (char *)g_new0(int8_t, SALG_NAME_LEN_MAX);
    if (is_hmac) {
        ret = snprintf(name, SALG_NAME_LEN_MAX, "hmac(%s)", alg_name);
    } else { /* hash */
        ret = snprintf(name, SALG_NAME_LEN_MAX, "%s", alg_name);
    }
    if (ret < 0 || ret >= SALG_NAME_LEN_MAX) {
        error_setg(errp, "Build %s name(name='%s') failed",
                   is_hmac ? "hmac" : "hash", alg_name);
        g_free(name);
        return NULL;
    }

    return name;
}

static QCryptoAFAlg *
qcrypto_afalg_hash_hmac_ctx_new(QCryptoHashAlgorithm alg,
                                const uint8_t *key, size_t nkey,
                                bool is_hmac, Error **errp)
{
    QCryptoAFAlg *afalg;
    char *name;

    name = qcrypto_afalg_hash_format_name(alg, is_hmac, errp);
    if (!name) {
        return NULL;
    }

    afalg = qcrypto_afalg_comm_alloc(AFALG_TYPE_HASH, name, errp);
    if (!afalg) {
        goto error;
    }

    /* HMAC needs setkey */
    if (is_hmac) {
        if (qemu_setsockopt(afalg->tfmfd, SOL_ALG, ALG_SET_KEY,
                            key, nkey) != 0) {
            error_setg_errno(errp, errno, "Set hmac key failed");
            goto error;
        }
    }

    /* prepare msg header */
    afalg->msg = g_new0(struct msghdr, 1);

cleanup:
    g_free(name);
    return afalg;

error:
    qcrypto_afalg_comm_free(afalg);
    afalg = NULL;
    goto cleanup;
}

static QCryptoAFAlg *
qcrypto_afalg_hash_ctx_new(QCryptoHashAlgorithm alg,
                           Error **errp)
{
    return qcrypto_afalg_hash_hmac_ctx_new(alg, NULL, 0, false, errp);
}

QCryptoAFAlg *
qcrypto_afalg_hmac_ctx_new(QCryptoHashAlgorithm alg,
                           const uint8_t *key, size_t nkey,
                           Error **errp)
{
    return qcrypto_afalg_hash_hmac_ctx_new(alg, key, nkey, true, errp);
}

static int
qcrypto_afalg_hash_hmac_bytesv(QCryptoAFAlg *hmac,
                               QCryptoHashAlgorithm alg,
                               const struct iovec *iov,
                               size_t niov, uint8_t **result,
                               size_t *resultlen,
                               Error **errp)
{
    QCryptoAFAlg *afalg;
    struct iovec outv;
    int ret = 0;
    bool is_hmac = (hmac != NULL) ? true : false;
    const int except_len = qcrypto_hash_digest_len(alg);

    if (*resultlen == 0) {
        *resultlen = except_len;
        *result = g_new0(uint8_t, *resultlen);
    } else if (*resultlen != except_len) {
        error_setg(errp,
                   "Result buffer size %zu is not match hash %d",
                   *resultlen, except_len);
        return -1;
    }

    if (is_hmac) {
        afalg = hmac;
    } else {
        afalg = qcrypto_afalg_hash_ctx_new(alg, errp);
        if (afalg == NULL) {
            return -1;
        }
    }

    /* send data to kernel's crypto core */
    ret = iov_send_recv(afalg->opfd, iov, niov,
                        0, iov_size(iov, niov), true);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Send data to afalg-core failed");
        goto out;
    }

    /* hash && get result */
    outv.iov_base = *result;
    outv.iov_len = *resultlen;
    afalg->msg->msg_iov = &outv;
    afalg->msg->msg_iovlen = 1;
    ret = recvmsg(afalg->opfd, afalg->msg, 0);
    if (ret != -1) {
        ret = 0;
    } else {
        error_setg_errno(errp, errno, "Recv result from afalg-core failed");
    }

out:
    if (!is_hmac) {
        qcrypto_afalg_comm_free(afalg);
    }
    return ret;
}

static int
qcrypto_afalg_hash_bytesv(QCryptoHashAlgorithm alg,
                          const struct iovec *iov,
                          size_t niov, uint8_t **result,
                          size_t *resultlen,
                          Error **errp)
{
    return qcrypto_afalg_hash_hmac_bytesv(NULL, alg, iov, niov, result,
                                          resultlen, errp);
}

static int
qcrypto_afalg_hmac_bytesv(QCryptoHmac *hmac,
                          const struct iovec *iov,
                          size_t niov, uint8_t **result,
                          size_t *resultlen,
                          Error **errp)
{
    return qcrypto_afalg_hash_hmac_bytesv(hmac->opaque, hmac->alg,
                                          iov, niov, result, resultlen,
                                          errp);
}

static void qcrypto_afalg_hmac_ctx_free(QCryptoHmac *hmac)
{
    QCryptoAFAlg *afalg;

    afalg = hmac->opaque;
    qcrypto_afalg_comm_free(afalg);
}

QCryptoHashDriver qcrypto_hash_afalg_driver = {
    .hash_bytesv = qcrypto_afalg_hash_bytesv,
};

QCryptoHmacDriver qcrypto_hmac_afalg_driver = {
    .hmac_bytesv = qcrypto_afalg_hmac_bytesv,
    .hmac_free = qcrypto_afalg_hmac_ctx_free,
};
