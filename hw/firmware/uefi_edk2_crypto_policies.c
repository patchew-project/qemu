/*
 * UEFI EDK2 Support
 *
 * Copyright (c) 2019 Red Hat Inc.
 *
 * Author:
 *  Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "hw/firmware/uefi_edk2.h"


#define TYPE_EDK2_CRYPTO "edk2_crypto"

#define EDK2_CRYPTO_CLASS(klass) \
     OBJECT_CLASS_CHECK(Edk2CryptoClass, (klass), \
                        TYPE_EDK2_CRYPTO)
#define EDK2_CRYPTO_GET_CLASS(obj) \
     OBJECT_GET_CLASS(Edk2CryptoClass, (obj), \
                      TYPE_EDK2_CRYPTO)
#define EDK2_CRYPTO(obj) \
     OBJECT_CHECK(Edk2Crypto, (obj), \
                  TYPE_EDK2_CRYPTO)

typedef struct FWCfgHostContent {
    /*
     * Path to the acceptable ciphersuites and the preferred order from
     * the host-side crypto policy.
     */
    char *filename;
    /*
     * Add a new NAMED fw_cfg item as a raw "blob" of the given size. The data
     * referenced by the starting pointer is only linked, NOT copied, into the
     * data structure of the fw_cfg device.
     */
    char *contents;

    size_t contents_length;
} FWCfgHostContent;

typedef struct Edk2Crypto {
    Object parent_obj;

    /*
     * Path to the acceptable ciphersuites and the preferred order from
     * the host-side crypto policy.
     */
    FWCfgHostContent ciphers;
    /* Path to the trusted CA certificates configured on the host side. */
    FWCfgHostContent cacerts;
} Edk2Crypto;

typedef struct Edk2CryptoClass {
    ObjectClass parent_class;
} Edk2CryptoClass;

static Edk2Crypto *edk2_crypto_by_policy_id(const char *policy_id, Error **errp)
{
    Object *obj;

    obj = object_resolve_path_component(object_get_objects_root(), policy_id);
    if (!obj) {
        error_setg(errp, "Cannot find EDK2 crypto policy ID %s", policy_id);
        return NULL;
    }

    if (!object_dynamic_cast(obj, TYPE_EDK2_CRYPTO)) {
        error_setg(errp, "Object '%s' is not a EDK2 crypto subclass",
                         policy_id);
        return NULL;
    }

    return EDK2_CRYPTO(obj);
}

static void edk2_crypto_prop_set_ciphers(Object *obj, const char *value,
                                         Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->ciphers.filename);
    s->ciphers.filename = g_strdup(value);
}

static char *edk2_crypto_prop_get_ciphers(Object *obj,
                                          Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    return g_strdup(s->ciphers.filename);
}

static void edk2_crypto_prop_set_cacerts(Object *obj, const char *value,
                                         Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->cacerts.filename);
    s->cacerts.filename = g_strdup(value);
}

static char *edk2_crypto_prop_get_cacerts(Object *obj,
                                          Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    return g_strdup(s->cacerts.filename);
}

static void edk2_crypto_complete(UserCreatable *uc, Error **errp)
{
    Edk2Crypto *s = EDK2_CRYPTO(uc);
    Error *local_err = NULL;
    GError *gerr = NULL;

    if (s->ciphers.filename) {
        if (!g_file_get_contents(s->ciphers.filename, &s->ciphers.contents,
                                 &s->ciphers.contents_length, &gerr)) {
            goto report_error;
        }
    }
    if (s->cacerts.filename) {
        if (!g_file_get_contents(s->cacerts.filename, &s->cacerts.contents,
                                 &s->cacerts.contents_length, &gerr)) {
            goto report_error;
        }
    }
    return;

 report_error:
    error_setg(&local_err, "%s", gerr->message);
    g_error_free(gerr);
    error_propagate_prepend(errp, local_err, "EDK2 crypto policy: ");
}

static void edk2_crypto_finalize(Object *obj)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->ciphers.filename);
    g_free(s->ciphers.contents);
    g_free(s->cacerts.filename);
    g_free(s->cacerts.contents);
}

static void edk2_crypto_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = edk2_crypto_complete;

    object_class_property_add_str(oc, "ciphers",
                                  edk2_crypto_prop_get_ciphers,
                                  edk2_crypto_prop_set_ciphers,
                                  NULL);
    object_class_property_add_str(oc, "cacerts",
                                  edk2_crypto_prop_get_cacerts,
                                  edk2_crypto_prop_set_cacerts,
                                  NULL);
}

static const TypeInfo edk2_crypto_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_EDK2_CRYPTO,
    .instance_size = sizeof(Edk2Crypto),
    .instance_finalize = edk2_crypto_finalize,
    .class_size = sizeof(Edk2CryptoClass),
    .class_init = edk2_crypto_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void edk2_crypto_register_types(void)
{
    type_register_static(&edk2_crypto_info);
}

type_init(edk2_crypto_register_types);

static void edk2_add_host_crypto_policy_https(FWCfgState *fw_cfg)
{
    Edk2Crypto *s;

    s = edk2_crypto_by_policy_id("https", NULL);
    if (!s) {
        return;
    }
    if (s->ciphers.contents_length) {
        fw_cfg_add_file(fw_cfg, "etc/edk2/https/ciphers",
                        s->ciphers.contents, s->ciphers.contents_length);
    }
    if (s->cacerts.contents_length) {
        fw_cfg_add_file(fw_cfg, "etc/edk2/https/cacerts",
                        s->cacerts.contents, s->cacerts.contents_length);
    }
}

void edk2_add_host_crypto_policy(FWCfgState *fw_cfg)
{
    edk2_add_host_crypto_policy_https(fw_cfg);
}
