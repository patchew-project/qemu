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
    GDBusProxy *proxy;
    char *id;
    char *dbus_addr;
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


static char *
get_idstr(DBusVMState *self)
{
    return g_strdup_printf("%s-%s", TYPE_DBUS_VMSTATE, self->id);
}

static char *
dbus_proxy_get_id(GDBusProxy *proxy, GError **err)
{
    char *id = NULL;
    GVariant *result = NULL;
    size_t size;

    result = g_dbus_proxy_get_cached_property(proxy, "Id");
    if (!result) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Failed to get Id property");
        return NULL;
    }

    id = g_variant_dup_string(result, &size);
    g_clear_pointer(&result, g_variant_unref);

    if (size == 0 || size >= 256) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Invalid Id property");
        g_free(id);
        return NULL;
    }

    return id;
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
    uint8_t *data = NULL;
    int ret = -1;
    char id[256];
    unsigned int size;

    if (qemu_get_counted_string(f, id) == 0) {
        error_report("Invalid vmstate Id");
        goto end;
    }

    if (g_strcmp0(id, self->id)) {
        error_report("Invalid vmstate Id: %s != %s", id, self->id);
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

    if (dbus_load_state_proxy(self->proxy, data, size) < 0) {
        error_report("Failed to restore Id '%s'", id);
        goto end;
    }

    ret = 0;

end:
    g_clear_pointer(&data, g_free);
    return ret;
}

static void
dbus_save_state(QEMUFile *f, void *opaque)
{
    DBusVMState *self = DBUS_VMSTATE(opaque);
    GVariant *result = NULL, *child = NULL;
    const uint8_t *data;
    size_t size;
    GError *err = NULL;

    result = g_dbus_proxy_call_sync(self->proxy, "Save",
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
        error_report("Too much vmstate data to save: %zu", size);
        goto end;
    }

    qemu_put_counted_string(f, self->id);
    qemu_put_be32(f, size);
    qemu_put_buffer(f, data, size);

end:
    g_clear_pointer(&child, g_variant_unref);
    g_clear_pointer(&result, g_variant_unref);
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
    GDBusConnection *bus = NULL;
    GDBusProxy *proxy = NULL;
    char *idstr = NULL;

    if (!self->dbus_addr) {
        error_setg(errp, QERR_MISSING_PARAMETER, "addr");
        return;
    }

    bus = g_dbus_connection_new_for_address_sync(self->dbus_addr,
               G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
               NULL, NULL, &err);
    if (err) {
        error_setg(errp, "failed to connect to DBus: '%s'", err->message);
        g_clear_error(&err);
        return;
    }

    self->bus = bus;

    proxy = g_dbus_proxy_new_sync(bus,
                G_DBUS_PROXY_FLAGS_NONE,
                (GDBusInterfaceInfo *) &vmstate1_interface_info,
                NULL,
                "/org/qemu/VMState1",
                "org.qemu.VMState1",
                NULL, &err);

    if (err) {
        error_setg(errp, "failed to create DBus proxy: '%s'", err->message);
        g_clear_error(&err);
        return;
    }

    self->proxy = proxy;

    self->id = dbus_proxy_get_id(proxy, &err);
    if (!self->id) {
        error_setg(errp, "failed to get DBus Id: '%s'", err->message);
        g_clear_error(&err);
        return;
    }

    idstr = get_idstr(self);
    if (register_savevm_live(NULL, idstr, 0, 0,
                             &savevm_handlers, self) < 0) {
        error_setg(errp, "Failed to register savevm handler");
    }
    g_free(idstr);
}

static void
dbus_vmstate_finalize(Object *o)
{
    DBusVMState *self = DBUS_VMSTATE(o);
    char *idstr = get_idstr(self);

    unregister_savevm(NULL, idstr, self);

    g_clear_object(&self->bus);
    g_clear_object(&self->proxy);
    g_free(self->dbus_addr);
    g_free(self->id);
    g_free(idstr);
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

static void
dbus_vmstate_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = dbus_vmstate_complete;

    object_class_property_add_str(oc, "addr",
                                  get_dbus_addr, set_dbus_addr,
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
