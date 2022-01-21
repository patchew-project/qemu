/*
 * QEMU Crypto akcipher algorithms
 *
 * Copyright (c) 2022 Bytedance
 * Author: zhenwei pi <pizhenwei@bytedance.com>
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
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "crypto/akcipher.h"

QCryptoAkcipher *qcrypto_akcipher_nettle_new(uint32_t alg, bool private,
                                             const uint8_t *key,
                                             size_t keylen,
                                             void *para,
                                             int index, Error **errp);

QCryptoAkcipher *qcrypto_akcipher_new(uint32_t alg, bool private,
                                      const uint8_t *key, size_t keylen,
                                      void *para,
                                      int index, Error **errp)
{
    QCryptoAkcipher *akcipher = NULL;

#ifdef CONFIG_HOGWEED
    akcipher = qcrypto_akcipher_nettle_new(alg, private, key, keylen,
                                           para, index, errp);
#else
    error_setg(errp, "qcrypto akcipher has no nettle/hogweed support");
#endif

    return akcipher;
}

int qcrypto_akcipher_encrypt(QCryptoAkcipher *akcipher,
                             const void *data, size_t data_len,
                             void *enc, size_t enc_len, Error **errp)
{
    const QCryptoAkcipherDriver *drv = akcipher->driver;

    return drv->encrypt(akcipher, data, data_len, enc, enc_len, errp);
}

int qcrypto_akcipher_decrypt(struct QCryptoAkcipher *akcipher,
                             const void *enc, size_t enc_len,
                             void *data, size_t data_len, Error **errp)
{
    const QCryptoAkcipherDriver *drv = akcipher->driver;

    return drv->decrypt(akcipher, enc, enc_len, data, data_len, errp);
}

int qcrypto_akcipher_sign(struct QCryptoAkcipher *akcipher,
                          const void *data, size_t data_len,
                          void *sig, size_t sig_len, Error **errp)
{
    const QCryptoAkcipherDriver *drv = akcipher->driver;

    return drv->sign(akcipher, data, data_len, sig, sig_len, errp);
}

int qcrypto_akcipher_verify(struct QCryptoAkcipher *akcipher,
                            const void *sig, size_t sig_len,
                            const void *data, size_t data_len, Error **errp)
{
    const QCryptoAkcipherDriver *drv = akcipher->driver;

    return drv->verify(akcipher, sig, sig_len, data, data_len, errp);
}

int qcrypto_akcipher_free(struct QCryptoAkcipher *akcipher, Error **errp)
{
    const QCryptoAkcipherDriver *drv = akcipher->driver;

    return drv->free(akcipher, errp);
}
