/*
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

#include "qapi/error.h"

/* Simple decoder used to parse DER encoded rsa keys. */

/**
 *  @opaque: user context.
 *  @value: the starting address of |value| part of 'Tag-Length-Value' pattern.
 *  @vlen: length of the |value|.
 *  Returns: 0 for success, any other value is considered an error.
 */
typedef int (*QCryptoDERDecodeCb) (void *opaque, const uint8_t *value,
                                   size_t vlen, Error **errp);

/**
 * qcrypto_der_decode_int:
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Decode integer from DER-encoded data.
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded INTEGER will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_int(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);
/**
 * qcrypto_der_decode_seq:
 *
 * Decode sequence from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded SEQUENCE will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_seq(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

/**
 * qcrypto_der_decode_oid:
 *
 * Decode OID from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded OID will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_oid(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

/**
 * qcrypto_der_decode_octet_str:
 *
 * Decode OCTET STRING from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded OCTET STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_octet_str(const uint8_t **data,
                                 size_t *dlen,
                                 QCryptoDERDecodeCb cb,
                                 void *opaque,
                                 Error **errp);

/**
 * qcrypto_der_decode_bit_str:
 *
 * Decode BIT STRING from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded BIT STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_bit_str(const uint8_t **data,
                               size_t *dlen,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);


/**
 * qcrypto_der_decode_ctx_tag:
 *
 * Decode context specific tag
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @tag: expected value of context specific tag
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded BIT STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_ctx_tag(const uint8_t **data,
                               size_t *dlen, int tag_id,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);

/**
 * qcrypto_der_encode_seq:
 * @src: source data to be encoded
 * @src_len: length of source data
 * @dest: distination to save the encoded data, if dest is NULL, dst_len is
 * set to expected buffer length and nothing more happends.
 * @dst_len: output parameter, indicates how many bytes wat wrote to dest.
 *
 * Encode data as SEQUENCE in DER rules.
 *
 */
void qcrypto_der_encode_seq(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len);

/**
 * qcrypto_der_encode_int:
 * @src: source data to be encoded
 * @src_len: length of source data
 * @dest: distination to save the encoded data, if dest is NULL, dst_len is
 * set to expected buffer length and nothing more happends.
 * @dst_len: output parameter, indicates how many bytes wat wrote to dest.
 *
 * Encode data as INTEGER in DER rules, the source data MUST be already
 * encoded as two's complement in bigendian.
 *
 */
void qcrypto_der_encode_int(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len);

#endif  /* QCRYPTO_ASN1_DECODER_H */
