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

static void parse_cipher_suites(QCryptoTLSCipherSuites *s,
                                const char *priority_name, Error **errp)
{
#ifdef CONFIG_GNUTLS
    int ret;
    unsigned int idx;
    const char *name;
    const char *err;
    gnutls_protocol_t version;
    gnutls_priority_t pcache;

    assert(priority_name);
    ret = gnutls_priority_init(&pcache, priority_name, &err);
    if (ret < 0) {
        error_setg(errp, "Syntax error using priority '%s': %s",
                   priority_name, gnutls_strerror(ret));
        return;
    }

    if (s->verbose) {
        fprintf(stderr, "Cipher suites for %s:\n", priority_name);
    }
    for (size_t i = 0;; i++) {
        ret = gnutls_priority_get_cipher_suite_index(pcache, i, &idx);
        if (ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
            break;
        }
        if (ret == GNUTLS_E_UNKNOWN_CIPHER_SUITE) {
            continue;
        }
        s->cipher_list = g_renew(IANA_TLS_CIPHER,
                                 s->cipher_list, s->cipher_count + 1);

        name = gnutls_cipher_suite_info(idx,
                                        s->cipher_list[s->cipher_count].data,
                                        NULL, NULL, NULL, &version);
        if (name != NULL) {
            if (s->verbose) {
                fprintf(stderr, "- %-50s\t0x%02x, 0x%02x\t%s\n", name,
                        s->cipher_list[s->cipher_count].data[0],
                        s->cipher_list[s->cipher_count].data[1],
                        gnutls_protocol_get_name(version));
            }
            s->cipher_count++;
       }
    }
    if (s->verbose) {
        fprintf(stderr, "total: %u ciphers\n", s->cipher_count);
    }
    gnutls_priority_deinit(pcache);
#else
    error_setg(errp, "GNU TLS not available");
#endif /* CONFIG_GNUTLS */
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

static void qcrypto_tls_cipher_suites_set_verbose(Object *obj, bool value,
                                                 Error **errp G_GNUC_UNUSED)
{
    QCRYPTO_TLS_CIPHER_SUITES(obj)->verbose = value;
}


static bool qcrypto_tls_cipher_suites_get_verbose(Object *obj,
                                                 Error **errp G_GNUC_UNUSED)
{
    return QCRYPTO_TLS_CIPHER_SUITES(obj)->verbose;
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

    object_class_property_add_bool(oc, "verbose",
                                   qcrypto_tls_cipher_suites_get_verbose,
                                   qcrypto_tls_cipher_suites_set_verbose);
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
