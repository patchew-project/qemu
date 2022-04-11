/*
 * QEMU Crypto RSA key parser
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

#ifndef QCRYPTO_RSAKEY_H
#define QCRYPTO_RSAKEY_H

#include <nettle/bignum.h>

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "crypto/akcipher.h"

typedef struct QCryptoAkCipherRSAKey QCryptoAkCipherRSAKey;
typedef struct QCryptoAkCipherMpi QCryptoAkCipherMpi;

/**
 * Multiple precious integer, encoded as two' complement,
 * copied directly from DER encoded ASN.1 structures.
 */
struct QCryptoAkCipherMpi {
    uint8_t *data;
    size_t len;
};

/* See rfc2437: https://datatracker.ietf.org/doc/html/rfc2437 */
struct QCryptoAkCipherRSAKey {
    /* The modulus */
    QCryptoAkCipherMpi n;
    /* The public exponent */
    QCryptoAkCipherMpi e;
    /* The private exponent */
    QCryptoAkCipherMpi d;
    /* The first factor */
    QCryptoAkCipherMpi p;
    /* The second factor */
    QCryptoAkCipherMpi q;
    /* The first factor's exponent */
    QCryptoAkCipherMpi dp;
    /* The second factor's exponent */
    QCryptoAkCipherMpi dq;
    /* The CRT coefficient */
    QCryptoAkCipherMpi u;
};

/**
 * Parse DER encoded ASN.1 RSA private key, expected ASN.1 schema:
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
 *         }
 *
 * Return: On success QCryptoAkCipherRSAKey is returned, otherwise returns NULL
 */
QCryptoAkCipherRSAKey *qcrypto_akcipher_parse_rsa_private_key(
    const uint8_t *key, size_t keylen);

/**
 * Parse DER encoded ASN.1 RSA public key, expected ASN.1 schema:
 *        RsaPubKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 *
 * Return: On success QCryptoAkCipherRSAKey is returned, otherwise returns NULL
 */
QCryptoAkCipherRSAKey *qcrypto_akcipher_parse_rsa_public_key(
    const uint8_t *key, size_t keylen);

void qcrypto_akcipher_free_rsa_key(QCryptoAkCipherRSAKey *key);

#endif
