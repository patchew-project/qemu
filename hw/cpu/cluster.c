/*
 * QEMU CPU cluster
 *
 * Copyright (c) 2018 GreenSocs SAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "hw/cpu/cluster.h"
#include "qom/cpu.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/cutils.h"

static Property cpu_cluster_properties[] = {
    DEFINE_PROP_UINT32("cluster-id", CPUClusterState, cluster_id, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void cpu_cluster_realize(DeviceState *dev, Error **errp)
{
    /* Iterate through all our CPU children and set their cluster_index */
    CPUClusterState *cluster = CPU_CLUSTER(dev);
    ObjectPropertyIterator iter;
    ObjectProperty *prop;
    Object *cluster_obj = OBJECT(dev);

    if (cluster->cluster_id >= MAX_CLUSTERS) {
        error_setg(errp, "cluster-id must be less than %d", MAX_CLUSTERS);
        return;
    }

    object_property_iter_init(&iter, cluster_obj);
    while ((prop = object_property_iter_next(&iter))) {
        Object *cpu_obj;
        CPUState *cpu;

        if (!strstart(prop->type, "child<", NULL)) {
            continue;
        }
        cpu_obj = object_property_get_link(cluster_obj, prop->name, NULL);
        cpu = (CPUState *)object_dynamic_cast(cpu_obj, TYPE_CPU);
        if (!cpu) {
            continue;
        }
        cpu->cluster_index = cluster->cluster_id;
    }
}

static void cpu_cluster_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = cpu_cluster_properties;
    dc->realize = cpu_cluster_realize;
}

static const TypeInfo cpu_cluster_type_info = {
    .name = TYPE_CPU_CLUSTER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CPUClusterState),
    .class_init = cpu_cluster_class_init,
};

static void cpu_cluster_register_types(void)
{
    type_register_static(&cpu_cluster_type_info);
}

type_init(cpu_cluster_register_types)
