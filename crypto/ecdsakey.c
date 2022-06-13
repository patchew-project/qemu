/*
 * QEMU Crypto ECDSA key parser
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

#include "qemu/osdep.h"
#include "ecdsakey.h"
#include "der.h"

void qcrypto_akcipher_ecdsasig_free(QCryptoAkCipherECDSASig *sig)
{
    if (!sig) {
        return;
    }
    g_free(sig->r.data);
    g_free(sig->s.data);
    g_free(sig);
}

void qcrypto_akcipher_ecdsasig_x9_62_encode(QCryptoAkCipherECDSASig *sig,
    uint8_t *dst, size_t *dst_len)
{
    size_t r_len, s_len;
    uint8_t *r_dst, *s_dst;
    g_autofree uint8_t *buff = NULL;

    qcrypto_der_encode_int(NULL, sig->r.len, NULL, &r_len);
    qcrypto_der_encode_int(NULL, sig->s.len, NULL, &s_len);

    buff = g_new0(uint8_t, r_len + s_len);
    r_dst = buff;
    qcrypto_der_encode_int(sig->r.data, sig->r.len, r_dst, &r_len);
    s_dst = buff + r_len;
    qcrypto_der_encode_int(sig->s.data, sig->s.len, s_dst, &s_len);

    qcrypto_der_encode_seq(buff, r_len + s_len, dst, dst_len);
}

QCryptoAkCipherECDSASig *qcrypto_akcipher_ecdsasig_alloc(
    QCryptoCurveID curve_id, Error **errp)
{
    int keylen;
    QCryptoAkCipherECDSASig *sig;

    switch (curve_id) {
    case QCRYPTO_CURVE_ID_NIST_P192:
        keylen = 192 / 8;
        break;

    case QCRYPTO_CURVE_ID_NIST_P256:
        keylen = 256 / 8;
        break;

    case QCRYPTO_CURVE_ID_NIST_P384:
        keylen = 384 / 8;
        break;

    default:
        error_setg(errp, "Unknown curve id: %d", curve_id);
        return NULL;
    }

    /*
     * Note: when encoding positive bignum in tow'complement, we have to add
     * a leading zero if the most significant byte is greater than or
     * equal to 0x80.
     */
    sig = g_new0(QCryptoAkCipherECDSASig, 1);
    sig->r.data = g_new0(uint8_t, keylen + 1);
    sig->r.len = keylen + 1;
    sig->s.data = g_new0(uint8_t, keylen + 1);
    sig->s.len = keylen + 1;
    return sig;
}

size_t qcrypto_akcipher_ecdsasig_x9_62_size(size_t keylen)
{
    size_t integer_len;
    size_t seq_len;

    /*
     * Note: when encoding positive bignum in tow'complement, we have to add
     * a leading zero if the most significant byte is greater than or
     * equal to 0x80.
     */
    qcrypto_der_encode_int(NULL, keylen + 1, NULL, &integer_len);
    qcrypto_der_encode_seq(NULL, integer_len * 2, NULL, &seq_len);
    return seq_len;
}

void qcrypto_akcipher_ecdsakey_free(QCryptoAkCipherECDSAKey *ecdsa)
{
    if (!ecdsa) {
        return;
    }
    g_free(ecdsa->priv.data);
    g_free(ecdsa->pub_x.data);
    g_free(ecdsa->pub_y.data);
    g_free(ecdsa);
}

#include "ecdsakey-builtin.c.inc"
