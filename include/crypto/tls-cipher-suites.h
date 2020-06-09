/*
 * QEMU TLS Cipher Suites Registry (RFC8447)
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author: Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QCRYPTO_TLSCIPHERSUITES_H
#define QCRYPTO_TLSCIPHERSUITES_H

#include "qom/object.h"
#include "crypto/tlscreds.h"

#define TYPE_QCRYPTO_TLS_CIPHER_SUITES "tls-cipher-suites"
#define QCRYPTO_TLS_CIPHER_SUITES(obj) \
    OBJECT_CHECK(QCryptoTLSCipherSuites, (obj), TYPE_QCRYPTO_TLS_CIPHER_SUITES)

/*
 * IANA registered TLS ciphers:
 * https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4
 */
typedef struct {
    uint8_t data[2];
} QEMU_PACKED IANA_TLS_CIPHER;

typedef struct QCryptoTLSCipherSuites {
    /* <private> */
    QCryptoTLSCreds parent_obj;

    /* <public> */
    IANA_TLS_CIPHER *cipher_list;
    unsigned cipher_count;
} QCryptoTLSCipherSuites;

#endif /* QCRYPTO_TLSCIPHERSUITES_H */
