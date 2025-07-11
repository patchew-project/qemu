/*
 * X.509 certificate related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/x509-utils.h"

#ifdef CONFIG_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include <gnutls/x509.h>
#include <gnutls/pkcs7.h>

static const int qcrypto_to_gnutls_hash_alg_map[QCRYPTO_HASH_ALGO__MAX] = {
    [QCRYPTO_HASH_ALGO_MD5] = GNUTLS_DIG_MD5,
    [QCRYPTO_HASH_ALGO_SHA1] = GNUTLS_DIG_SHA1,
    [QCRYPTO_HASH_ALGO_SHA224] = GNUTLS_DIG_SHA224,
    [QCRYPTO_HASH_ALGO_SHA256] = GNUTLS_DIG_SHA256,
    [QCRYPTO_HASH_ALGO_SHA384] = GNUTLS_DIG_SHA384,
    [QCRYPTO_HASH_ALGO_SHA512] = GNUTLS_DIG_SHA512,
    [QCRYPTO_HASH_ALGO_RIPEMD160] = GNUTLS_DIG_RMD160,
};

static const int qcrypto_to_gnutls_keyid_flags_map[] = {
    [QCRYPTO_KEYID_FLAGS_SHA1] = GNUTLS_KEYID_USE_SHA1,
    [QCRYPTO_KEYID_FLAGS_SHA256] = GNUTLS_KEYID_USE_SHA256,
    [QCRYPTO_KEYID_FLAGS_SHA512] = GNUTLS_KEYID_USE_SHA512,
    [QCRYPTO_KEYID_FLAGS_BEST_KNOWN] = GNUTLS_KEYID_USE_BEST_KNOWN,
};

static const int gnutls_to_qcrypto_sig_alg_map[] = {
    [GNUTLS_SIGN_UNKNOWN] = QCRYPTO_SIG_ALGO_UNKNOWN,
    [GNUTLS_SIGN_RSA_SHA1] = QCRYPTO_SIG_ALGO_RSA_SHA1,
    [GNUTLS_SIGN_RSA_SHA] = QCRYPTO_SIG_ALGO_RSA_SHA1,
    [GNUTLS_SIGN_DSA_SHA1] = QCRYPTO_SIG_ALGO_DSA_SHA1,
    [GNUTLS_SIGN_RSA_MD5] = QCRYPTO_SIG_ALGO_RSA_MD5,
    [GNUTLS_SIGN_RSA_MD2] = QCRYPTO_SIG_ALGO_RSA_MD2,
    [GNUTLS_SIGN_RSA_RMD160] = QCRYPTO_SIG_ALGO_RSA_RMD160,
    [GNUTLS_SIGN_RSA_SHA256] = QCRYPTO_SIG_ALGO_RSA_SHA256,
    [GNUTLS_SIGN_RSA_SHA384] = QCRYPTO_SIG_ALGO_RSA_SHA384,
    [GNUTLS_SIGN_RSA_SHA512] = QCRYPTO_SIG_ALGO_RSA_SHA512,
    [GNUTLS_SIGN_RSA_SHA224] = QCRYPTO_SIG_ALGO_RSA_SHA224,
    [GNUTLS_SIGN_DSA_SHA224] = QCRYPTO_SIG_ALGO_DSA_SHA224,
    [GNUTLS_SIGN_DSA_SHA256] = QCRYPTO_SIG_ALGO_DSA_SHA256,
    [GNUTLS_SIGN_ECDSA_SHA1] = QCRYPTO_SIG_ALGO_ECDSA_SHA1,
    [GNUTLS_SIGN_ECDSA_SHA224] = QCRYPTO_SIG_ALGO_ECDSA_SHA224,
    [GNUTLS_SIGN_ECDSA_SHA256] = QCRYPTO_SIG_ALGO_ECDSA_SHA256,
    [GNUTLS_SIGN_ECDSA_SHA384] = QCRYPTO_SIG_ALGO_ECDSA_SHA384,
    [GNUTLS_SIGN_ECDSA_SHA512] = QCRYPTO_SIG_ALGO_ECDSA_SHA512,
};

static const int gnutls_to_qcrypto_pk_alg_map[] = {
    [GNUTLS_PK_UNKNOWN] = QCRYPTO_PK_ALGO_UNKNOWN,
    [GNUTLS_PK_RSA] = QCRYPTO_PK_ALGO_RSA,
    [GNUTLS_PK_DSA] = QCRYPTO_PK_ALGO_DSA,
    [GNUTLS_PK_DH] = QCRYPTO_PK_ALGO_DH,
    [GNUTLS_PK_ECDSA] = QCRYPTO_PK_ALGO_ECDSA,
};

int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result, size_t *resultlen,
                                  Error **errp)
{
    int ret = -1;
    int rc;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    *result = g_malloc0(*resultlen);
    rc = gnutls_x509_crt_export(crt, GNUTLS_X509_FMT_DER, *result, resultlen);
    if (rc != 0) {
        error_setg(errp, "Failed to convert certificate to DER format: %s",
                   gnutls_strerror(rc));
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_keyid_len(QCryptoKeyidFlags flag, Error **errp)
{
    if (flag >= G_N_ELEMENTS(qcrypto_to_gnutls_keyid_flags_map)) {
        error_setg(errp, "Unknow key ID flag %d", flag);
        return -1;
    }

    if ((flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_SHA512]) ||
        (flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_BEST_KNOWN])) {
        return QCRYPTO_HASH_DIGEST_LEN_SHA512;
    } else if (flag & qcrypto_to_gnutls_keyid_flags_map[QCRYPTO_KEYID_FLAGS_SHA256]) {
        return QCRYPTO_HASH_DIGEST_LEN_SHA256;
    } else {
        return QCRYPTO_HASH_DIGEST_LEN_SHA1;
    }
}

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo alg,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    int ret = -1;
    int hlen;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    if (alg >= G_N_ELEMENTS(qcrypto_to_gnutls_hash_alg_map)) {
        error_setg(errp, "Unknown hash algorithm");
        return -1;
    }

    if (result == NULL) {
        error_setg(errp, "No valid buffer given");
        return -1;
    }

    gnutls_x509_crt_init(&crt);

    if (gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM) != 0) {
        error_setg(errp, "Failed to import certificate");
        goto cleanup;
    }

    hlen = gnutls_hash_get_len(qcrypto_to_gnutls_hash_alg_map[alg]);
    if (*resultlen < hlen) {
        error_setg(errp,
                   "Result buffer size %zu is smaller than hash %d",
                   *resultlen, hlen);
        goto cleanup;
    }

    if (gnutls_x509_crt_get_fingerprint(crt,
                                        qcrypto_to_gnutls_hash_alg_map[alg],
                                        result, resultlen) != 0) {
        error_setg(errp, "Failed to get fingerprint from certificate");
        goto cleanup;
    }

    ret = 0;

 cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_signature_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    /*
     * This function never returns a negative error code.
     * Error cases and unknown/unsupported signature algorithms
     * are mapped to GNUTLS_SIGN_UNKNOWN.
     */
    rc = gnutls_x509_crt_get_signature_algorithm(crt);
    if (rc >= G_N_ELEMENTS(gnutls_to_qcrypto_sig_alg_map)) {
        error_setg(errp, "Unknown signature algorithm %d", rc);
        goto cleanup;
    }

    ret = gnutls_to_qcrypto_sig_alg_map[rc];

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_cert_version(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_version(crt);
    if (rc < 0) {
        error_setg(errp, "Failed to get certificate version: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    ret = rc;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};
    time_t now = time(0);
    time_t exp_time;
    time_t act_time;

    if (now == ((time_t)-1)) {
        error_setg_errno(errp, errno, "Cannot get current time");
        return ret;
    }

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    exp_time = gnutls_x509_crt_get_expiration_time(crt);
    if (exp_time == ((time_t)-1)) {
        error_setg(errp, "Failed to get certificate expiration time");
        goto cleanup;
    }
    if (exp_time < now) {
        error_setg(errp, "The certificate has expired");
        goto cleanup;
    }

    act_time = gnutls_x509_crt_get_activation_time(crt);
    if (act_time == ((time_t)-1)) {
        error_setg(errp, "Failed to get certificate activation time");
        goto cleanup;
    }
    if (act_time > now) {
        error_setg(errp, "The certificate is not yet active");
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_pk_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    int rc;
    int ret = -1;
    unsigned int bits;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_x509_crt_get_pk_algorithm(crt, &bits);
    if (rc >= G_N_ELEMENTS(gnutls_to_qcrypto_pk_alg_map)) {
        error_setg(errp, "Unknown public key algorithm %d", rc);
        goto cleanup;
    }

    ret = gnutls_to_qcrypto_pk_alg_map[rc];

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_datum_t datum = {.data = cert, .size = size};

    *resultlen = qcrypto_x509_get_keyid_len(qcrypto_to_gnutls_keyid_flags_map[flag],
                                            errp);
    if (*resultlen == -1) {
        return ret;
    }

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    *result = g_malloc0(*resultlen);
    if (gnutls_x509_crt_get_key_id(crt,
                                   qcrypto_to_gnutls_keyid_flags_map[flag],
                                   *result, resultlen) != 0) {
        error_setg(errp, "Failed to get key ID from certificate");
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    return ret;
}

int qcrypto_pkcs7_convert_sig_pem(uint8_t *sig, size_t sig_size,
                                  uint8_t **result, size_t *resultlen,
                                  Error **errp)
{
    int ret = -1;
    int rc;
    gnutls_pkcs7_t signature;
    gnutls_datum_t sig_datum_der = {.data = sig, .size = sig_size};
    gnutls_datum_t sig_datum_pem = { 0, };

    rc = gnutls_pkcs7_init(&signature);
    if (rc < 0) {
        error_setg(errp, "Failed to initalize pkcs7 data: %s", gnutls_strerror(rc));
        return ret;
     }

    rc = gnutls_pkcs7_import(signature, &sig_datum_der, GNUTLS_X509_FMT_DER);
    if (rc != 0) {
        error_setg(errp, "Failed to import signature: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_pkcs7_export2(signature, GNUTLS_X509_FMT_PEM, &sig_datum_pem);
    if (rc != 0) {
        error_setg(errp, "Failed to convert signature to PEM format: %s",
                   gnutls_strerror(rc));
        gnutls_free(sig_datum_pem.data);
        goto cleanup;
    }

    *result = g_steal_pointer(&sig_datum_pem.data);
    *resultlen = sig_datum_pem.size;

    ret = 0;

cleanup:
    gnutls_pkcs7_deinit(signature);
    return ret;
}

int qcrypto_x509_verify_sig(uint8_t *cert, size_t cert_size,
                            uint8_t *comp, size_t comp_size,
                            uint8_t *sig, size_t sig_size, Error **errp)
{
    int rc;
    int ret = -1;
    gnutls_x509_crt_t crt;
    gnutls_pkcs7_t signature;
    gnutls_datum_t cert_datum = {.data = cert, .size = cert_size};
    gnutls_datum_t data_datum = {.data = comp, .size = comp_size};
    gnutls_datum_t sig_datum = {.data = sig, .size = sig_size};

    rc = gnutls_x509_crt_init(&crt);
    if (rc < 0) {
        error_setg(errp, "Failed to initialize certificate: %s", gnutls_strerror(rc));
        return ret;
    }

    rc = gnutls_x509_crt_import(crt, &cert_datum, GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import certificate: %s", gnutls_strerror(rc));
        gnutls_x509_crt_deinit(crt);
        return ret;
    }

    rc = gnutls_pkcs7_init(&signature);
    if (rc < 0) {
        error_setg(errp, "Failed to initalize pkcs7 data: %s", gnutls_strerror(rc));
        gnutls_x509_crt_deinit(crt);
        return ret;
     }

    rc = gnutls_pkcs7_import(signature, &sig_datum , GNUTLS_X509_FMT_PEM);
    if (rc != 0) {
        error_setg(errp, "Failed to import signature: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    rc = gnutls_pkcs7_verify_direct(signature, crt, 0, &data_datum, 0);
    if (rc != 0) {
        error_setg(errp, "Failed to verify signature: %s", gnutls_strerror(rc));
        goto cleanup;
    }

    ret = 0;

cleanup:
    gnutls_x509_crt_deinit(crt);
    gnutls_pkcs7_deinit(signature);
    return ret;
}

#else /* ! CONFIG_GNUTLS */

int qcrypto_x509_convert_cert_der(uint8_t *cert, size_t size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp)
{
    error_setg(errp, "GNUTLS is required to export X.509 certificate");
    return -1;
}

int qcrypto_x509_get_keyid_len(QCryptoKeyidFlags flag, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get key ID length");
    return -1;
}

int qcrypto_get_x509_cert_fingerprint(uint8_t *cert, size_t size,
                                      QCryptoHashAlgo hash,
                                      uint8_t *result,
                                      size_t *resultlen,
                                      Error **errp)
{
    error_setg(errp, "GNUTLS is required to get fingerprint");
    return -1;
}

int qcrypto_x509_get_signature_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get signature algorithm");
    return -1;
}

int qcrypto_x509_get_cert_version(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get certificate version");
    return -1;
}

int qcrypto_x509_check_cert_times(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get certificate times");
    return -1;
}

int qcrypto_x509_get_pk_algorithm(uint8_t *cert, size_t size, Error **errp)
{
    error_setg(errp, "GNUTLS is required to get public key algorithm");
    return -1;
}

int qcrypto_x509_get_cert_key_id(uint8_t *cert, size_t size,
                                 QCryptoKeyidFlags flag,
                                 uint8_t **result,
                                 size_t *resultlen,
                                 Error **errp)
{
    error_setg(errp, "GNUTLS is required to get key ID");
    return -1;
}

int qcrypto_pkcs7_convert_sig_pem(uint8_t *sig, size_t sig_size,
                                  uint8_t **result,
                                  size_t *resultlen,
                                  Error **errp)
{
    error_setg(errp, "GNUTLS is required to export pkcs7 signature");
    return -1;
}

int qcrypto_x509_verify_sig(uint8_t *cert, size_t cert_size,
                            uint8_t *comp, size_t comp_size,
                            uint8_t *sig, size_t sig_size, Error **errp)
{
    error_setg(errp, "GNUTLS is required for signature-verification support");
    return -1;
}

#endif /* ! CONFIG_GNUTLS */
