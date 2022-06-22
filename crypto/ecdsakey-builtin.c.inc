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
#include "ecdsakey.h"

#define QCRYPTO_ECDSA_PUBKEY_FMT_UNCOMPRESSED 0x04

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
        error_setg(errp, "Invalid ecdsa key version");
        return -1;
    }
    *version = *value;
    return 0;
}

static int extract_cons_content(void *ctx, const uint8_t *value,
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

static int qcrypto_akcipher_builtin_ecdsa_pubkey_parse2(
    QCryptoAkCipherECDSAKey *ecdsa,
    const uint8_t *key, size_t keylen, Error **errp);

static int extract_pubkey(void *ctx, const uint8_t *value,
                          size_t vlen, Error **errp)
{
    QCryptoAkCipherECDSAKey *ecdsa = (QCryptoAkCipherECDSAKey *)ctx;
    if (vlen < 4) {
        error_setg(errp, "Public key part too short");
        return -1;
    }
    /* Skip meta byte of BIT STRING */
    if (*value != 0) {
        error_setg(errp, "Invalid public key");
        return -1;
    }
    value++;
    vlen--;
    return qcrypto_akcipher_builtin_ecdsa_pubkey_parse2(
        ecdsa, value, vlen, errp);
}

/**
 *        ECDSASignature ::= SEQUENCE {
 *             r           INTEGER
 *             s           INTEGER
 *         }
 */
QCryptoAkCipherECDSASig *qcrypto_akcipher_ecdsasig_parse(
    const uint8_t *signature, size_t len, Error **errp)
{
    g_autoptr(QCryptoAkCipherECDSASig) sig = g_new0(QCryptoAkCipherECDSASig, 1);
    const uint8_t *seq;
    size_t seq_length;
    int decode_ret;

    decode_ret = qcrypto_der_decode_seq(&signature, &len,
                                        extract_cons_content, &seq, errp);

    if (decode_ret < 0) {
        return NULL;
    }
    if (len != 0) {
        error_setg(errp, "Invalid ECDSA signature");
        return NULL;
    }
    seq_length = decode_ret;

    if (qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &sig->r, errp) < 0 ||
        qcrypto_der_decode_int(&seq, &seq_length, extract_mpi,
                               &sig->s, errp) < 0) {
        return NULL;
    }
    if (seq_length != 0) {
        error_setg(errp, "Invalid ECDSA signature");
        return NULL;
    }

    return g_steal_pointer(&sig);
}

/**
 *   ECDSAPublicKey: compress-format || x coordinate || y coordinate
 */
static int qcrypto_akcipher_builtin_ecdsa_pubkey_parse2(
    QCryptoAkCipherECDSAKey *ecdsa,
    const uint8_t *key, size_t keylen, Error **errp)
{
    if (keylen < 3) {
        error_setg(errp, "keylen is too short: %zu", keylen);
        return -1;
    }
    if (key[0] != QCRYPTO_ECDSA_PUBKEY_FMT_UNCOMPRESSED) {
        error_setg(errp, "Only uncompressed ECDSA public key is supported");
        return -1;
    }

    /* Skip format byte */
    key++;
    keylen--;
    if (keylen % 2 != 0) {
        error_setg(errp, "ECDSA public key's length must be odd");
        return -1;
    }

    ecdsa->pub_x.data = g_memdup2(key, keylen / 2);
    ecdsa->pub_x.len = keylen / 2;
    ecdsa->pub_y.data = g_memdup2(key + keylen / 2, keylen / 2);
    ecdsa->pub_y.len = keylen / 2;

    return 0;
}

static QCryptoAkCipherECDSAKey *qcrypto_akcipher_builtin_ecdsa_pubkey_parse(
    const uint8_t *key, size_t keylen, Error **errp)
{
    g_autoptr(QCryptoAkCipherECDSAKey) ecdsa =
        g_new0(QCryptoAkCipherECDSAKey, 1);
    if (qcrypto_akcipher_builtin_ecdsa_pubkey_parse2(
        ecdsa, key, keylen, errp) != 0) {
        return NULL;
    }
    return g_steal_pointer(&ecdsa);
}

/**
 *     ECDSAPrivateKey ::= SEQUENCE {
 *          version         INTEGER
 *             k            OCTET STRING
 *          parameters [0]  OID           OPTIONAL
 *          publickey  [1]  BIT STRING    OPTIONAL
 *     }
 */
static QCryptoAkCipherECDSAKey *qcrypto_akcipher_builtin_ecdsa_privkey_parse(
    const uint8_t *key, size_t keylen, Error **errp)
{
    g_autoptr(QCryptoAkCipherECDSAKey) ecdsa =
        g_new0(QCryptoAkCipherECDSAKey, 1);
    uint8_t version;
    const uint8_t *seq, *pubkey;
    int decode_ret;
    size_t seq_length, pubkey_length;

    decode_ret = qcrypto_der_decode_seq(&key, &keylen, extract_cons_content,
                                        &seq, errp);
    if (decode_ret < 0) {
        return NULL;
    }
    if (keylen != 0) {
        error_setg(errp, "Invalid ECDSA private key");
        return NULL;
    }
    seq_length = decode_ret;

    if (qcrypto_der_decode_int(&seq, &seq_length, extract_version,
                               &version, errp) < 0 ||
        qcrypto_der_decode_octet_str(&seq, &seq_length, extract_mpi,
                                     &ecdsa->priv, errp) < 0) {
        return NULL;
    }

    /* Here we just ignore curve id */
    qcrypto_der_decode_ctx_tag(&seq, &seq_length, 0, NULL, NULL, NULL);

    decode_ret = qcrypto_der_decode_ctx_tag(&seq, &seq_length, 1,
                                            extract_cons_content,
                                            &pubkey, NULL);
    if (decode_ret > 0) {
        pubkey_length = decode_ret;
        if (qcrypto_der_decode_bit_str(&pubkey, &pubkey_length,
                                       extract_pubkey, ecdsa, errp) < 0) {
            return NULL;
        }
        if (pubkey_length != 0) {
            error_setg(errp, "Invalid ECDSA private key");
            return NULL;
        }
    }

    if (seq_length != 0) {
        error_setg(errp, "Invalid ECDSA private key");
        return NULL;
    }

    return g_steal_pointer(&ecdsa);
}

QCryptoAkCipherECDSAKey *qcrypto_akcipher_ecdsakey_parse(
    QCryptoAkCipherKeyType type,
    const uint8_t *key, size_t keylen, Error **errp)
{
    switch (type) {
    case QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE:
        return qcrypto_akcipher_builtin_ecdsa_privkey_parse(key, keylen, errp);

    case QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC:
        return qcrypto_akcipher_builtin_ecdsa_pubkey_parse(key, keylen, errp);

    default:
        error_setg(errp, "Unknown key type: %d", type);
        return NULL;
    }
}
