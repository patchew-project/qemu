/*
 * QEMU Crypto af_alg-backend hash support
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
#include "hashpriv.h"

static char *
qcrypto_afalg_hash_format_name(QCryptoHashAlgorithm alg,
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
    ret = snprintf(name, SALG_NAME_LEN_MAX, "%s", alg_name);
    if (ret < 0 || ret >= SALG_NAME_LEN_MAX) {
        error_setg(errp, "Build hash name(name='%s') failed",
                   alg_name);
        g_free(name);
        return NULL;
    }

    return name;
}

static QCryptoAFAlg *
qcrypto_afalg_hash_ctx_new(QCryptoHashAlgorithm alg, Error **errp)
{
    QCryptoAFAlg *afalg;
    char *name;

    name = qcrypto_afalg_hash_format_name(alg, errp);
    if (!name) {
        return NULL;
    }

    afalg = qcrypto_afalg_comm_alloc(AFALG_TYPE_HASH, name, errp);
    if (!afalg) {
        goto error;
    }

    /* prepare msg header */
    afalg->msg = g_new0(struct msghdr, 1);

cleanup:
    g_free(name);
    return afalg;

error:
    qcrypto_afalg_comm_free(afalg);
    goto cleanup;
}

static int
qcrypto_afalg_hash_bytesv(QCryptoHashAlgorithm alg,
                          const struct iovec *iov,
                          size_t niov, uint8_t **result,
                          size_t *resultlen,
                          Error **errp)
{
    QCryptoAFAlg *afalg;
    struct iovec outv;
    int ret = 0;
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

    afalg = qcrypto_afalg_hash_ctx_new(alg, errp);
    if (afalg == NULL) {
        return -1;
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
    qcrypto_afalg_comm_free(afalg);
    return ret;
}

QCryptoHashDriver qcrypto_hash_afalg_driver = {
    .hash_bytesv = qcrypto_afalg_hash_bytesv,
};
