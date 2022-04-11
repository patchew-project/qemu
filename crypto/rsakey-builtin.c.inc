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

#include "rsakey.h"
#include "asn1_decoder.h"

static int extract_mpi(void *ctx, const uint8_t *value, size_t vlen)
{
    QCryptoAkCipherMpi *mpi = (QCryptoAkCipherMpi *)ctx;
    mpi->data = g_memdup2(value, vlen);
    mpi->len = vlen;
    return 0;
}

static int extract_version(void *ctx, const uint8_t *value, size_t vlen)
{
    uint8_t *version = (uint8_t *)ctx;
    if (vlen != 1) {
        return -1;
    }
    if (*value > 1) {
        return -1;
    }
    *version = *value;
    return 0;
}

static int extract_content(void *ctx, const uint8_t *value, size_t vlen)
{
    const uint8_t **content = (const uint8_t **)ctx;
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
QCryptoAkCipherRSAKey *qcrypto_akcipher_parse_rsa_public_key(const uint8_t *key,
                                                             size_t keylen)
{
    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    const uint8_t *seq;
    size_t seq_length;
    int decode_ret;

    decode_ret = der_decode_seq(&key, &keylen, extract_content, &seq);
    if (decode_ret <= 0 || keylen != 0) {
        goto err;
    }
    seq_length = decode_ret;

    if (der_decode_int(&seq, &seq_length, extract_mpi, &rsa->n) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->e) <= 0 ||
        seq_length != 0) {
        goto err;
    }

    return rsa;

err:
    qcrypto_akcipher_free_rsa_key(rsa);
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
QCryptoAkCipherRSAKey *qcrypto_akcipher_parse_rsa_private_key(
    const uint8_t *key, size_t keylen)
{
    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    uint8_t version;
    const uint8_t *seq;
    int decode_ret;
    size_t seq_length;

    decode_ret = der_decode_seq(&key, &keylen, extract_content, &seq);
    if (decode_ret <= 0 || keylen != 0) {
        goto err;
    }
    seq_length = decode_ret;

    if (der_decode_int(&seq, &seq_length,
                       extract_version, &version) != sizeof(version) ||
        version > 1) {
        goto err;
    }
    if (der_decode_int(&seq, &seq_length, extract_mpi, &rsa->n) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->e) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->d) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->p) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->q) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->dp) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->dq) <= 0 ||
        der_decode_int(&seq, &seq_length, extract_mpi, &rsa->u) <= 0) {
        goto err;
    }
    /**
     * According to the standard, otherPrimeInfos must be present for version 1.
     * There is no strict verification here, this is to be compatible with
     * the unit test of the kernel. TODO: remove this until linux kernel's
     * unit-test is fixed.
     */
    if (version == 1 && seq_length != 0) {
        if (der_decode_seq(&seq, &seq_length, NULL, NULL) <= 0) {
            goto err;
        }
    }
    if (seq_length != 0) {
        goto err;
    }
    return rsa;

err:
    qcrypto_akcipher_free_rsa_key(rsa);
    return NULL;
}
