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
#include "hw/cpu/cpus.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/cutils.h"

static int add_cpu_to_cluster(Object *obj, void *opaque)
{
    CpusState *base = CPUS(opaque);
    CPUState *cpu = (CPUState *)object_dynamic_cast(obj, TYPE_CPU);

    if (cpu) {
        cpu->cluster_index = base->cluster_index;
        base->topology.cpus++;
    }
    return 0;
}

static void cpu_cluster_realize(DeviceState *dev, Error **errp)
{
    CPUClusterClass *ccc = CPU_CLUSTER_GET_CLASS(dev);
    CpusState *base = CPUS(dev);
    Object *cluster_obj = OBJECT(dev);

    /* This is a special legacy case */
    assert(base->topology.cpus == 0);
    assert(base->cpu_type == NULL);
    assert(base->is_cluster);

    /* Iterate through all our CPU children and set their cluster_index */
    object_child_foreach_recursive(cluster_obj, add_cpu_to_cluster, base);

    /*
     * A cluster with no CPUs is a bug in the board/SoC code that created it;
     * if you hit this during development of new code, check that you have
     * created the CPUs and parented them into the cluster object before
     * realizing the cluster object.
     */
    assert(base->topology.cpus > 0);

    /* realize base class (will set cluster field to true) */
    ccc->parent_realize(dev, errp);
}

static void cpu_cluster_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CPUClusterClass *ccc = CPU_CLUSTER_CLASS(klass);
    CpusClass *cc = CPUS_CLASS(klass);

    device_class_set_parent_realize(dc, cpu_cluster_realize,
                                    &ccc->parent_realize);

    /* This is not directly for users, CPU children must be attached by code */
    dc->user_creatable = false;

    /* Cpus are created by external code */
    cc->skip_cpus_creation = true;
}

static const TypeInfo cpu_cluster_type_info = {
    .name = TYPE_CPU_CLUSTER,
    .parent = TYPE_CPUS,
    .instance_size = sizeof(CPUClusterState),
    .class_size = sizeof(CPUClusterClass),
    .class_init = cpu_cluster_class_init,
};

static void cpu_cluster_register_types(void)
{
    type_register_static(&cpu_cluster_type_info);
}

type_init(cpu_cluster_register_types)
