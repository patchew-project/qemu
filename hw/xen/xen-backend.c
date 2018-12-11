/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/xen/xen-backend.h"

typedef struct XenBackendImpl {
    const char *type;
    XenBackendDeviceCreate create;
} XenBackendImpl;

static GHashTable *xen_backend_table_get(void)
{
    static GHashTable *table;

    if (table == NULL) {
        table = g_hash_table_new(g_str_hash, g_str_equal);
    }

    return table;
}

static void xen_backend_table_add(XenBackendImpl *impl)
{
    g_hash_table_insert(xen_backend_table_get(), (void *)impl->type, impl);
}

static XenBackendImpl *xen_backend_table_lookup(const char *type)
{
    return g_hash_table_lookup(xen_backend_table_get(), type);
}

void xen_backend_register(const XenBackendInfo *info)
{
    XenBackendImpl *impl = g_new0(XenBackendImpl, 1);

    g_assert(info->type);

    if (xen_backend_table_lookup(info->type)) {
        error_report("attempt to register duplicate Xen backend type '%s'",
                     info->type);
        abort();
    }

    if (!info->create) {
        error_report("backend type '%s' has no creator", info->type);
        abort();
    }

    impl->type = info->type;
    impl->create = info->create;

    xen_backend_table_add(impl);
}

void xen_backend_device_create(BusState *bus, const char *type,
                               const char *name, QDict *opts, Error **errp)
{
    XenBackendImpl *impl = xen_backend_table_lookup(type);

    if (impl) {
        impl->create(bus, name, opts, errp);
    }
}
