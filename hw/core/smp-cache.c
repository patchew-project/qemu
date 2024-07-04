/*
 * Cache Object for SMP machine
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Author: Zhao Liu <zhao1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/core/smp-cache.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-machine-common.h"
#include "qom/object_interfaces.h"

static void
smp_cache_get_cache_prop(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    SMPCache *cache = SMP_CACHE(obj);
    SMPCachePropertyList *head = NULL;
    SMPCachePropertyList **tail = &head;

    for (int i = 0; i < SMP_CACHE__MAX; i++) {
        SMPCacheProperty *node = g_new(SMPCacheProperty, 1);

        node->name = cache->props[i].name;
        node->topo = cache->props[i].topo;
        QAPI_LIST_APPEND(tail, node);
    }

    if (!visit_type_SMPCachePropertyList(v, name, &head, errp)) {
        return;
    }
    qapi_free_SMPCachePropertyList(head);
}

static void
smp_cache_set_cache_prop(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    SMPCache *cache = SMP_CACHE(obj);
    SMPCachePropertyList *list, *node;

    if (!visit_type_SMPCachePropertyList(v, name, &list, errp)) {
        return;
    }

    for (node = list; node; node = node->next) {
        if (node->value->topo == CPU_TOPO_LEVEL_INVALID) {
            error_setg(errp,
                       "Invalid topology level: %s. "
                       "The topology should match the valid CPU topology level",
                       CpuTopologyLevel_str(node->value->topo));
            goto out;
        }
        cache->props[node->value->name].topo = node->value->topo;
    }

out:
    qapi_free_SMPCachePropertyList(list);
}

static void smp_cache_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add(oc, "caches", "SMPCacheProperties",
                              smp_cache_get_cache_prop,
                              smp_cache_set_cache_prop,
                              NULL, NULL);
    object_class_property_set_description(oc, "caches",
            "Cache property list for SMP machine");
}

static void smp_cache_instance_init(Object *obj)
{
    SMPCache *cache = SMP_CACHE(obj);
    for (int i = 0; i < SMP_CACHE__MAX; i++) {
        cache->props[i].name = (SMPCacheName)i;
        cache->props[i].topo = CPU_TOPO_LEVEL_DEFAULT;
    }
}

static const TypeInfo smp_cache_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_SMP_CACHE,
    .class_init = smp_cache_class_init,
    .instance_size = sizeof(SMPCache),
    .instance_init = smp_cache_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void smp_cache_register_type(void)
{
    type_register_static(&smp_cache_info);
}

type_init(smp_cache_register_type);
