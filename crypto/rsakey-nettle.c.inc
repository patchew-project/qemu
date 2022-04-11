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

#include <nettle/asn1.h>
#include <stdbool.h>

#include "rsakey.h"

static bool DumpMpi(struct asn1_der_iterator *i, QCryptoAkCipherMpi *mpi)
{
    mpi->data = g_memdup2(i->data, i->length);
    mpi->len = i->length;
    return true;
}

static bool GetMpi(struct asn1_der_iterator *i, QCryptoAkCipherMpi *mpi)
{
    if (asn1_der_iterator_next(i) != ASN1_ITERATOR_PRIMITIVE ||
        i->type != ASN1_INTEGER) {
        return false;
    }
    return DumpMpi(i, mpi);
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
    struct asn1_der_iterator i;
    uint32_t version;
    int tag;

    /* Parse entire struct */
    if (asn1_der_iterator_first(&i, keylen, key) != ASN1_ITERATOR_CONSTRUCTED
        || i.type != ASN1_SEQUENCE
        || asn1_der_decode_constructed_last(&i) != ASN1_ITERATOR_PRIMITIVE
        || i.type != ASN1_INTEGER
        || !asn1_der_get_uint32(&i, &version)
        || version > 1
        || !GetMpi(&i, &rsa->n)
        || !GetMpi(&i, &rsa->e)
        || !GetMpi(&i, &rsa->d)
        || !GetMpi(&i, &rsa->p)
        || !GetMpi(&i, &rsa->q)
        || !GetMpi(&i, &rsa->dp)
        || !GetMpi(&i, &rsa->dq)
        || !GetMpi(&i, &rsa->u)) {
        goto err;
    }

    if (version == 1) {
        tag = asn1_der_iterator_next(&i);
        /**
         * According to the standard otherPrimeInfos must be present for
         * version 1. There is no strict verification here, this is to be
         * compatible with the unit test of the kernel. TODO: remove this
         * until linux-kernel's unit-test is fixed;
         */
        if (tag == ASN1_ITERATOR_END) {
            return rsa;
        }
        if (tag != ASN1_ITERATOR_CONSTRUCTED ||
            i.type != ASN1_SEQUENCE) {
                goto err;
        }
    }

    if (asn1_der_iterator_next(&i) != ASN1_ITERATOR_END) {
        goto err;
    }

    return rsa;

    /* fallthrough to err */
err:
    qcrypto_akcipher_free_rsa_key(rsa);
    return NULL;
}

/**
 *
 *        RsaPubKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 */
QCryptoAkCipherRSAKey *qcrypto_akcipher_parse_rsa_public_key(
    const uint8_t *key, size_t keylen)
{

    QCryptoAkCipherRSAKey *rsa = g_new0(QCryptoAkCipherRSAKey, 1);
    struct asn1_der_iterator i;

    if (asn1_der_iterator_first(&i, keylen, key) != ASN1_ITERATOR_CONSTRUCTED
        || i.type != ASN1_SEQUENCE
        || asn1_der_decode_constructed_last(&i) != ASN1_ITERATOR_PRIMITIVE
        || !DumpMpi(&i, &rsa->n)
        || !GetMpi(&i, &rsa->e)
        || asn1_der_iterator_next(&i) != ASN1_ITERATOR_END) {
        goto err;
    }

    return rsa;

err:
    qcrypto_akcipher_free_rsa_key(rsa);
    return NULL;
}
