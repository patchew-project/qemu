/*
 * QEMU Crypto akcipher algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
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

#include "der.h"
#include "rsakey.h"

static int extract_mpi(void *ctx, const uint8_t *value,
                       size_t vlen, Error **errp)
{
    QCryptoAkCipherMPI *mpi = (QCryptoAkCipherMPI *)ctx;
    if (vlen == 0) {
        error_setg(errp, "Empty mpi field");
        return -1;
    }
    mpi->data = g_memdup2(value, vlen);
    mpi->len = vlen;
    return 0;
}

static int extract_version(void *ctx, const uint8_t *value,
                           size_t vlen, Error **errp)
{
    uint8_t *version = (uint8_t *)ctx;
    if (vlen != 1 || *value > 1) {
        error_setg(errp, "Invalid rsakey version");
        return -1;
    }
    *version = *value;
    return 0;
}

static int extract_seq_content(void *ctx, const uint8_t *value,
                               size_t vlen, Error **errp)
{
    const uint8_t **content = (const uint8_t **)ctx;
    if (vlen == 0) {
        error_setg(errp, "Empty sequence");
        return -1;
    }
    *content = value;
    return 0;
}

/**
 *
 *        RsaPubKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 */
static QCryptoAkCipherRSAKey *qcrypto_builtin_rsa_public_key_parse(
    const uint8_t *key, size_t keylen)
{
    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    const uint8_t *seq;
    size_t seq_length;
    int decode_ret;
    Error *local_error = NULL;

    decode_ret = qcrypto_der_decode_seq(&key, &keylen,
        extract_seq_content, &seq, &local_error);
    if (decode_ret < 0) {
        error_report_err(local_error);
        goto error;
    }
    if (keylen != 0) {
        goto error;
    }
    seq_length = decode_ret;

    if (qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &rsa->n, &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &rsa->e, &local_error) < 0) {
        error_report_err(local_error);
        goto error;
    }
    if (seq_length != 0) {
        goto error;
    }

    return rsa;

error:
    qcrypto_akcipher_rsakey_free(rsa);
    return NULL;
}

/**
 *        RsaPrivKey ::= SEQUENCE {
 *             version     INTEGER
 *             n           INTEGER
 *             e           INTEGER
 *             d           INTEGER
 *             p           INTEGER
 *             q           INTEGER
 *             dp          INTEGER
 *             dq          INTEGER
 *             u           INTEGER
 *       otherPrimeInfos   OtherPrimeInfos OPTIONAL
 *         }
 */
static QCryptoAkCipherRSAKey *qcrypto_builtin_rsa_private_key_parse(
    const uint8_t *key, size_t keylen)
{
    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    uint8_t version;
    const uint8_t *seq;
    int decode_ret;
    size_t seq_length;
    Error *local_error = NULL;

    decode_ret = qcrypto_der_decode_seq(&key, &keylen,
        extract_seq_content, &seq, &local_error);
    if (decode_ret < 0) {
        error_report_err(local_error);
        goto error;
    }
    if (keylen != 0) {
        goto error;
    }
    seq_length = decode_ret;

    decode_ret = qcrypto_der_decode_int(&seq, &seq_length, extract_version,
                                        &version, &local_error);
    if (decode_ret < 0) {
        error_report_err(local_error);
        goto error;
    }

    if (qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &rsa->n, &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &rsa->e, &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &rsa->d, &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi, &rsa->p,
                               &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi, &rsa->q,
                               &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi, &rsa->dp,
                               &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi, &rsa->dq,
                               &local_error) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi, &rsa->u,
                               &local_error) < 0) {
        error_report_err(local_error);
        goto error;
    }
    /**
     * According to the standard, otherPrimeInfos must be present for version 1.
     * There is no strict verification here, this is to be compatible with
     * the unit test of the kernel. TODO: remove this until linux kernel's
     * unit-test is fixed.
     */
    if (version == 1 && seq_length != 0) {
        if (qcrypto_der_decode_seq(&seq, &seq_length,
                                   NULL, NULL, &local_error) < 0) {
            error_report_err(local_error);
            goto error;
        }
        if (seq_length != 0) {
            goto error;
        }
        return rsa;
    }
    if (seq_length != 0) {
        goto error;
    }

    return rsa;

error:
    qcrypto_akcipher_rsakey_free(rsa);
    return NULL;
}

QCryptoAkCipherRSAKey *qcrypto_akcipher_rsakey_parse(
    QCryptoAkCipherKeyType type, const uint8_t *key, size_t keylen)
{
    switch (type) {
    case QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE:
        return qcrypto_builtin_rsa_private_key_parse(key, keylen);

    case QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC:
        return qcrypto_builtin_rsa_public_key_parse(key, keylen);

    default:
        return NULL;
    }
}
