/*
 *  Reset handlers.
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2019 GreenSocs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "hw/reset-domain.h"

#define TYPE_SYSTEM_RESET_DOMAIN "system-reset-domain"
#define SYSTEM_RESET_DOMAIN(obj) \
    OBJECT_CHECK(SystemResetDomain, (obj), TYPE_SYSTEM_RESET_DOMAIN)
#define SYSTEM_RESET_DOMAIN_CLASS(class) \
    OBJECT_CLASS_CHECK(SystemResetDomainClass, (class), \
                       TYPE_SYSTEM_RESET_DOMAIN)
#define SYSTEM_RESET_DOMAIN_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SystemResetDomainClass, (obj), \
                     TYPE_SYSTEM_RESET_DOMAIN)

/**
 * SystemResetDomainClass:
 * Class to hold the global reset domain and handlers
 * @parent_init_phase hold parent init_phase method
 */
typedef struct SystemResetDomainClass {
    ResetDomainClass parent_class;

    ResettableInitPhase parent_init_phase;
} SystemResetDomainClass;

/* reset/shutdown handler */

typedef struct QEMUResetEntry {
    QTAILQ_ENTRY(QEMUResetEntry) entry;
    QEMUResetHandler *func;
    void *opaque;
} QEMUResetEntry;

/**
 * SystemResetDomain:
 * @reset_handlers list of legacy reset handlers
 */
typedef struct SystemResetDomain {
    ResetDomain parent_obj;

    QTAILQ_HEAD(, QEMUResetEntry) reset_handlers;
} SystemResetDomain;

/* global/system reset domain */
static SystemResetDomain *reset_domain;

static SystemResetDomain *get_reset_domain(void)
{
    if (reset_domain == NULL) {
        Object *obj = object_new(TYPE_SYSTEM_RESET_DOMAIN);
        /*
         * the ref to the object will be deleted by
         * qemu_delete_system_reset_domain function below.
         */
        reset_domain = SYSTEM_RESET_DOMAIN(obj);
    }
    return reset_domain;
}

ResetDomain *qemu_get_system_reset_domain(void)
{
    return RESET_DOMAIN(get_reset_domain());
}

void qemu_delete_system_reset_domain(void)
{
    /* unref the reset_domain object if it exists */
    if (reset_domain != NULL) {
        object_unref(OBJECT(reset_domain));
        reset_domain = NULL;
    }
}

void qemu_register_system_reset_domain_object(Object *obj)
{
    ResetDomain *domain = RESET_DOMAIN(get_reset_domain());

    reset_domain_register_object(domain, obj);
}

void qemu_unregister_system_reset_domain_object(Object *obj)
{
    ResetDomain *domain = RESET_DOMAIN(get_reset_domain());

    reset_domain_unregister_object(domain, obj);
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    SystemResetDomain *domain = get_reset_domain();
    QEMUResetEntry *re = g_malloc0(sizeof(QEMUResetEntry));

    re->func = func;
    re->opaque = opaque;
    QTAILQ_INSERT_TAIL(&domain->reset_handlers, re, entry);
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
    SystemResetDomain *domain = get_reset_domain();
    QEMUResetEntry *re;

    QTAILQ_FOREACH(re, &domain->reset_handlers, entry) {
        if (re->func == func && re->opaque == opaque) {
            QTAILQ_REMOVE(&domain->reset_handlers, re, entry);
            g_free(re);
            return;
        }
    }
}

void qemu_devices_reset(void)
{
    qemu_system_reset_domain_reset(true);
}

void qemu_system_reset_domain_reset(bool cold)
{
    resettable_reset(OBJECT(get_reset_domain()), cold);
}

static void system_reset_domain_init_phase(Object *obj, bool cold)
{
    SystemResetDomain *domain = SYSTEM_RESET_DOMAIN(obj);
    SystemResetDomainClass *srdc = SYSTEM_RESET_DOMAIN_GET_CLASS(obj);
    QEMUResetEntry *re, *nre;

    /* call function handlers first */
    QTAILQ_FOREACH_SAFE(re, &domain->reset_handlers, entry, nre) {
        re->func(re->opaque);
    }

    /* then handle the objects in the ResetDomain */
    srdc->parent_init_phase(OBJECT(domain), cold);
}

static void system_reset_domain_init(Object *obj)
{
    SystemResetDomain *domain = SYSTEM_RESET_DOMAIN(obj);

    QTAILQ_INIT(&domain->reset_handlers);
}

static void system_reset_domain_finalize(Object *obj)
{
    SystemResetDomain *domain = SYSTEM_RESET_DOMAIN(obj);
    QEMUResetEntry *re, *nre;

    QTAILQ_FOREACH_SAFE(re, &domain->reset_handlers, entry, nre) {
        QTAILQ_REMOVE(&domain->reset_handlers, re, entry);
        g_free(re);
    }
}

static void system_reset_domain_class_init(ObjectClass *class, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(class);
    SystemResetDomainClass *srdc = SYSTEM_RESET_DOMAIN_CLASS(class);

    srdc->parent_init_phase = rc->phases.init;
    rc->phases.init = system_reset_domain_init_phase;
}

static const TypeInfo system_reset_domain_type_info = {
    .name = TYPE_SYSTEM_RESET_DOMAIN,
    .parent = TYPE_RESET_DOMAIN,
    .instance_size = sizeof(SystemResetDomain),
    .instance_init = system_reset_domain_init,
    .instance_finalize = system_reset_domain_finalize,
    .class_init = system_reset_domain_class_init,
    .class_size = sizeof(SystemResetDomainClass),
};

static void system_reset_register_types(void)
{
    type_register_static(&system_reset_domain_type_info);
}

type_init(system_reset_register_types)
