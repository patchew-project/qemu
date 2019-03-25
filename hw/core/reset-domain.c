/*
 * Reset Domain object.
 *
 * Copyright (c) 2019 GreenSocs
 *
 * Authors:
 *   Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/reset-domain.h"

void reset_domain_register_object(ResetDomain *domain, Object *obj)
{
    struct ResetDomainEntry *rde;

    INTERFACE_CHECK(ResettableClass, obj, TYPE_RESETTABLE);

    rde = g_malloc0(sizeof(*rde));
    rde->obj = obj;
    object_ref(obj);
    QLIST_INSERT_HEAD(&domain->members, rde, node);
}

void reset_domain_unregister_object(ResetDomain *domain, Object *obj)
{
    struct ResetDomainEntry *rde;

    INTERFACE_CHECK(ResettableClass, obj, TYPE_RESETTABLE);

    QLIST_FOREACH(rde, &domain->members, node) {
        if (rde->obj == obj) {
            QLIST_REMOVE(rde, node);
            object_unref(rde->obj);
            g_free(rde);
            return;
        }
    }
}

static void reset_domain_init_phase(Object *obj, bool cold)
{
    struct ResetDomainEntry *rde;
    ResetDomain *domain = RESET_DOMAIN(obj);

    QLIST_FOREACH(rde, &domain->members, node) {
        resettable_init_phase(rde->obj, cold);
    }
}

static void reset_domain_hold_phase(Object *obj)
{
    struct ResetDomainEntry *rde;
    ResetDomain *domain = RESET_DOMAIN(obj);

    QLIST_FOREACH(rde, &domain->members, node) {
        resettable_hold_phase(rde->obj);
    }
}

static void reset_domain_exit_phase(Object *obj)
{
    struct ResetDomainEntry *rde;
    ResetDomain *domain = RESET_DOMAIN(obj);

    QLIST_FOREACH(rde, &domain->members, node) {
        resettable_exit_phase(rde->obj);
    }
}

static void reset_domain_init(Object *obj)
{
    ResetDomain *domain = RESET_DOMAIN(obj);

    QLIST_INIT(&domain->members);
}

static void reset_domain_finalize(Object *obj)
{
    ResetDomain *domain = RESET_DOMAIN(obj);
    struct ResetDomainEntry *rde, *nrde;

    QLIST_FOREACH_SAFE(rde, &domain->members, node, nrde) {
        QLIST_REMOVE(rde, node);
        object_unref(rde->obj);
        g_free(rde);
    }
}

static void reset_domain_class_init(ObjectClass *class, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(class);

    rc->phases.init = reset_domain_init_phase;
    rc->phases.hold = reset_domain_hold_phase;
    rc->phases.exit = reset_domain_exit_phase;
}

static const TypeInfo reset_domain_type_info = {
    .name = TYPE_RESET_DOMAIN,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(ResetDomain),
    .instance_init = reset_domain_init,
    .instance_finalize = reset_domain_finalize,
    .class_init = reset_domain_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_RESETTABLE },
        { }
    },
};

static void reset_register_types(void)
{
    type_register_static(&reset_domain_type_info);
}

type_init(reset_register_types)
