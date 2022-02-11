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

#ifndef QCRYPTO_ASN1_DECODER_H
#define QCRYPTO_ASN1_DECODER_H

/*
 *  ctx: user content.
 *  value: the starting address of |value| part of 'Tag-Length-Value' pattern.
 *  vlen: length of the |value|.
 */
typedef int (*BerDecodeCb) (void *ctx, const uint8_t *value, size_t vlen);

int ber_decode_int(const uint8_t **data,
                   size_t *dlen,
                   BerDecodeCb cb,
                   void *ctx);

int ber_decode_seq(const uint8_t **data,
                   size_t *dlen,
                   BerDecodeCb cb,
                   void *ctx);

#endif  /* QCRYPTO_ASN1_DECODER_H */
