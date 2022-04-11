/*
 * QEMU Crypto asymmetric algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: zhenwei pi <pizhenwei@bytedance.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QCRYPTO_AKCIPHER_H
#define QCRYPTO_AKCIPHER_H

#include "qapi/qapi-types-crypto.h"

typedef struct QCryptoAkCipher QCryptoAkCipher;
typedef struct QCryptoAkCipherDriver QCryptoAkCipherDriver;

struct QCryptoAkCipher {
    QCryptoAkCipherAlgorithm alg;
    QCryptoAkCipherKeyType type;
    int max_plaintext_len;
    int max_ciphertext_len;
    int max_signature_len;
    int max_dgst_len;
    QCryptoAkCipherDriver *driver;
};

/**
 * qcrypto_akcipher_supports:
 * @opts: the asymmetric key algorithm and related options
 *
 * Determine if asymmetric key cipher decribed with @opts is
 * supported by the current configured build
 *
 * Returns: true if it is supported, false otherwise.
 */
bool qcrypto_akcipher_supports(QCryptoAkCipherOptions *opts);

/**
 * qcrypto_akcipher_new:
 * @opts: specify the algorithm and the related arguments
 * @type: private or public key type
 * @key: buffer to store the key
 * @key_len: the length of key buffer
 * @errp: error pointer
 *
 * Create akcipher context
 *
 * Returns: On success, a new QCryptoAkCipher initialized with @opt
 * is created and returned, otherwise NULL is returned.
 */

QCryptoAkCipher *qcrypto_akcipher_new(const QCryptoAkCipherOptions *opts,
                                      QCryptoAkCipherKeyType type,
                                      const uint8_t *key, size_t key_len,
                                      Error **errp);

/**
 * qcrypto_akcipher_encrypt:
 * @akcipher: akcipher context
 * @in: plaintext pending to be encrypted
 * @in_len: length of the plaintext, MUST less or equal to max_plaintext_len
 * @out: buffer to store the ciphertext
 * @out_len: the length of ciphertext buffer, usually equals to
 *           max_ciphertext_len
 * @errp: error pointer
 *
 * Encrypt data and write ciphertext into out
 *
 * Returns: length of ciphertext if encrypt succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_encrypt(QCryptoAkCipher *akcipher,
                             const void *in, size_t in_len,
                             void *out, size_t out_len, Error **errp);

/**
 * qcrypto_akcipher_decrypt:
 * @akcipher: akcipher context
 * @in: ciphertext to be decrypted
 * @in_len: the length of ciphertext
 * @out: buffer to store the plaintext
 * @out_len: length of the plaintext buffer, usually less or equals to
 *           max_plaintext_len
 * @errp: error pointer
 *
 * Decrypt ciphertext and write plaintext into out
 *
 * Returns: length of plaintext if decrypt succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_decrypt(QCryptoAkCipher *akcipher,
                             const void *in, size_t in_len,
                             void *out, size_t out_len, Error **errp);

/**
 * qcrypto_akcipher_sign:
 * @akcipher: akcipher context
 * @in: data to be signed
 * @in_len: the length of data
 * @out: buffer to store the signature
 * @out_len: length of the signature buffer, usually equals to max_signature_len
 * @errp: error pointer
 *
 * Generate signature for data using akcipher
 *
 * Returns: length of signature if succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_sign(QCryptoAkCipher *akcipher,
                          const void *in, size_t in_len,
                          void *out, size_t out_len, Error **errp);

/**
 * qcrypto_akcipher_verify:
 * @akcipher: akcipher used to do verifycation
 * @in: pointer to the signature
 * @in_len: length of the signature
 * @in2: pointer to original data
 * @in2_len: the length of original data
 * @errp: error pointer
 *
 * Verify the signature and the data match or not
 *
 * Returns: 0 for succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_verify(QCryptoAkCipher *akcipher,
                            const void *in, size_t in_len,
                            const void *in2, size_t in2_len, Error **errp);

int qcrypto_akcipher_max_plaintext_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_ciphertext_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_signature_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_max_dgst_len(QCryptoAkCipher *akcipher);

int qcrypto_akcipher_free(QCryptoAkCipher *akcipher, Error **errp);


#endif /* QCRYPTO_AKCIPHER_H */
