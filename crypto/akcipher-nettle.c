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

#include <stdbool.h>

#include <nettle/rsa.h>

#include "asn1_decoder.h"
#include "crypto/akcipher.h"
#include "crypto/random.h"
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/cryptodev.h"
#include "standard-headers/linux/virtio_crypto.h"

typedef struct QCryptoNettleRsa {
    QCryptoAkcipher akcipher;
    struct rsa_public_key pub;
    struct rsa_private_key priv;
    int padding_algo;
    int hash_algo;
} QCryptoNettleRsa;

struct asn1_parse_ctx {
    const uint8_t *data;
    size_t dlen;
};

static int extract_value(void *p, const uint8_t *data, size_t dlen)
{
    struct asn1_parse_ctx *ctx = (struct asn1_parse_ctx *)p;
    ctx->data = (uint8_t *)data;
    ctx->dlen = dlen;

    return 0;
}

static int extract_mpi(void *p, const uint8_t *data, size_t dlen)
{
    mpz_t *target = (mpz_t *)p;
    nettle_mpz_set_str_256_u(*target, dlen, data);

    return 0;
}

static QCryptoNettleRsa *qcrypto_nettle_rsa_malloc(void);

static void qcrypto_nettle_rsa_destroy(void *ptr)
{
    QCryptoNettleRsa *rsa = (QCryptoNettleRsa *)ptr;
    if (!rsa) {
        return;
    }

    rsa_public_key_clear(&rsa->pub);
    rsa_private_key_clear(&rsa->priv);
    g_free(rsa);
}

static QCryptoAkcipher *qcrypto_nettle_new_rsa(bool private, const uint8_t *key,
                                               size_t keylen, void *para,
                                               int index,  Error **errp);

QCryptoAkcipher *qcrypto_akcipher_nettle_new(uint32_t alg, bool private,
                                             const uint8_t *key,
                                             size_t keylen, void *para,
                                             int index, Error **errp);

QCryptoAkcipher *qcrypto_akcipher_nettle_new(uint32_t alg, bool private,
                                             const uint8_t *key,
                                             size_t keylen, void *para,
                                             int index, Error **errp)
{
    switch (alg) {
    case VIRTIO_CRYPTO_AKCIPHER_RSA:
        return qcrypto_nettle_new_rsa(private, key, keylen, para, index, errp);
    default:
        error_setg(errp, "Unsupported algorithm: %u", alg);
        return NULL;
    }

    return NULL;
}

/*
 * Parse ber encoded rsa private key, asn1 schema:
 *        RsaPrivKey ::= SEQUENCE {
 *             version     INTEGER
 *             n           INTEGER
 *             e           INTEGER
 *             d           INTEGER
 *             p           INTEGER
 *             q           INTEGER
 *             e1          INTEGER
 *             e2          INTEGER
 *             u           INTEGER
 *         }
 */
static int parse_rsa_private_key(QCryptoNettleRsa *rsa,
                                 const uint8_t *key, size_t keylen)
{
    struct asn1_parse_ctx ctx;

    if (ber_decode_seq(&key, &keylen, extract_value, &ctx) != 0 ||
        keylen != 0) {
        return -1;
    }

    if (ber_decode_int(&ctx.data, &ctx.dlen, NULL, NULL) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->pub.n) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->pub.e) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.d) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.p) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.q) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.a) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.b) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->priv.c) != 0 ||
        ctx.dlen != 0) {
        return -1;
    }

    if (!rsa_public_key_prepare(&rsa->pub)) {
        return -1;
    }

    /*
     * Since in the kernel's unit test, the p, q, a, b, c of some
     * private keys is 0, only the simplest length check is done here
     */
    rsa->priv.size = rsa->pub.size;

    return 0;
}

/*
 * Parse ber encoded rsa pubkey, asn1 schema:
 *        RsaPrivKey ::= SEQUENCE {
 *             n           INTEGER
 *             e           INTEGER
 *         }
 */
static int parse_rsa_public_key(QCryptoNettleRsa *rsa,
                                const uint8_t *key,
                                size_t keylen)
{
    struct asn1_parse_ctx ctx;

    if (ber_decode_seq(&key, &keylen, extract_value, &ctx) != 0 ||
        keylen != 0) {
        return -1;
    }

    if (ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->pub.n) != 0 ||
        ber_decode_int(&ctx.data, &ctx.dlen, extract_mpi, &rsa->pub.e) != 0 ||
        ctx.dlen != 0) {
        return -1;
    }

    if (!rsa_public_key_prepare(&rsa->pub)) {
        return -1;
    }

    return 0;
}

static QCryptoAkcipher *qcrypto_nettle_new_rsa(bool private, const uint8_t *key,
                                               size_t keylen, void *para,
                                               int index, Error **errp)
{
    QCryptoNettleRsa *rsa = qcrypto_nettle_rsa_malloc();
    CryptoDevBackendRsaPara *p = (CryptoDevBackendRsaPara *)para;
    rsa->padding_algo = p->padding_algo;
    rsa->hash_algo = p->hash_algo;

    if (private && parse_rsa_private_key(rsa, key, keylen) == 0) {
        return (QCryptoAkcipher *)rsa;
    } else if (!private && parse_rsa_public_key(rsa, key, keylen) == 0) {
        return (QCryptoAkcipher *)rsa;
    }

    qcrypto_nettle_rsa_destroy(rsa);
    error_setg(errp, "Failed to parse %s key", private ? "private" : "public");

    return NULL;
}


/*
 * nettle does not provide RSA interfaces without padding,
 * here we implemented rsa algorithm with nettle/mpz.
 */
static int _rsa_enc_raw(QCryptoNettleRsa *rsa, const void *data,
                        size_t data_len, void *enc,
                        size_t enc_len, Error **errp)
{
    mpz_t m;

    nettle_mpz_init_set_str_256_u(m, data_len, data);
    /* (1) Validate 0 <= m < n */
    if (mpz_cmp_ui(m, 0) < 0 || mpz_cmp(m, rsa->pub.n) >= 0) {
        error_setg(errp, "Failed to validate input data");
        return -VIRTIO_CRYPTO_BADMSG;
    }

    /* (2) c = m ^ e mod n */
    mpz_powm(m, m, rsa->pub.e, rsa->pub.n);
    nettle_mpz_get_str_256(enc_len, (uint8_t *)enc, m);

    mpz_clear(m);

    return 0;
}

static int _rsa_dec_raw(QCryptoNettleRsa *rsa,
                        const void *enc,
                        size_t enc_len,
                        void *data,
                        size_t data_len,
                        Error **errp)
{
    mpz_t c;
    nettle_mpz_init_set_str_256_u(c, enc_len, enc);

    /* (1) Validate 0 <= c < n */
    if (mpz_cmp_ui(c, 0) < 0 || mpz_cmp(c, rsa->pub.n) >= 0) {
        error_setg(errp, "Failed to validate input data");
        return -VIRTIO_CRYPTO_BADMSG;
    }

    /* (2) m = c ^ d mod n */
    mpz_powm(c, c, rsa->priv.d, rsa->pub.n);
    nettle_mpz_get_str_256(data_len, (uint8_t *)data, c);
    mpz_clear(c);

    return 0;
}

static void wrap_nettle_random_func(void *ctx, size_t len, uint8_t *out)
{
    /* TODO: check result */
    qcrypto_random_bytes(out, len, NULL);
}

static int qcrypto_nettle_rsa_encrypt(QCryptoAkcipher *akcipher_driver,
                                      const void *data, size_t data_len,
                                      void *enc, size_t enc_len,
                                      Error **errp)
{

    QCryptoNettleRsa *rsa =
        container_of(akcipher_driver, QCryptoNettleRsa, akcipher);
    mpz_t c;
    int enc_ret;

    if (data_len > rsa->pub.size || enc_len < rsa->pub.size) {
        error_setg(errp, "Invalid buffer size");
        return -VIRTIO_CRYPTO_BADMSG;
    }

    switch (rsa->padding_algo) {
    case VIRTIO_CRYPTO_RSA_RAW_PADDING:
        return _rsa_enc_raw(rsa, data, data_len, enc, enc_len, errp);

    case VIRTIO_CRYPTO_RSA_PKCS1_PADDING:
        mpz_init(c);
        enc_ret = rsa_encrypt(&rsa->pub, NULL, wrap_nettle_random_func,
                              data_len, (uint8_t *)data, c);
        if (enc_ret != 1) {
            error_setg(errp, "Failed to encrypt");
        } else {
            nettle_mpz_get_str_256(enc_len, (uint8_t *)enc, c);
        }
        mpz_clear(c);
        return enc_ret == 1 ? 0 : -1;

    default:
        error_setg(errp, "Unknown padding");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    return -1;
}

static int qcrypto_nettle_rsa_decrypt(QCryptoAkcipher *akcipher,
                                      const void *enc, size_t enc_len,
                                      void *data, size_t data_len,
                                      Error **errp)
{
    QCryptoNettleRsa *rsa = container_of(akcipher, QCryptoNettleRsa, akcipher);
    mpz_t m;
    int dec_ret;

    if (enc_len > rsa->priv.size || data_len < rsa->priv.size) {
        error_setg(errp, "Invalid buffer size");
        return -VIRTIO_CRYPTO_BADMSG;
    }

    switch (rsa->padding_algo) {
    case VIRTIO_CRYPTO_RSA_RAW_PADDING:
        return _rsa_dec_raw(rsa, enc, enc_len, data, data_len, errp);

    case VIRTIO_CRYPTO_RSA_PKCS1_PADDING:
        mpz_init(m);
        dec_ret = rsa_encrypt(&rsa->pub, NULL, wrap_nettle_random_func,
                              data_len, (uint8_t *)data, m);
        if (dec_ret != 1) {
            error_setg(errp, "Failed to encrypt");
        } else {
            nettle_mpz_get_str_256(data_len, (uint8_t *)data_len, m);
        }
        mpz_clear(m);
        return dec_ret == 1 ? 0 : -1;

    default:
        error_setg(errp, "Unknown padding");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    return -1;
}


static int qcrypto_nettle_rsa_sign(QCryptoAkcipher *akcipher,
                                   const void *data, size_t data_len,
                                   void *sig, size_t sig_len, Error **errp)
{
    QCryptoNettleRsa *rsa = container_of(akcipher, QCryptoNettleRsa, akcipher);
    int ret;
    mpz_t s;

    /*
     * The RSA algorithm cannot be used for signature/verification
     * without padding.
     */
    if (rsa->padding_algo == VIRTIO_CRYPTO_RSA_RAW_PADDING) {
        error_setg(errp, "Try to make signature without padding");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    if (data_len > rsa->priv.size || sig_len < rsa->priv.size) {
        error_setg(errp, "Invalid buffer size");
        return -VIRTIO_CRYPTO_BADMSG;
    }

    mpz_init(s);
    switch (rsa->hash_algo) {
    case VIRTIO_CRYPTO_RSA_MD5:
        ret = rsa_md5_sign_digest(&rsa->priv, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA1:
        ret = rsa_sha1_sign_digest(&rsa->priv, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA256:
        ret = rsa_sha256_sign_digest(&rsa->priv, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA512:
        ret = rsa_sha512_sign_digest(&rsa->priv, data, s);
        break;

    default:
        error_setg(errp, "Unknown hash algorithm");
        ret = -VIRTIO_CRYPTO_NOTSUPP;
        goto clear;
    }

    if (ret != 1) {
        error_setg(errp, "Failed to make signature");
        ret = -VIRTIO_CRYPTO_BADMSG;
        goto clear;
    }
    nettle_mpz_get_str_256(sig_len, (uint8_t *)sig, s);
    ret = 0;

clear:
    mpz_clear(s);

    return ret;
}

static int qcrypto_nettle_rsa_verify(QCryptoAkcipher *akcipher,
                                     const void *sig, size_t sig_len,
                                     const void *data, size_t data_len,
                                     Error **errp)
{
    QCryptoNettleRsa *rsa = container_of(akcipher, QCryptoNettleRsa, akcipher);

    int ret;
    mpz_t s;

    /*
     * The RSA algorithm cannot be used for signature/verification
     * without padding.
     */
    if (rsa->padding_algo == VIRTIO_CRYPTO_RSA_RAW_PADDING) {
        error_setg(errp, "Operation not supported");
        return -1;
    }
    if (data_len > rsa->pub.size || sig_len < rsa->pub.size) {
        error_setg(errp, "Invalid buffer size");
        return -1;
    }

    nettle_mpz_init_set_str_256_u(s, sig_len, sig);
    switch (rsa->hash_algo) {
    case VIRTIO_CRYPTO_RSA_MD5:
        ret = rsa_md5_verify_digest(&rsa->pub, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA1:
        ret = rsa_sha1_verify_digest(&rsa->pub, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA256:
        ret = rsa_sha256_verify_digest(&rsa->pub, data, s);
        break;

    case VIRTIO_CRYPTO_RSA_SHA512:
        ret = rsa_sha512_verify_digest(&rsa->pub, data, s);
        break;

    default:
        error_setg(errp, "Unknown hash algorithm");
        ret = -VIRTIO_CRYPTO_NOTSUPP;
        goto clear;
    }

    if (ret != 1) {
        error_setg(errp, "Failed to verify");
        ret = -VIRTIO_CRYPTO_KEY_REJECTED;
        goto clear;
    }
    ret = 0;

clear:
    mpz_clear(s);

    return ret;
}

static int qcrypto_nettle_rsa_free(struct QCryptoAkcipher *akcipher,
                                   Error **errp)
{
    qcrypto_nettle_rsa_destroy(akcipher);

    return 0;
}

QCryptoAkcipherDriver nettle_rsa = {
    .encrypt = qcrypto_nettle_rsa_encrypt,
    .decrypt = qcrypto_nettle_rsa_decrypt,
    .sign = qcrypto_nettle_rsa_sign,
    .verify = qcrypto_nettle_rsa_verify,
    .free = qcrypto_nettle_rsa_free,
};

static QCryptoNettleRsa *qcrypto_nettle_rsa_malloc(void)
{
    QCryptoNettleRsa *rsa = g_malloc0(sizeof(QCryptoNettleRsa));
    memset(rsa, 0, sizeof(QCryptoNettleRsa));
    rsa->akcipher.driver = &nettle_rsa;
    rsa_public_key_init(&rsa->pub);
    rsa_private_key_init(&rsa->priv);

    return rsa;
}
