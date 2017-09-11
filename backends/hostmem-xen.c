/*
 * QEMU Host Memory Backend for Xen
 *
 * Copyright(C) 2017 Intel Corporation.
 *
 * Author:
 *   Haozhong Zhang <haozhong.zhang@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "sysemu/hostmem.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"

#define TYPE_MEMORY_BACKEND_XEN "memory-backend-xen"

#define MEMORY_BACKEND_XEN(obj) \
    OBJECT_CHECK(HostMemoryBackendXen, (obj), TYPE_MEMORY_BACKEND_XEN)

typedef struct HostMemoryBackendXen HostMemoryBackendXen;

struct HostMemoryBackendXen {
    HostMemoryBackend parent_obj;

    uint64_t host_addr;
};

static void xen_backend_get_host_addr(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    HostMemoryBackendXen *backend = MEMORY_BACKEND_XEN(obj);
    uint64_t value = backend->host_addr;

    visit_type_size(v, name, &value, errp);
}

static void xen_backend_set_host_addr(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    HostMemoryBackendXen *xb = MEMORY_BACKEND_XEN(obj);
    Error *local_err = NULL;
    uint64_t value;

    if (memory_region_size(&backend->mr)) {
        error_setg(&local_err, "cannot change property value");
        goto out;
    }

    visit_type_size(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    xb->host_addr = value;

 out:
    error_propagate(errp, local_err);
}

static void xen_backend_alloc(HostMemoryBackend *backend, Error **errp)
{
    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return;
    }
    memory_region_init(&backend->mr, OBJECT(backend), "hostmem-xen",
                       backend->size);
    backend->mr.align = getpagesize();
}

static void xen_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = xen_backend_alloc;

    object_class_property_add(oc, "host-addr", "int",
                              xen_backend_get_host_addr,
                              xen_backend_set_host_addr,
                              NULL, NULL, &error_abort);
}

static const TypeInfo xen_backend_info = {
    .name = TYPE_MEMORY_BACKEND_XEN,
    .parent = TYPE_MEMORY_BACKEND,
    .class_init = xen_backend_class_init,
    .instance_size = sizeof(HostMemoryBackendXen),
};

static void register_types(void)
{
    type_register_static(&xen_backend_info);
}

type_init(register_types);
