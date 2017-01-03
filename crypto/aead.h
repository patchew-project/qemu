/*
 * QEMU Crypto aead algorithms
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#ifndef QCRYPTO_AEAD_H
#define QCRYPTO_AEAD_H

#include "qapi-types.h"

/**
 * We currently only support six basic aead algorithms,
 * ccm(aes128/192/256) and gcm(aes128/192/256), so for
 * some global arrays, like qcrypto_aead_alg_map, we use
 * AEAD_ALG__MAX/AEAD_MODE__MAX as their size instead of
 * CIPHER_ALG__MAX/CIPHER_MODE__MAX, in order to save memory.
 *
 * Note: this means that we must make sure ccm,gcm,ecb at
 * the first three places in QCryptoCipherMode, and aes128,
 * aes192,aes256,des-rfb at the first four places in
 * QCryptoCipherAlgorithm.
 */
#define QCRYPTO_AEAD_ALG__MAX QCRYPTO_CIPHER_ALG_DES_RFB
#define QCRYPTO_AEAD_MODE__MAX QCRYPTO_CIPHER_MODE_ECB

typedef struct QCryptoAead QCryptoAead;
struct QCryptoAead {
    QCryptoCipherAlgorithm alg;
    QCryptoCipherMode mode;
    void *opaque;
};

/**
 * qcrypto_aead_supports:
 * @alg: the cipher algorithm
 * @mode: the cipher mode
 *
 * Determine if @alg hmac algorithm is supported by
 * the current configured build
 *
 * Returns:
 * true if the algorithm is supported, false otherwise
 */
bool qcrypto_aead_supports(QCryptoCipherAlgorithm alg,
                           QCryptoCipherMode mode);

/**
 * qcrypto_aead_get_key_len:
 * @alg: the cipher algorithm
 *
 * Get the required key size in bytes
 *
 * Returns: the key size in bytes
 */
size_t qcrypto_aead_get_key_len(QCryptoCipherAlgorithm alg);

/**
 * qcrypto_aead_new:
 * @alg: the cipher algorithm
 * @mode: the cipher usage mode
 * @key: the private key bytes
 * @nkey: the length of @key
 * @errp: pointer to a NULL-initialized error object
 *
 * Creates a new aead object for encrypting/decrypting
 * data with the algorithm @alg in the usage mode @mode.
 *
 * The returned aead object must be released with
 * qcrypto_aead_free() when no longer required
 *
 * Returns:
 *  a new aead object, or NULL on error
 */
QCryptoAead *qcrypto_aead_new(QCryptoCipherAlgorithm alg,
                              QCryptoCipherMode mode,
                              const uint8_t *key, size_t nkey,
                              Error **errp);

/**
 * qcrypto_aead_free:
 * @aead: the aead object
 *
 * Release the memory associated with @aead that
 * was previously allocated by qcrypto_aead_new()
 */
void qcrypto_aead_free(QCryptoAead *aead);

/**
 * qcrypto_aead_set_nonce:
 * @aead: the aead object
 * @nonce: the nonce/iv data
 * @nonce_len: the length of @nonce
 * @aad_len: the length of associated data
 * @in_len: the length of plain data
 * @tag_len: the length of authentication tag
 *
 * Set the aead object's nonce/iv
 *
 * Returns:
 *  0 if success, or -1 on error
 */
int qcrypto_aead_set_nonce(QCryptoAead *aead,
                           const uint8_t *nonce, size_t nonce_len,
                           size_t aad_len, size_t in_len,
                           size_t tag_len,
                           Error **errp);

/**
 * qcrypto_aead_authenticate:
 * @aead: the aead object
 * @aad: associated data
 * @aad_len: the length of @add
 *
 * Set associated data to be authenticated
 *
 * Returns:
 *  0 if success, or -1 on error
 */
int qcrypto_aead_authenticate(QCryptoAead *aead,
                              const uint8_t *aad, size_t aad_len,
                              Error **errp);

/**
 * qcrypto_aead_encrypt:
 * @aead: the aead object
 * @in: the plain data
 * @in_len: the length of @in
 * @out: the cipher data buffer
 * @out_len: the length of @out
 *
 * Encrypts the input data
 *
 * Returns:
 *  0 if success, or -1 on error
 */
int qcrypto_aead_encrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp);

/**
 * qcrypto_aead_decrypt:
 * @aead: the aead object
 * @in: the cipher data
 * @in_len: the length of @in
 * @out: the plain data buffer
 * @out_len: the length of @out
 *
 * Decrypts the input data
 *
 * Returns:
 *  0 if success, or -1 on error
 */
int qcrypto_aead_decrypt(QCryptoAead *aead,
                         const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_len,
                         Error **errp);

/**
 * qcrypto_aead_get_tag:
 * @aead: the aead object
 * @tag: the tag buffer
 * @tag_len: the length of @tag
 *
 * Extracts the authentication tag
 *
 * Returns:
 *  0 if success, or -1 on error
 */
int qcrypto_aead_get_tag(QCryptoAead *aead,
                         uint8_t *tag, size_t tag_len,
                         Error **errp);

#endif
