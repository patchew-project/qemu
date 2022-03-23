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

#include <stdint.h>
#include <stddef.h>

#include "crypto/asn1_decoder.h"

enum ber_type_tag {
    ber_type_tag_bool = 0x1,
    ber_type_tag_int = 0x2,
    ber_type_tag_bit_str = 0x3,
    ber_type_tag_oct_str = 0x4,
    ber_type_tag_oct_null = 0x5,
    ber_type_tag_oct_oid = 0x6,
    ber_type_tag_seq = 0x10,
    ber_type_tag_set = 0x11,
};

#define BER_CONSTRUCTED_MASK 0x20
#define BER_SHORT_LEN_MASK 0x80

static uint8_t ber_peek_byte(const uint8_t **data, size_t *dlen)
{
    return **data;
}

static int invoke_callback(BerDecodeCb cb, void *ctx,
                           const uint8_t *value, size_t vlen)
{
    if (!cb) {
        return 0;
    }

    return cb(ctx, value, vlen);
}

static void ber_cut_nbytes(const uint8_t **data, size_t *dlen,
                           size_t nbytes)
{
    *data += nbytes;
    *dlen -= nbytes;
}

static uint8_t ber_cut_byte(const uint8_t **data, size_t *dlen)
{
    uint8_t val = ber_peek_byte(data, dlen);

    ber_cut_nbytes(data, dlen, 1);

    return val;
}

static int ber_extract_definite_data(const uint8_t **data, size_t *dlen,
                                     BerDecodeCb cb, void *ctx)
{
    const uint8_t *value;
    size_t vlen = 0;
    uint8_t byte_count = ber_cut_byte(data, dlen);

    /* short format of definite-length */
    if (!(byte_count & BER_SHORT_LEN_MASK)) {
        if (byte_count > *dlen) {
            return -1;
        }

        value = *data;
        vlen = byte_count;
        ber_cut_nbytes(data, dlen, vlen);

        return invoke_callback(cb, ctx, value, vlen);
    }

    /* Ignore highest bit */
    byte_count &= ~BER_SHORT_LEN_MASK;

    /*
     * size_t is enough to express the length, although the ber encoding
     * standard supports larger length.
     */
    if (byte_count > sizeof(size_t)) {
        return -1;
    }

    while (byte_count--) {
        vlen <<= 8;
        vlen += ber_cut_byte(data, dlen);
    }

    if (vlen > *dlen) {
        return -1;
    }

    value = *data;
    ber_cut_nbytes(data, dlen, vlen);

    return invoke_callback(cb, ctx, value, vlen);
}

static int ber_extract_undefinite_data(const uint8_t **data, size_t *dlen,
                                       BerDecodeCb cb, void *ctx)
{
    size_t vlen = 0;
    const uint8_t *value;

    if (*dlen < 3) {
        return -1;
    }

    /* skip undefinite-length-mask 0x80 */
    ber_cut_nbytes(data, dlen, 1);

    value = *data;
    while (vlen < *dlen) {
        if ((*data)[vlen] != 0) {
            vlen++;
            continue;
        }

        if (vlen + 1 < *dlen && (*data[vlen + 1] == 0)) {
            ber_cut_nbytes(data, dlen, vlen + 2);
            return invoke_callback(cb, ctx, value, vlen);
        }

        vlen += 2;
    }

    return -1;
}

static int ber_extract_data(const uint8_t **data, size_t *dlen,
                            BerDecodeCb cb, void *ctx)
{
    uint8_t val = ber_peek_byte(data, dlen);

    if (val == BER_SHORT_LEN_MASK) {
        return ber_extract_undefinite_data(data, dlen, cb, ctx);
    }

    return ber_extract_definite_data(data, dlen, cb, ctx);
}

int ber_decode_int(const uint8_t **data, size_t *dlen,
                   BerDecodeCb cb, void *ctx)
{
    uint8_t tag = ber_cut_byte(data, dlen);

    /* INTEGER must encoded in primitive-form */
    if (tag != ber_type_tag_int) {
        return -1;
    }

    return ber_extract_data(data, dlen, cb, ctx);
}

int ber_decode_seq(const uint8_t **data, size_t *dlen,
                   BerDecodeCb cb, void *ctx)
{
    uint8_t val = ber_cut_byte(data, dlen);

    /* SEQUENCE must use constructed form */
    if (val != (ber_type_tag_seq | BER_CONSTRUCTED_MASK)) {
        return -1;
    }

    return ber_extract_data(data, dlen, cb, ctx);
}
