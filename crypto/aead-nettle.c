/*
 * QEMU Crypto aead algorithms (based on nettle)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Longpeng(Mike) <longpeng2@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/aead.h"
#include <nettle/aes.h>
#include <nettle/ccm.h>
#include <nettle/gcm.h>

typedef void (*qcrypto_nettle_aead_setkey)(void *ctx,
              const uint8_t *key);

typedef void (*qcrypto_nettle_aead_setiv)(void *ctx,
              size_t length, const uint8_t *iv);

typedef void (*qcrypto_nettle_aead_setnonce)(void *ctx,
              size_t length, const uint8_t *nonce,
              size_t authlen, size_t msglen, size_t taglen);

typedef void (*qcrypto_nettle_aead_update)(void *ctx,
              size_t length, const uint8_t *data);

typedef void (*qcrypto_nettle_aead_encrypt)(void *ctx,
              size_t length, uint8_t *dst, const uint8_t *src);

typedef void (*qcrypto_nettle_aead_decrypt)(void *ctx,
              size_t length, uint8_t *dst, const uint8_t *src);

typedef void (*qcrypto_nettle_aead_digest)(void *ctx,
              size_t length, uint8_t *digest);

#define NETTLE_AEAD_SET_CCM_FN(alg) { \
    .setkey = (qcrypto_nettle_aead_setkey)ccm_##alg##_set_key, \
    .u_op.setnonce = (qcrypto_nettle_aead_setnonce)ccm_##alg##_set_nonce, \
    .update = (qcrypto_nettle_aead_update)ccm_##alg##_update, \
    .encrypt = (qcrypto_nettle_aead_encrypt)ccm_##alg##_encrypt, \
    .decrypt = (qcrypto_nettle_aead_decrypt)ccm_##alg##_decrypt, \
    .digest = (qcrypto_nettle_aead_digest)ccm_##alg##_digest, \
}

#define NETTLE_AEAD_SET_GCM_FN(alg) { \
    .setkey = (qcrypto_nettle_aead_setkey)gcm_##alg##_set_key, \
    .u_op.setiv = (qcrypto_nettle_aead_setiv)gcm_##alg##_set_iv, \
    .update = (qcrypto_nettle_aead_update)gcm_##alg##_update, \
    .encrypt = (qcrypto_nettle_aead_encrypt)gcm_##alg##_encrypt, \
    .decrypt = (qcrypto_nettle_aead_decrypt)gcm_##alg##_decrypt, \
    .digest = (qcrypto_nettle_aead_digest)gcm_##alg##_digest, \
}

static struct qcrypto_nettle_aead_alg {
    qcrypto_nettle_aead_setkey setkey;
    union {
        qcrypto_nettle_aead_setnonce setnonce;
        qcrypto_nettle_aead_setiv setiv;
    } u_op;
    qcrypto_nettle_aead_update update;
    qcrypto_nettle_aead_encrypt encrypt;
    qcrypto_nettle_aead_decrypt decrypt;
    qcrypto_nettle_aead_digest digest;
} qcrypto_aead_alg_map[][QCRYPTO_AEAD_ALG__MAX] = {
    {
        [QCRYPTO_CIPHER_ALG_AES_128] = NETTLE_AEAD_SET_CCM_FN(aes128),
        [QCRYPTO_CIPHER_ALG_AES_192] = NETTLE_AEAD_SET_CCM_FN(aes192),
        [QCRYPTO_CIPHER_ALG_AES_256] = NETTLE_AEAD_SET_CCM_FN(aes256),
    },
    {
        [QCRYPTO_CIPHER_ALG_AES_128] = NETTLE_AEAD_SET_GCM_FN(aes128),
        [QCRYPTO_CIPHER_ALG_AES_192] = NETTLE_AEAD_SET_GCM_FN(aes192),
        [QCRYPTO_CIPHER_ALG_AES_256] = NETTLE_AEAD_SET_GCM_FN(aes256),
    }
};

typedef struct QCryptoAeadNettle QCryptoAeadNettle;
struct QCryptoAeadNettle {
    union qcrypto_nettle_aead_ctx {
        struct ccm_aes128_ctx c_aes128_ctx;
        struct ccm_aes192_ctx c_aes192_ctx;
        struct ccm_aes256_ctx c_aes256_ctx;
        struct gcm_aes128_ctx g_aes128_ctx;
        struct gcm_aes192_ctx g_aes192_ctx;
        struct gcm_aes256_ctx g_aes256_ctx;
    } u;
};

QCryptoAead *qcrypto_aead_new(QCryptoCipherAlgorithm alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    QCryptoAead *aead;
    QCryptoAeadNettle *ctx;

    if (!qcrypto_aead_supports(alg, mode)) {
        return NULL;
    }

    if (nkey != qcrypto_aead_get_key_len(alg)) {
        error_setg(errp, "Cipher key length %zu is invalid",
                   nkey);
        return NULL;
    }

    aead = g_new0(QCryptoAead, 1);
    aead->alg = alg;
    aead->mode = mode;

    ctx = g_new0(QCryptoAeadNettle, 1);

    qcrypto_aead_alg_map[mode][alg].setkey(&ctx->u, key);

    aead->opaque = ctx;

    return aead;
}

void qcrypto_aead_free(QCryptoAead *aead)
{
    QCryptoAeadNettle *ctx;

    if (!aead) {
        return;
    }

    ctx = aead->opaque;

    g_free(ctx);
    g_free(aead);
}

int qcrypto_aead_set_nonce(QCryptoAead *aead,
                           const uint8_t *nonce, size_t nonce_len,
                           size_t aad_len, size_t in_len,
                           size_t tag_len,
                           Error **errp)
{
    QCryptoAeadNettle *ctx;
    struct qcrypto_nettle_aead_alg *aead_ops;

    ctx = (QCryptoAeadNettle *)aead->opaque;

    switch (aead->mode) {
    case QCRYPTO_CIPHER_MODE_CCM:
        aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];
        aead_ops->u_op.setnonce(&ctx->u, nonce_len, nonce, aad_len,
                      in_len, tag_len);
        break;
    case QCRYPTO_CIPHER_MODE_GCM:
        aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];
        aead_ops->u_op.setiv(&ctx->u, nonce_len, nonce);
        break;
    default:
        error_setg(errp, "Unsupported AEAD mode: %d",
                   aead->mode);
        return -1;
    }

    return 0;
}

int qcrypto_aead_authenticate(QCryptoAead *aead,
                              const uint8_t *aad, size_t aad_len,
                              Error **errp)
{
    QCryptoAeadNettle *ctx;
    struct qcrypto_nettle_aead_alg *aead_ops;

    ctx = (QCryptoAeadNettle *)aead->opaque;
    aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];

    aead_ops->update(&ctx->u, aad_len, aad);

    return 0;
}

int qcrypto_aead_encrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    QCryptoAeadNettle *ctx;
    struct qcrypto_nettle_aead_alg *aead_ops;

    ctx = (QCryptoAeadNettle *)aead->opaque;
    aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];

    aead_ops->encrypt(&ctx->u, in_len, out, in);

    return 0;
}

int qcrypto_aead_decrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    QCryptoAeadNettle *ctx;
    struct qcrypto_nettle_aead_alg *aead_ops;

    ctx = (QCryptoAeadNettle *)aead->opaque;
    aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];

    aead_ops->decrypt(&ctx->u, in_len, out, in);

    return 0;
}

int qcrypto_aead_get_tag(QCryptoAead *aead,
                         uint8_t *tag, size_t tag_len,
                         Error **errp)
{
    QCryptoAeadNettle *ctx;
    struct qcrypto_nettle_aead_alg *aead_ops;

    ctx = (QCryptoAeadNettle *)aead->opaque;
    aead_ops = &qcrypto_aead_alg_map[aead->mode][aead->alg];

    aead_ops->digest(&ctx->u, tag_len, tag);

    return 0;
}
