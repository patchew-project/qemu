/*
 * QEMU dbus-vmstate
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qapi/qmp/qerror.h"
#include "migration/register.h"
#include "migration/qemu-file-types.h"
#include <gio/gio.h>

typedef struct DBusVMState DBusVMState;
typedef struct DBusVMStateClass DBusVMStateClass;

#define TYPE_DBUS_VMSTATE "dbus-vmstate"
#define DBUS_VMSTATE(obj)                                \
    OBJECT_CHECK(DBusVMState, (obj), TYPE_DBUS_VMSTATE)
#define DBUS_VMSTATE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(DBusVMStateClass, (obj), TYPE_DBUS_VMSTATE)
#define DBUS_VMSTATE_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(DBusVMStateClass, (klass), TYPE_DBUS_VMSTATE)

struct DBusVMStateClass {
    ObjectClass parent_class;
};

struct DBusVMState {
    Object parent;

    GDBusConnection *bus;
    char *dbus_addr;
    char *id_list;
};

static const GDBusPropertyInfo vmstate_property_info[] = {
    { -1, (char *) "Id", (char *) "s",
      G_DBUS_PROPERTY_INFO_FLAGS_READABLE, NULL },
};

static const GDBusPropertyInfo * const vmstate_property_info_pointers[] = {
    &vmstate_property_info[0],
    NULL
};

static const GDBusInterfaceInfo vmstate1_interface_info = {
    -1,
    (char *) "org.qemu.VMState1",
    (GDBusMethodInfo **) NULL,
    (GDBusSignalInfo **) NULL,
    (GDBusPropertyInfo **) &vmstate_property_info_pointers,
    NULL,
};

#define DBUS_VMSTATE_SIZE_LIMIT (1 << 20) /* 1mb */
#define DBUS_VMSTATE_SECTION 0x00
#define DBUS_VMSTATE_EOF 0xff


static char **
dbus_get_vmstate1_names(DBusVMState *self, GError **err)
{
    char **names = NULL;
    GDBusProxy *proxy;
    GVariant *result = NULL, *child = NULL;

    proxy = g_dbus_proxy_new_sync(self->bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  "org.freedesktop.DBus",
                                  "/org/freedesktop/DBus",
                                  "org.freedesktop.DBus",
                                  NULL, err);
    if (!proxy) {
        goto end;
    }

    result = g_dbus_proxy_call_sync(proxy, "ListQueuedOwners",
                                    g_variant_new("(s)", "org.qemu.VMState1"),
                                    G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, err);
    if (!result) {
        goto end;
    }

    child = g_variant_get_child_value(result, 0);
    names = g_variant_dup_strv(child, NULL);

end:
    g_clear_pointer(&child, g_variant_unref);
    g_clear_pointer(&result, g_variant_unref);
    g_clear_object(&proxy);
    return names;
}

static GHashTable *
get_id_list_set(DBusVMState *self)
{
    char **ids;
    GHashTable *set;
    int i;

    if (!self->id_list) {
        return NULL;
    }

    ids = g_strsplit(self->id_list, ",", -1);
    set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (i = 0; ids[i]; i++) {
        g_hash_table_add(set, ids[i]);
        ids[i] = NULL;
    }

    g_strfreev(ids);
    return set;
}

static GHashTable *
dbus_get_proxies(DBusVMState *self, GError **err)
{
    GError *local_err = NULL;
    GHashTable *proxies = NULL;
    GVariant *result = NULL;
    char **names = NULL;
    char **left = NULL;
    GHashTable *ids = get_id_list_set(self);
    size_t i;

    proxies = g_hash_table_new_full(g_str_hash, g_str_equal,
                                    g_free, g_object_unref);

    names = dbus_get_vmstate1_names(self, &local_err);
    if (!names) {
        if (g_error_matches(local_err,
                            G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            return proxies;
        }
        g_propagate_error(err, local_err);
        goto err;
    }

    for (i = 0; names[i]; i++) {
        GDBusProxy *proxy;
        char *id;
        size_t size;

        proxy = g_dbus_proxy_new_sync(self->bus, G_DBUS_PROXY_FLAGS_NONE,
                    (GDBusInterfaceInfo *) &vmstate1_interface_info,
                    names[i],
                    "/org/qemu/VMState1",
                    "org.qemu.VMState1",
                    NULL, err);
        if (!proxy) {
            goto err;
        }

        result = g_dbus_proxy_get_cached_property(proxy, "Id");
        if (!result) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "VMState Id property is missing.");
            g_clear_object(&proxy);
            goto err;
        }

        id = g_variant_dup_string(result, &size);
        if (ids && !g_hash_table_remove(ids, id)) {
            g_free(id);
            g_clear_object(&proxy);
            continue;
        }
        if (size == 0 || size >= 256) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "VMState Id '%s' is invalid.", id);
            g_free(id);
            g_clear_object(&proxy);
            goto err;
        }

        if (!g_hash_table_insert(proxies, id, proxy)) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Duplicated VMState Id '%s'", id);
            goto err;
        }

        g_clear_pointer(&result, g_variant_unref);
    }

    if (ids) {
        left = (char **)g_hash_table_get_keys_as_array(ids, NULL);
        if (*left) {
            char *leftids = g_strjoinv(",", left);
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Required VMState Id are missing: %s", leftids);
            g_free(leftids);
            goto err;
        }
        g_free(left);
    }

    g_clear_pointer(&ids, g_hash_table_unref);
    g_strfreev(names);
    return proxies;

err:
    g_free(left);
    g_clear_pointer(&proxies, g_hash_table_unref);
    g_clear_pointer(&result, g_variant_unref);
    g_clear_pointer(&ids, g_hash_table_unref);
    g_strfreev(names);
    return NULL;
}

static int
dbus_load_state_proxy(GDBusProxy *proxy, const uint8_t *data, size_t size)
{
    GError *err = NULL;
    GVariant *value, *result;
    int ret = -1;

    value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                      data, size, sizeof(char));
    result = g_dbus_proxy_call_sync(proxy, "Load",
                                    g_variant_new("(@ay)", value),
                                    G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, &err);
    if (!result) {
        error_report("Failed to Load: %s", err->message);
        goto end;
    }

    ret = 0;

end:
    g_clear_pointer(&result, g_variant_unref);
    g_clear_error(&err);
    return ret;
}

static int
dbus_load_state(QEMUFile *f, void *opaque, int version_id)
{
    DBusVMState *self = DBUS_VMSTATE(opaque);
    GError *err = NULL;
    GHashTable *proxies = NULL;
    uint8_t *data = NULL;
    int ret = -1;

    proxies = dbus_get_proxies(self, &err);
    if (!proxies) {
        error_report("Failed to get proxies: %s", err->message);
        goto end;
    }

    while (qemu_get_byte(f) != DBUS_VMSTATE_EOF) {
        GDBusProxy *proxy = NULL;
        char id[256];
        unsigned int size;

        if (qemu_get_counted_string(f, id) == 0) {
            error_report("Invalid vmstate Id");
            goto end;
        }

        proxy = g_hash_table_lookup(proxies, id);
        if (!proxy) {
            error_report("Failed to find proxy Id '%s'", id);
            goto end;
        }

        size = qemu_get_be32(f);
        if (size > DBUS_VMSTATE_SIZE_LIMIT) {
            error_report("Invalid vmstate size: %u", size);
            goto end;
        }

        data = g_malloc(size);
        if (qemu_get_buffer(f, data, size) != size) {
            error_report("Failed to read %u bytes", size);
            goto end;
        }

        if (dbus_load_state_proxy(proxy, data, size) < 0) {
            error_report("Failed to restore Id '%s'", id);
            goto end;
        }

        g_clear_pointer(&data, g_free);
        g_hash_table_remove(proxies, id);
    }

    if (g_hash_table_size(proxies) != 0) {
        error_report("Missing DBus states from migration stream.");
        goto end;
    }

    ret = 0;

end:
    g_clear_pointer(&proxies, g_hash_table_unref);
    g_clear_pointer(&data, g_free);
    g_clear_error(&err);
    return ret;
}

static void
dbus_save_state_proxy(gpointer key,
                      gpointer value,
                      gpointer user_data)
{
    QEMUFile *f = user_data;
    const char *id = key;
    GDBusProxy *proxy = value;
    GVariant *result = NULL, *child = NULL;
    const uint8_t *data;
    size_t size;
    GError *err = NULL;

    result = g_dbus_proxy_call_sync(proxy, "Save",
                                    NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    -1, NULL, &err);
    if (!result) {
        error_report("Failed to Save: %s", err->message);
        g_clear_error(&err);
        goto end;
    }

    child = g_variant_get_child_value(result, 0);
    data = g_variant_get_fixed_array(child, &size, sizeof(char));
    if (!data) {
        error_report("Failed to Save: not a byte array");
        goto end;
    }
    if (size > DBUS_VMSTATE_SIZE_LIMIT) {
        error_report("Too much vmstate data to save: %lu", size);
        goto end;
    }

    qemu_put_byte(f, DBUS_VMSTATE_SECTION);
    qemu_put_counted_string(f, id);
    qemu_put_be32(f, size);
    qemu_put_buffer(f, data, size);

end:
    g_clear_pointer(&child, g_variant_unref);
    g_clear_pointer(&result, g_variant_unref);
}

static void
dbus_save_state(QEMUFile *f, void *opaque)
{
    DBusVMState *self = DBUS_VMSTATE(opaque);
    GHashTable *proxies;
    GError *err = NULL;

    proxies = dbus_get_proxies(self, &err);
    if (!proxies) {
        error_report("Failed to get proxies: %s", err->message);
        goto end;
    }

    /* TODO: how to report an error and cancel? */
    g_hash_table_foreach(proxies, dbus_save_state_proxy, f);
    qemu_put_byte(f, DBUS_VMSTATE_EOF);

end:
    g_clear_error(&err);
    g_clear_pointer(&proxies, g_hash_table_unref);
}

static const SaveVMHandlers savevm_handlers = {
    .save_state = dbus_save_state,
    .load_state = dbus_load_state,
};

static void
dbus_vmstate_complete(UserCreatable *uc, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(uc);
    GError *err = NULL;
    GDBusConnection *bus;

    if (!object_resolve_path_type("", TYPE_DBUS_VMSTATE, NULL)) {
        error_setg(errp, "There is already an instance of %s",
                   TYPE_DBUS_VMSTATE);
        return;
    }

    if (!self->dbus_addr) {
        error_setg(errp, QERR_MISSING_PARAMETER, "addr");
        return;
    }

    if (register_savevm_live(NULL, TYPE_DBUS_VMSTATE, 0, 0,
                             &savevm_handlers, self) < 0) {
        error_setg(errp, "Failed to register savevm handler");
        return;
    }

    bus = g_dbus_connection_new_for_address_sync(self->dbus_addr,
             G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
             G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
             NULL, NULL, &err);
    if (err) {
        error_setg(errp, "failed to connect to DBus: '%s'", err->message);
        g_clear_error(&err);
    }

    self->bus = bus;
}

static void
dbus_vmstate_finalize(Object *o)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    unregister_savevm(NULL, TYPE_DBUS_VMSTATE, self);
    g_clear_object(&self->bus);
    g_free(self->dbus_addr);
    g_free(self->id_list);
}

static char *
get_dbus_addr(Object *o, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    return g_strdup(self->dbus_addr);
}

static void
set_dbus_addr(Object *o, const char *str, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    g_free(self->dbus_addr);
    self->dbus_addr = g_strdup(str);
}

static char *
get_id_list(Object *o, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    return g_strdup(self->id_list);
}

static void
set_id_list(Object *o, const char *str, Error **errp)
{
    DBusVMState *self = DBUS_VMSTATE(o);

    g_free(self->id_list);
    self->id_list = g_strdup(str);
}

static void
dbus_vmstate_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = dbus_vmstate_complete;

    object_class_property_add_str(oc, "addr",
                                  get_dbus_addr, set_dbus_addr,
                                  &error_abort);
    object_class_property_add_str(oc, "id-list",
                                  get_id_list, set_id_list,
                                  &error_abort);
}

static const TypeInfo dbus_vmstate_info = {
    .name = TYPE_DBUS_VMSTATE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DBusVMState),
    .instance_finalize = dbus_vmstate_finalize,
    .class_size = sizeof(DBusVMStateClass),
    .class_init = dbus_vmstate_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
register_types(void)
{
    type_register_static(&dbus_vmstate_info);
}

type_init(register_types);
