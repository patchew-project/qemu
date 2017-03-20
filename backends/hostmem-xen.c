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
#include "qemu/mmap-alloc.h"
#include "sysemu/hostmem.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"


#define TYPE_MEMORY_BACKEND_XEN "memory-backend-xen"

#define MEMORY_BACKEND_XEN(obj) \
    OBJECT_CHECK(HostMemoryBackendXen, (obj), TYPE_MEMORY_BACKEND_XEN)

typedef struct HostMemoryBackendXen HostMemoryBackendXen;

struct HostMemoryBackendXen {
    HostMemoryBackend parent_obj;

    char *mem_path;
};

static void xen_backend_memory_alloc(HostMemoryBackend *backend, Error **errp)
{
    HostMemoryBackendXen *db = MEMORY_BACKEND_XEN(backend);
    int fd;
    size_t page_size;

    if (!backend->size) {
        error_setg(errp, "can't create backend with size 0");
        return;
    }
    memory_region_init(&backend->mr,
                       OBJECT(backend), db->mem_path, backend->size);

    fd = open(db->mem_path, O_RDONLY);
    if (!fd) {
        error_setg(errp, "can't open file %s, err %d", db->mem_path, errno);
        return;
    }
    page_size = qemu_fd_getpagesize(fd);
    backend->mr.align = MAX(page_size, QEMU_VMALLOC_ALIGN);
    close(fd);
}

static void xen_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->alloc = xen_backend_memory_alloc;
}

static char *get_mem_path(Object *o, Error **errp)
{
    HostMemoryBackendXen *db = MEMORY_BACKEND_XEN(o);

    return g_strdup(db->mem_path);
}

static void set_mem_path(Object *o, const char *str, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);
    HostMemoryBackendXen *db = MEMORY_BACKEND_XEN(o);

    if (memory_region_size(&backend->mr)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    g_free(db->mem_path);
    db->mem_path = g_strdup(str);
}

static void
xen_backend_instance_init(Object *o)
{
    object_property_add_str(o, "mem-path", get_mem_path,
                            set_mem_path, NULL);
}

static void xen_backend_instance_finalize(Object *o)
{
    HostMemoryBackendXen *db = MEMORY_BACKEND_XEN(o);

    g_free(db->mem_path);
}

static const TypeInfo xen_backend_info = {
    .name = TYPE_MEMORY_BACKEND_XEN,
    .parent = TYPE_MEMORY_BACKEND,
    .class_init = xen_backend_class_init,
    .instance_init = xen_backend_instance_init,
    .instance_finalize = xen_backend_instance_finalize,
    .instance_size = sizeof(HostMemoryBackendXen),
};

static void register_types(void)
{
    type_register_static(&xen_backend_info);
}

type_init(register_types);
