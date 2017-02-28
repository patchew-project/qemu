/*
 * QEMU Clock
 *
 *  Copyright (C) 2016 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Frederic Konrad <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/qemu-clock.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "qapi/error.h"

#ifndef DEBUG_QEMU_CLOCK
#define DEBUG_QEMU_CLOCK 0
#endif

#define DPRINTF(fmt, args...) do {                                           \
    if (DEBUG_QEMU_CLOCK) {                                                  \
        qemu_log("%s: " fmt, __func__, ## args);                             \
    }                                                                        \
} while (0);

void qemu_clk_refresh(QEMUClock *clk)
{
    qemu_clk_update_rate(clk, clk->ref_rate);
}

void qemu_clk_update_rate(QEMUClock *clk, uint64_t rate)
{
    ClkList *child;

    clk->ref_rate = rate;
    clk->rate = rate;

    if (clk->cb) {
        clk->rate = clk->cb(clk->opaque, rate);
    }

    DPRINTF("%s output rate updated to %" PRIu64 "\n",
            object_get_canonical_path(OBJECT(clk)),
            clk->rate);

    QLIST_FOREACH(child, &clk->bound, node) {
        qemu_clk_update_rate(child->clk, clk->rate);
    }
}

void qemu_clk_bind(QEMUClock *out, QEMUClock *in)
{
    ClkList *child;

    child = g_malloc(sizeof(child));
    assert(child);
    child->clk = in;
    object_ref(OBJECT(in));
    QLIST_INSERT_HEAD(&out->bound, child, node);
    qemu_clk_update_rate(in, out->rate);
}

void qemu_clk_unbind(QEMUClock *out, QEMUClock *in)
{
    ClkList *child, *next;

    QLIST_FOREACH_SAFE(child, &out->bound, node, next) {
        if (child->clk == in) {
            QLIST_REMOVE(child, node);
            g_free(child);
            object_unref(OBJECT(in));
        }
    }
}

void qemu_clk_set_callback(QEMUClock *clk,
                           QEMUClkRateUpdateCallback *cb,
                           void *opaque)
{
    clk->cb = cb;
    clk->opaque = opaque;
}

void qemu_clk_device_add_clock(DeviceState *dev, QEMUClock *clk,
                               const char *name)
{
    assert(name);
    assert(!clk->name);
    object_property_add_child(OBJECT(dev), name, OBJECT(clk), &error_abort);
    clk->name = g_strdup(name);
}

QEMUClock *qemu_clk_device_get_clock(DeviceState *dev, const char *name)
{
    gchar *path = NULL;
    Object *clk;
    bool ambiguous;

    path = g_strdup_printf("%s/%s", object_get_canonical_path(OBJECT(dev)),
                           name);
    clk = object_resolve_path(path, &ambiguous);
    g_free(path);
    return QEMU_CLOCK(clk);
}

static const TypeInfo qemu_clk_info = {
    .name          = TYPE_CLOCK,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(QEMUClock),
};

static void qemu_clk_register_types(void)
{
    type_register_static(&qemu_clk_info);
}

type_init(qemu_clk_register_types);
