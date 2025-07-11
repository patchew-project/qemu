/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QCRYPTO_X509_UTILS_H
#define QCRYPTO_X509_UTILS_H

#include "crypto/hash.h"

typedef enum {
    QCRYPTO_KEYID_FLAGS_SHA1,
    QCRYPTO_KEYID_FLAGS_SHA256,
    QCRYPTO_KEYID_FLAGS_SHA512,
    QCRYPTO_KEYID_FLAGS_BEST_KNOWN,
} QCryptoKeyidFlags;

typedef enum {
    QCRYPTO_SIG_ALGO_UNKNOWN,
    QCRYPTO_SIG_ALGO_RSA_SHA1,
    QCRYPTO_SIG_ALGO_DSA_SHA1,
    QCRYPTO_SIG_ALGO_RSA_MD5,
    QCRYPTO_SIG_ALGO_RSA_MD2,
    QCRYPTO_SIG_ALGO_RSA_RMD160,
    QCRYPTO_SIG_ALGO_RSA_SHA256,
    QCRYPTO_SIG_ALGO_RSA_SHA384,
    QCRYPTO_SIG_ALGO_RSA_SHA512,
    QCRYPTO_SIG_ALGO_RSA_SHA224,
    QCRYPTO_SIG_ALGO_DSA_SHA224,
    QCRYPTO_SIG_ALGO_DSA_SHA256,
    QCRYPTO_SIG_ALGO_ECDSA_SHA1,
    QCRYPTO_SIG_ALGO_ECDSA_SHA224,
    QCRYPTO_SIG_ALGO_ECDSA_SHA256,
    QCRYPTO_SIG_ALGO_ECDSA_SHA384,
    QCRYPTO_SIG_ALGO_ECDSA_SHA512,
} QCryptoSigAlgo;

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp);

/**
 * qcrypto_x509_convert_cert_der
 * @cert: pointer to the raw certificate data in PEM format
 * @size: size of the certificate
 * @result: output location for the allocated buffer for the certificate in DER format
            (the function allocates memory which must be freed by the caller)
 * @resultlen: pointer to the size of the buffer
               (will be replaced by the actual size of the DER-encoded certificate)
 * @errp: error pointer
 *
 * Convert given @cert from PEM to DER format.
 *
 * Returns: 0 on success,
 *         -1 on error.
 */
int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp);

/**
 * qcrypto_x509_get_keyid_len
 * @flag: the key ID flag
 *
 * Determine the length of the key ID of the given @flag.
 *
 * Returns: the length on success,
 *          -1 on error.
 */
int qcrypto_x509_get_keyid_len(QCryptoKeyidFlags flag, Error **errp);

/**
 * qcrypto_x509_get_signature_algorithm
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the signature algorithm used to sign the @cert.
 *
 * Returns: a value from the QCryptoSigAlgo enum on success,
 *          -1 on error.
 */
int qcrypto_x509_get_signature_algorithm(uint8_t *cert, size_t size, Error **errp);

#endif
