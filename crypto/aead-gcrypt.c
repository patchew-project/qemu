/*
 * QEMU Crypto aead algorithms (based on libgcrypt)
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
#include <gcrypt.h>

typedef struct QCryptoAeadGcrypt QCryptoAeadGcrypt;
struct QCryptoAeadGcrypt {
    gcry_cipher_hd_t handle;
};

QCryptoAead *qcrypto_aead_new(QCryptoCipherAlgorithm alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    QCryptoAead *aead;
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;
    int gcryalg, gcrymode;

    switch (mode) {
    case QCRYPTO_CIPHER_MODE_CCM:
        gcrymode = GCRY_CIPHER_MODE_CCM;
        break;
    case QCRYPTO_CIPHER_MODE_GCM:
        gcrymode = GCRY_CIPHER_MODE_GCM;
        break;
    default:
        error_setg(errp, "Unsupported AEAD mode %s",
                   QCryptoCipherMode_lookup[mode]);
        return NULL;
    }

    if (nkey != qcrypto_aead_get_key_len(alg)) {
        error_setg(errp, "Cipher key length %zu is invalid",
                   nkey);
        return NULL;
    }

    switch (alg) {
    case QCRYPTO_CIPHER_ALG_AES_128:
        gcryalg = GCRY_CIPHER_AES128;
        break;
    case QCRYPTO_CIPHER_ALG_AES_192:
        gcryalg = GCRY_CIPHER_AES192;
        break;
    case QCRYPTO_CIPHER_ALG_AES_256:
        gcryalg = GCRY_CIPHER_AES256;
        break;
    default:
        error_setg(errp, "Unsupported AEAD algorithm %s",
                   QCryptoCipherAlgorithm_lookup[alg]);
        return NULL;
    }

    aead = g_new0(QCryptoAead, 1);
    aead->alg = alg;
    aead->mode = mode;

    ctx = g_new0(QCryptoAeadGcrypt, 1);

    err = gcry_cipher_open(&ctx->handle, gcryalg, gcrymode, 0);
    if (err) {
        error_setg(errp, "Cannot initialize aead: %s",
                   gcry_strerror(err));
        goto error;
    }

    err = gcry_cipher_setkey(ctx->handle, key, nkey);
    if (err) {
        error_setg(errp, "Cannot set key: %s",
                   gcry_strerror(err));
        goto error;
    }

    aead->opaque = ctx;

    return aead;

error:
    gcry_cipher_close(ctx->handle);
    g_free(ctx);
    g_free(aead);
    return NULL;
}

void qcrypto_aead_free(QCryptoAead *aead)
{
    QCryptoAeadGcrypt *ctx;

    if (!aead) {
        return;
    }

    ctx = aead->opaque;

    gcry_cipher_close(ctx->handle);
    g_free(ctx);
    g_free(aead);
}

int qcrypto_aead_set_nonce(QCryptoAead *aead,
                           const uint8_t *nonce, size_t nonce_len,
                           size_t aad_len, size_t in_len,
                           size_t tag_len,
                           Error **errp)
{
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;

    ctx = aead->opaque;

    err = gcry_cipher_setiv(ctx->handle, nonce, nonce_len);
    if (err) {
        error_setg(errp, "Cannot set iv/nonce: %s",
                   gcry_strerror(err));
        return -1;
    }

    if (aead->mode == QCRYPTO_CIPHER_MODE_CCM) {
        size_t ctl_para[3];

        ctl_para[0] = in_len;
        ctl_para[1] = aad_len;
        ctl_para[2] = tag_len;

        err = gcry_cipher_ctl(ctx->handle, GCRYCTL_SET_CCM_LENGTHS,
                              ctl_para, sizeof(ctl_para));
        if (err) {
            error_setg(errp, "Cannot set lengths: %s",
                       gcry_strerror(err));
            return -1;
        }
    }

    return 0;
}

int qcrypto_aead_authenticate(QCryptoAead *aead,
                              const uint8_t *aad, size_t aad_len,
                              Error **errp)
{
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;

    ctx = aead->opaque;

    err = gcry_cipher_authenticate(ctx->handle, aad, aad_len);
    if (err) {
        error_setg(errp, "Cannot set associated data: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}

int qcrypto_aead_encrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;

    ctx = aead->opaque;

    err = gcry_cipher_encrypt(ctx->handle, out, out_len,
                              in, in_len);
    if (err) {
        error_setg(errp, "Cannot encrypt data: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}

int qcrypto_aead_decrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;

    ctx = aead->opaque;

    err = gcry_cipher_decrypt(ctx->handle, out, out_len,
                              in, in_len);
    if (err) {
        error_setg(errp, "Cannot decrypt data: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}

int qcrypto_aead_get_tag(QCryptoAead *aead,
                         uint8_t *tag, size_t tag_len,
                         Error **errp)
{
    QCryptoAeadGcrypt *ctx;
    gcry_error_t err;

    ctx = aead->opaque;

    err = gcry_cipher_gettag(ctx->handle, tag, tag_len);
    if (err) {
        error_setg(errp, "Cannot get tag: %s",
                   gcry_strerror(err));
        return -1;
    }

    return 0;
}
