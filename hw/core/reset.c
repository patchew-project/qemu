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
#include "sysemu/reset.h"
#include "hw/reset-domain.h"

/* reset/shutdown handler */

typedef struct QEMUResetEntry {
    QTAILQ_ENTRY(QEMUResetEntry) entry;
    QEMUResetHandler *func;
    void *opaque;
} QEMUResetEntry;

static QTAILQ_HEAD(, QEMUResetEntry) reset_handlers =
    QTAILQ_HEAD_INITIALIZER(reset_handlers);

/* global/system reset domain */
static ResetDomain *reset_domain;

static ResetDomain *get_reset_domain(void)
{
    if (reset_domain == NULL) {
        /*
         * the ref to the object will be deleted by
         * qemu_delete_system_reset_domain function below.
         */
        reset_domain = RESET_DOMAIN(object_new(TYPE_RESET_DOMAIN));
    }
    return reset_domain;
}

ResetDomain *qemu_get_system_reset_domain(void)
{
    return get_reset_domain();
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
    ResetDomain *domain = get_reset_domain();

    reset_domain_register_object(domain, obj);
}

void qemu_unregister_system_reset_domain_object(Object *obj)
{
    ResetDomain *domain = get_reset_domain();

    reset_domain_unregister_object(domain, obj);
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    QEMUResetEntry *re = g_malloc0(sizeof(QEMUResetEntry));

    re->func = func;
    re->opaque = opaque;
    QTAILQ_INSERT_TAIL(&reset_handlers, re, entry);
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
    QEMUResetEntry *re;

    QTAILQ_FOREACH(re, &reset_handlers, entry) {
        if (re->func == func && re->opaque == opaque) {
            QTAILQ_REMOVE(&reset_handlers, re, entry);
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
    ResetDomain *domain = get_reset_domain();
    QEMUResetEntry *re, *nre;

    /* call function handlers first */
    QTAILQ_FOREACH_SAFE(re, &reset_handlers, entry, nre) {
        re->func(re->opaque);
    }

    /* then handle the objects in the ResetDomain */
    resettable_reset(OBJECT(domain), cold);
}

