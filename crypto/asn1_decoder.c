/*
 * QEMU Crypto ASN.1 decoder
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

#include <stdint.h>
#include <stddef.h>

#include "crypto/asn1_decoder.h"

enum der_type_tag {
    der_type_tag_bool = 0x1,
    der_type_tag_int = 0x2,
    der_type_tag_bit_str = 0x3,
    der_type_tag_oct_str = 0x4,
    der_type_tag_oct_null = 0x5,
    der_type_tag_oct_oid = 0x6,
    der_type_tag_seq = 0x10,
    der_type_tag_set = 0x11,
};

#define DER_CONSTRUCTED_MASK 0x20
#define DER_SHORT_LEN_MASK 0x80

static uint8_t der_peek_byte(const uint8_t **data, size_t *dlen)
{
    return **data;
}

static void der_cut_nbytes(const uint8_t **data, size_t *dlen,
                           size_t nbytes)
{
    *data += nbytes;
    *dlen -= nbytes;
}

static uint8_t der_cut_byte(const uint8_t **data, size_t *dlen)
{
    uint8_t val = der_peek_byte(data, dlen);

    der_cut_nbytes(data, dlen, 1);

    return val;
}

static int der_invoke_callback(DERDecodeCb cb, void *ctx,
                           const uint8_t *value, size_t vlen)
{
    if (!cb) {
        return 0;
    }

    return cb(ctx, value, vlen);
}

static int der_extract_definite_data(const uint8_t **data, size_t *dlen,
                                     DERDecodeCb cb, void *ctx)
{
    const uint8_t *value;
    size_t vlen = 0;
    uint8_t byte_count = der_cut_byte(data, dlen);

    /* short format of definite-length */
    if (!(byte_count & DER_SHORT_LEN_MASK)) {
        if (byte_count > *dlen) {
            return -1;
        }

        value = *data;
        vlen = byte_count;
        der_cut_nbytes(data, dlen, vlen);

        if (der_invoke_callback(cb, ctx, value, vlen)) {
            return -1;
        }
        return vlen;
    }

    /* Ignore highest bit */
    byte_count &= ~DER_SHORT_LEN_MASK;

    /*
     * size_t is enough to express the length, although the der encoding
     * standard supports larger length.
     */
    if (byte_count > sizeof(size_t)) {
        return -1;
    }

    while (byte_count--) {
        vlen <<= 8;
        vlen += der_cut_byte(data, dlen);
    }

    if (vlen > *dlen) {
        return -1;
    }

    value = *data;
    der_cut_nbytes(data, dlen, vlen);

    if (der_invoke_callback(cb, ctx, value, vlen) != 0) {
        return -1;
    }
    return vlen;
}

static int der_extract_data(const uint8_t **data, size_t *dlen,
                            DERDecodeCb cb, void *ctx)
{
    uint8_t val = der_peek_byte(data, dlen);

    /* must use definite length format */
    if (val == DER_SHORT_LEN_MASK) {
        return -1;
    }

    return der_extract_definite_data(data, dlen, cb, ctx);
}

int der_decode_int(const uint8_t **data, size_t *dlen,
                   DERDecodeCb cb, void *ctx)
{
    uint8_t tag = der_cut_byte(data, dlen);

    /* INTEGER must encoded in primitive-form */
    if (tag != der_type_tag_int) {
        return -1;
    }

    return der_extract_data(data, dlen, cb, ctx);
}

int der_decode_seq(const uint8_t **data, size_t *dlen,
                   DERDecodeCb cb, void *ctx)
{
    uint8_t val = der_cut_byte(data, dlen);

    /* SEQUENCE must use constructed form */
    if (val != (der_type_tag_seq | DER_CONSTRUCTED_MASK)) {
        return -1;
    }

    return der_extract_data(data, dlen, cb, ctx);
}
