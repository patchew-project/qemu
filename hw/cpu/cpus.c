/*
 * QEMU CPUs type
 *
 * Copyright (c) 2022 GreenSocs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "hw/cpu/cpus.h"
#include "hw/core/cpu.h"
#include "hw/resettable.h"
#include "sysemu/reset.h"

static QLIST_HEAD(, CpusState) clusters =
    QLIST_HEAD_INITIALIZER(clusters);

static Property cpus_properties[] = {
    DEFINE_PROP_STRING("cpu-type", CpusState, cpu_type),
    DEFINE_PROP_UINT16("num-cpus", CpusState, topology.cpus, 0),
    /*
     * Default behavior is to automatically compute a valid index.
     * FIXME: remove this property to keep it internal ?
     */
    DEFINE_PROP_INT32("cluster-id", CpusState, cluster_index, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void cpus_reset(Object *obj)
{
    CpusState *s = CPUS(obj);
    for (unsigned i = 0; i < s->topology.cpus; i++) {
        cpu_reset(s->cpus[i]);
    }
}

static void cpus_init(Object *obj)
{
    CpusState *s = CPUS(obj);

    /* may be overriden by subclasses or code to disable clustering */
    s->is_cluster = true;
}

void cpus_disable_clustering(CpusState *s)
{
    assert(!DEVICE(s)->realized);
    s->is_cluster = false;
}

static void cpus_create_cpus(CpusState *s, Error **errp)
{
    Error *err = NULL;
    CpusClass *cgc = CPUS_GET_CLASS(s);
    s->cpus = g_new0(CPUState *, s->topology.cpus);

    for (unsigned i = 0; i < s->topology.cpus; i++) {
        CPUState *cpu = CPU(object_new(s->cpu_type));
        s->cpus[i] = cpu;

        /* set child property and release the initial ref */
        object_property_add_child(OBJECT(s), "cpu[*]", OBJECT(cpu));
        object_unref(OBJECT(cpu));

        /* set index in case of cluster */
        if (s->is_cluster) {
            cpu->cluster_index = s->cluster_index;
        }

        /* let subclass configure the cpu */
        if (cgc->configure_cpu) {
            cgc->configure_cpu(s, cpu, i);
        }

        /* finally realize the cpu */
        qdev_realize(DEVICE(cpu), NULL, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }
}

static void cpus_realize(DeviceState *dev, Error **errp)
{
    CpusState *item, *s = CPUS(dev);
    CpusClass *cgc = CPUS_GET_CLASS(s);

    /* if subclass defined a base type, let's check it */
    if (cgc->base_cpu_type &&
        !object_class_dynamic_cast(object_class_by_name(s->cpu_type),
                                   cgc->base_cpu_type)) {
        error_setg(errp, "bad cpu-type '%s' (expected '%s')", s->cpu_type,
                   cgc->base_cpu_type);
        return;
    }

    if (s->topology.cpus == 0) {
        error_setg(errp, "num-cpus is zero");
        return;
    }

    if (s->is_cluster) {
        if (s->cluster_index < 0) {
            int32_t max = -1;
            QLIST_FOREACH(item, &clusters, cluster_node) {
                if (max < item->cluster_index) {
                    max = item->cluster_index;
                }
            }
            s->cluster_index = max + 1;
        } else {
            /*
             * Check the index is not already taken.
             */
            QLIST_FOREACH(item, &clusters, cluster_node) {
                if (s->cluster_index == item->cluster_index) {
                    error_setg(errp, "cluster index %d already exists",
                               s->cluster_index);
                    return;
                }
            }
        }

        if (s->cluster_index >= MAX_CLUSTERS) {
            error_setg(errp, "cluster index must be less than %d",
                       MAX_CLUSTERS);
            return;
        }

        /* Put the cpus in the inferior list */
        QLIST_INSERT_HEAD(&clusters, s, cluster_node);
    }

    /* create the cpus if needed */
    if (!cgc->skip_cpus_creation) {
        cpus_create_cpus(s, errp);
        qemu_register_reset(resettable_cold_reset_fn, s);
    }
}

static void cpus_finalize(Object *obj)
{
    CpusState *s = CPUS(obj);

    g_free(s->cpus);

    /* it may not be in the list */
    QLIST_SAFE_REMOVE(s, cluster_node);
}

static void cpus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, cpus_properties);
    dc->realize = cpus_realize;

    rc->phases.exit = cpus_reset;

    /*
     * Subclasses are expected to be user-creatable.
     * They may provide support to hotplug cpus, but they are
     * not expected to be hotpluggable themselves.
     */
    dc->hotpluggable = false;
}

static const TypeInfo cpus_type_info = {
    .name              = TYPE_CPUS,
    .parent            = TYPE_DEVICE,
    .abstract          = true,
    .instance_size     = sizeof(CpusState),
    .instance_init     = cpus_init,
    .instance_finalize = cpus_finalize,
    .class_size        = sizeof(CpusClass),
    .class_init        = cpus_class_init,
};

static void cpus_register_types(void)
{
    type_register_static(&cpus_type_info);
}

type_init(cpus_register_types)
