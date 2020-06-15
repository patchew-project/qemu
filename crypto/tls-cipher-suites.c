/*
 * QEMU TLS Cipher Suites
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author: Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "crypto/tlscreds.h"
#include "crypto/tls-cipher-suites.h"
#include "trace.h"

static void parse_cipher_suites(QCryptoTLSCipherSuites *s,
                                const char *priority_name, Error **errp)
{
    int ret;
    const char *err;
    gnutls_priority_t pcache;
    enum { M_ENUMERATE, M_GENERATE, M_DONE } mode;

    assert(priority_name);
    trace_qcrypto_tls_cipher_suite_priority(priority_name);
    ret = gnutls_priority_init(&pcache, priority_name, &err);
    if (ret < 0) {
        error_setg(errp, "Syntax error using priority '%s': %s",
                   priority_name, gnutls_strerror(ret));
        return;
    }

    for (mode = M_ENUMERATE; mode < M_DONE; mode++) {
        size_t i;

        for (i = 0;; i++) {
            int ret;
            unsigned idx;
            const char *name;
            IANA_TLS_CIPHER cipher;
            gnutls_protocol_t protocol;

            ret = gnutls_priority_get_cipher_suite_index(pcache, i, &idx);
            if (ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
                break;
            }
            if (ret == GNUTLS_E_UNKNOWN_CIPHER_SUITE) {
                continue;
            }

            name = gnutls_cipher_suite_info(idx, (unsigned char *)&cipher,
                                            NULL, NULL, NULL, &protocol);
            if (name == NULL) {
                continue;
            }

            if (mode == M_GENERATE) {
                const char *version;

                version = gnutls_protocol_get_name(protocol);
                trace_qcrypto_tls_cipher_suite_info(cipher.data[0],
                                                    cipher.data[1],
                                                    version, name);
                s->cipher_list[s->cipher_count] = cipher;
            }
            s->cipher_count++;
        }

        if (mode == M_ENUMERATE) {
            if (s->cipher_count == 0) {
                break;
            }
            s->cipher_list = g_new(IANA_TLS_CIPHER, s->cipher_count);
            s->cipher_count = 0;
        }
    }
    trace_qcrypto_tls_cipher_suite_count(s->cipher_count);
    gnutls_priority_deinit(pcache);
}

static void qcrypto_tls_cipher_suites_complete(UserCreatable *uc, Error **errp)
{
    QCryptoTLSCreds *s = QCRYPTO_TLS_CREDS(uc);

    if (!s->priority) {
        error_setg(errp, "'priority' property is not set");
        return;
    }
    parse_cipher_suites(QCRYPTO_TLS_CIPHER_SUITES(s), s->priority, errp);
}

static void qcrypto_tls_cipher_suites_finalize(Object *obj)
{
    QCryptoTLSCipherSuites *s = QCRYPTO_TLS_CIPHER_SUITES(obj);

    g_free(s->cipher_list);
}

static void qcrypto_tls_cipher_suites_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = qcrypto_tls_cipher_suites_complete;
}

static const TypeInfo qcrypto_tls_cipher_suites_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CIPHER_SUITES,
    .instance_size = sizeof(QCryptoTLSCipherSuites),
    .instance_finalize = qcrypto_tls_cipher_suites_finalize,
    .class_size = sizeof(QCryptoTLSCredsClass),
    .class_init = qcrypto_tls_cipher_suites_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void qcrypto_tls_cipher_suites_register_types(void)
{
    type_register_static(&qcrypto_tls_cipher_suites_info);
}

type_init(qcrypto_tls_cipher_suites_register_types);
