/*
 * Copyright (c) 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "trace.h"

static QLIST_HEAD(, FactoryObject) factory_objects;

void vmstate_add_factory_object(const char *factory_name,
                                const char *instance_name,
                                int instance_id,
                                void *opaque)
{
    FactoryObject *obj = g_new0(FactoryObject, 1);

    obj->opaque = opaque;
    obj->factory_name = g_strdup(factory_name);
    obj->instance_name = g_strdup(instance_name);
    obj->instance_id = instance_id;
    QLIST_INSERT_HEAD(&factory_objects, obj, next);
    trace_vmstate_add_factory_object(factory_name, instance_name, instance_id,
                                     opaque);

}

#define object_match(obj, _factory_name, _instance_name, _instance_id) \
    (!strcmp(obj->factory_name, _factory_name) &&                      \
     !strcmp(obj->instance_name, _instance_name) &&                    \
     obj->instance_id == _instance_id)

static FactoryObject *find_object(const char *factory_name,
                                  const char *instance_name,
                                  int instance_id)
{
    FactoryObject *obj;

    QLIST_FOREACH(obj, &factory_objects, next) {
        if (object_match(obj, factory_name, instance_name, instance_id)) {
            return obj;
        }
    }

    return NULL;
}

void *vmstate_find_factory_object(const char *factory_name,
                                  const char *instance_name,
                                  int instance_id)
{
    FactoryObject *obj = find_object(factory_name, instance_name, instance_id);
    void *opaque = obj ? obj->opaque : NULL;

    trace_vmstate_find_factory_object(factory_name, instance_name, instance_id,
                                      opaque);
    return opaque;
}

void *vmstate_claim_factory_object(const char *factory_name,
                                   const char *instance_name,
                                   int instance_id)
{
    FactoryObject *obj = find_object(factory_name, instance_name, instance_id);
    void *opaque = obj ? obj->opaque : NULL;

    if (obj) {
        g_free(obj->factory_name);
        g_free(obj->instance_name);
        QLIST_REMOVE(obj, next);
    }

    trace_vmstate_claim_factory_object(factory_name, instance_name, instance_id,
                                       opaque);
    return opaque;
}
