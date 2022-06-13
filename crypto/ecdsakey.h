/*
 * QEMU Crypto ECDSA signature parser
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

#ifndef QCRYPTO_ECDSASIG_H
#define QCRYPTO_ECDSASIG_H

#include "qemu/host-utils.h"
#include "crypto/akcipher.h"
#include "crypto/rsakey.h"

typedef struct QCryptoAkCipherECDSAKey QCryptoAkCipherECDSAKey;
typedef struct QCryptoAkCipherECDSASig QCryptoAkCipherECDSASig;

struct QCryptoAkCipherECDSASig {
    QCryptoAkCipherMPI r;
    QCryptoAkCipherMPI s;
};

struct QCryptoAkCipherECDSAKey {
    QCryptoAkCipherMPI priv;
    QCryptoAkCipherMPI pub_x;
    QCryptoAkCipherMPI pub_y;
};

QCryptoAkCipherECDSASig *qcrypto_akcipher_ecdsasig_parse(
    const uint8_t *sig, size_t len, Error **errp);

QCryptoAkCipherECDSASig *qcrypto_akcipher_ecdsasig_alloc(
    QCryptoCurveID curve_id, Error **errp);

void qcrypto_akcipher_ecdsasig_free(QCryptoAkCipherECDSASig *sig);

void qcrypto_akcipher_ecdsasig_x9_62_encode(
    QCryptoAkCipherECDSASig *sig, uint8_t *dst, size_t *dst_len);

size_t qcrypto_akcipher_ecdsasig_x9_62_size(size_t keylen);

QCryptoAkCipherECDSAKey *qcrypto_akcipher_ecdsakey_parse(
    QCryptoAkCipherKeyType type,
    const uint8_t *key, size_t keylen, Error **errp);

void qcrypto_akcipher_ecdsakey_free(QCryptoAkCipherECDSAKey *key);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoAkCipherECDSASig,
                              qcrypto_akcipher_ecdsasig_free);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoAkCipherECDSAKey,
                              qcrypto_akcipher_ecdsakey_free);
#endif
