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

QCryptoAead *qcrypto_aead_new(QCryptoCipherAlgorithm alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              Error **errp)
{
    return NULL;
}

void qcrypto_aead_free(QCryptoAead *aead)
{
    return;
}

int qcrypto_aead_set_nonce(QCryptoAead *aead,
                           const uint8_t *nonce, size_t nonce_len,
                           size_t aad_len, size_t in_len,
                           size_t tag_len,
                           Error **errp)
{
    return -1;
}

int qcrypto_aead_authenticate(QCryptoAead *aead,
                              const uint8_t *aad, size_t aad_len,
                              Error **errp)
{
    return -1;
}

int qcrypto_aead_encrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    return -1;
}

int qcrypto_aead_decrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp)
{
    return -1;
}

int qcrypto_aead_get_tag(QCryptoAead *aead,
                         uint8_t *tag, size_t tag_len,
                         Error **errp)
{
    return -1;
}
