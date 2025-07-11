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

typedef enum {
    QCRYPTO_PK_ALGO_UNKNOWN,
    QCRYPTO_PK_ALGO_RSA,
    QCRYPTO_PK_ALGO_DSA,
    QCRYPTO_PK_ALGO_DH,
    QCRYPTO_PK_ALGO_ECDSA,
} QCryptoPkAlgo;

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
               (will be updated with the actual size of the DER-encoded certificate)
 * @errp: error pointer
 *
 * Convert the given @cert from PEM to DER format.
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
 * Determine the length of the key ID corresponding to the given @flag.
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

/**
 * qcrypto_x509_get_cert_version
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the version of the @cert.
 *
 * Returns: the version on success,
 *          -1 on error.
 */
int qcrypto_x509_get_cert_version(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_x509_check_cert_times
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Check whether the activation and expiration times of @cert
 * are valid at the current time.
 *
 * Returns: 0 if the certificate times are valid,
 *         -1 on error.
 */
int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_x509_get_pk_algorithm
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @errp: error pointer
 *
 * Determine the public key algorithm of the @cert.
 *
 * Returns: a value from the QCryptoPkAlgo enum on success,
 *          -1 on error.
 */
int qcrypto_x509_get_pk_algorithm(uint8_t *cert, size_t size, Error **errp);

/**
 * qcrypto_x509_get_cert_key_id
 * @cert: pointer to the raw certificate data
 * @size: size of the certificate
 * @flag: the key ID flag
 * @result: output location for the allocated buffer for key ID
            (the function allocates memory which must be freed by the caller)
 * @resultlen: pointer to the size of the buffer
               (will be updated with the actual size of key id)
 * @errp: error pointer
 *
 * Retrieve the key ID from the @cert based on the specified @flag.
 *
 * Returns: 0 if key ID was successfully stored in @result,
 *         -1 on error.
 */
int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp);

/**
 * qcrypto_pkcs7_convert_sig_pem
 * @sig: pointer to the PKCS#7 signature in DER format
 * @sig_size: size of the signature
 * @result: output location for the allocated buffer for the signature in PEM format
            (the function allocates memory which must be freed by the caller)
 * @resultlen: pointer to the size of the buffer
               (will be updated with the actual size of the PEM-encoded signature)
 * @errp: error pointer
 *
 * Convert given PKCS#7 @sig from DER to PEM format.
 *
 * Returns: 0 if PEM-encoded signature was successfully stored in @result,
 *         -1 on error.
 */
int qcrypto_pkcs7_convert_sig_pem(uint8_t *sig, size_t sig_size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp);

/**
 * qcrypto_x509_verify_sig
 * @cert: pointer to the raw certificate data
 * @cert_size: size of the certificate
 * @comp: pointer to the component to be verified
 * @comp_size: size of the component
 * @sig: pointer to the signature
 * @sig_size: size of the signature
 * @errp: error pointer
 *
 * Verify the provided @comp against the @sig and @cert.
 *
 * Returns: 0 on success,
 *         -1 on error.
 */
int qcrypto_x509_verify_sig(uint8_t *cert, size_t cert_size,
                            uint8_t *comp, size_t comp_size,
                            uint8_t *sig, size_t sig_size, Error **errp);

#endif
