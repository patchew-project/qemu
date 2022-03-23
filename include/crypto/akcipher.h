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

#include "qemu/typedefs.h"
#include "qapi/qapi-types-crypto.h"

typedef struct QCryptoAkcipher QCryptoAkcipher;
typedef struct QCryptoAkcipherDriver QCryptoAkcipherDriver;

struct QCryptoAkcipherDriver {
    int (*encrypt)(struct QCryptoAkcipher *akcipher,
                   const void *data, size_t data_len,
                   void *enc, size_t enc_len, Error **errp);
    int (*decrypt)(struct QCryptoAkcipher *akcipher,
                   const void *enc, size_t enc_len,
                   void *data, size_t data_len, Error **errp);
    int (*sign)(struct QCryptoAkcipher *akcipher,
                const void *data, size_t data_len,
                void *sig, size_t sig_len, Error **errp);
    int (*verify)(struct QCryptoAkcipher *akcipher,
                  const void *sig, size_t sig_len,
                  const void *data, size_t data_len, Error **errp);
    int (*free)(struct QCryptoAkcipher *akcipher, Error **errp);
};

struct QCryptoAkcipher {
    QCryptoAkcipherAlgorithm alg;
    QCryptoAkcipherKeyType type;
    uint8_t *key;
    size_t keylen;
    int max_plaintext_len;
    int max_ciphertext_len;
    int max_signature_len;
    int max_dgst_len;
    QCryptoAkcipherDriver *driver;
};

QCryptoAkcipher *qcrypto_akcipher_new(QCryptoAkcipherAlgorithm alg,
                                      QCryptoAkcipherKeyType type,
                                      const uint8_t *key, size_t keylen,
                                      void *para, Error **errp);

/**
 * qcrypto_akcipher_encrypt:
 * @akcipher: akcipher used to do encryption
 * @data: plaintext pending to be encrypted
 * @data_len: length of the plaintext, MUST less or equal
 * akcipher->max_plaintext_len
 * @enc: buffer to store the ciphertext
 * @enc_len: the length of ciphertext buffer, usually equals to
 * akcipher->max_ciphertext_len
 * @errp: error pointer
 *
 * Encrypt data and write ciphertext into enc
 *
 * Returns: length of ciphertext if encrypt succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_encrypt(QCryptoAkcipher *akcipher,
                             const void *data, size_t data_len,
                             void *enc, size_t enc_len, Error **errp);

/**
 * qcrypto_akcipher_decrypt:
 * @akcipher: akcipher used to do decryption
 * @enc: ciphertext to be decrypted
 * @enc_len: the length of ciphertext
 * @data: buffer to store the plaintext
 * @data_len: length of the plaintext buffer, usually equals to
 * akcipher->max_plaintext_len
 * @errp: error pointer
 *
 * Decrypt ciphertext and write plaintext into data
 *
 * Returns: length of plaintext if decrypt succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_decrypt(struct QCryptoAkcipher *akcipher,
                             const void *enc, size_t enc_len,
                             void *data, size_t data_len, Error **errp);

/**
 * qcrypto_akcipher_sign:
 * @akcipher: akcipher used to generate signature
 * @data: data to be signed
 * @data_len: the length of data
 * @sig: buffer to store the signature
 * @sig_len: length of the signature buffer, usually equals to
 * akcipher->max_signature_len
 * @errp: error pointer
 *
 * Generate signature for data using akcipher
 *
 * Returns: length of signature if succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_sign(struct QCryptoAkcipher *akcipher,
                          const void *data, size_t data_len,
                          void *sig, size_t sig_len, Error **errp);

/**
 * qcrypto_akcipher_verify:
 * @akcipher: akcipher used to do verifycation
 * @sig: pointer to the signature
 * @sig_len: length of the signature
 * @data: pointer to original data
 * @data_len: the length of data
 * @errp: error pointer
 *
 * Verify the signature and the data match or not
 *
 * Returns: 0 for succeed, otherwise -1 is returned
 */
int qcrypto_akcipher_verify(struct QCryptoAkcipher *akcipher,
                            const void *sig, size_t sig_len,
                            const void *data, size_t data_len, Error **errp);

int qcrypto_akcipher_free(struct QCryptoAkcipher *akcipher, Error **errp);


#endif /* QCRYPTO_AKCIPHER_H */
