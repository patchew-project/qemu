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
     INTERFACE_CHECK(Edk2Crypto, (obj), \
                     TYPE_EDK2_CRYPTO)

typedef struct Edk2Crypto {
    Object parent_obj;

    /*
     * Path to the acceptable ciphersuites and the preferred order from
     * the host-side crypto policy.
     */
    char *ciphers_path;

    /* Path to the trusted CA certificates configured on the host side. */
    char *cacerts_path;
} Edk2Crypto;

typedef struct Edk2CryptoClass {
    ObjectClass parent_class;
} Edk2CryptoClass;


static void edk2_crypto_prop_set_ciphers(Object *obj, const char *value,
                                         Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->ciphers_path);
    s->ciphers_path = g_strdup(value);
}

static char *edk2_crypto_prop_get_ciphers(Object *obj,
                                          Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    return g_strdup(s->ciphers_path);
}

static void edk2_crypto_prop_set_cacerts(Object *obj, const char *value,
                                         Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->cacerts_path);
    s->cacerts_path = g_strdup(value);
}

static char *edk2_crypto_prop_get_cacerts(Object *obj,
                                          Error **errp G_GNUC_UNUSED)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    return g_strdup(s->cacerts_path);
}

static void edk2_crypto_finalize(Object *obj)
{
    Edk2Crypto *s = EDK2_CRYPTO(obj);

    g_free(s->ciphers_path);
    g_free(s->cacerts_path);
}

static void edk2_crypto_class_init(ObjectClass *oc, void *data)
{
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

static Edk2Crypto *edk2_crypto_by_id(const char *edk_crypto_id, Error **errp)
{
    Object *obj;
    Object *container;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container,
                                        edk_crypto_id);
    if (!obj) {
        error_setg(errp, "Cannot find EDK2 crypto object ID %s",
                   edk_crypto_id);
        return NULL;
    }

    if (!object_dynamic_cast(obj, TYPE_EDK2_CRYPTO)) {
        error_setg(errp, "Object '%s' is not a EDK2 crypto subclass",
                   edk_crypto_id);
        return NULL;
    }

    return EDK2_CRYPTO(obj);
}

void edk2_add_host_crypto_policy(FWCfgState *fw_cfg)
{
    Edk2Crypto *s;

    s = edk2_crypto_by_id("https", NULL);
    if (!s) {
        return;
    }

    if (s->ciphers_path) {
        /* TODO g_free the returned pointer */
        fw_cfg_add_file_from_host(fw_cfg, "etc/edk2/https/ciphers",
                                  s->ciphers_path, NULL);
    }

    if (s->cacerts_path) {
        /* TODO g_free the returned pointer */
        fw_cfg_add_file_from_host(fw_cfg, "etc/edk2/https/cacerts",
                                  s->cacerts_path, NULL);
    }
}
