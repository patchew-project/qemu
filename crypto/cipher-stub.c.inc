/*
 * QEMU Crypto cipher built-in algorithms
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

bool qcrypto_cipher_supports(QCryptoCipherAlgo alg,
                             QCryptoCipherMode mode)
{
    return false;
}

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgo alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    error_setg(errp,
               "Unsupported cipher algorithm %s, no crypto library enabled in build",
               QCryptoCipherAlgo_str(alg));
    return NULL;
}
