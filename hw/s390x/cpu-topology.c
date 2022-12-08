/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>

 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "qemu/typedefs.h"
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/cpu-topology.h"

/**
 * s390_has_topology
 *
 * Return false until the commit activating the topology is
 * commited.
 */
bool s390_has_topology(void)
{
    return false;
}

/**
 * s390_get_topology
 *
 * Returns a pointer to the topology.
 *
 * This function is called when we know the topology exist.
 * Testing if the topology exist is done with s390_has_topology()
 */
S390Topology *s390_get_topology(void)
{
    static S390Topology *s390Topology;

    if (!s390Topology) {
        s390Topology = S390_CPU_TOPOLOGY(
            object_resolve_path(TYPE_S390_CPU_TOPOLOGY, NULL));
    }

    assert(s390Topology);

    return s390Topology;
}

/**
 * s390_init_topology
 * @machine: The Machine state, used to retrieve the SMP parameters
 * @errp: the error pointer in case of problem
 *
 * This function creates and initialize the S390Topology with
 * the QEMU -smp parameters we will use during adding cores to the
 * topology.
 */
void s390_init_topology(MachineState *machine, Error **errp)
{
    DeviceState *dev;

    if (machine->smp.threads > 1) {
        error_setg(errp, "CPU Topology do not support multithreading");
        return;
    }

    dev = qdev_new(TYPE_S390_CPU_TOPOLOGY);

    object_property_add_child(&machine->parent_obj,
                              TYPE_S390_CPU_TOPOLOGY, OBJECT(dev));
    object_property_set_int(OBJECT(dev), "num-cores",
                            machine->smp.cores, errp);
    object_property_set_int(OBJECT(dev), "num-sockets",
                            machine->smp.sockets, errp);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp);
}

/**
 * s390_topology_realize:
 * @dev: the device state
 *
 * We free the socket array allocated in realize.
 */
static void s390_topology_unrealize(DeviceState *dev)
{
    S390Topology *topo = S390_CPU_TOPOLOGY(dev);

    g_free(topo->socket);
}

/**
 * s390_topology_realize:
 * @dev: the device state
 * @errp: the error pointer (not used)
 *
 * During realize the machine CPU topology is initialized with the
 * QEMU -smp parameters.
 * The maximum count of CPU TLE in the all Topology can not be greater
 * than the maximum CPUs.
 */
static void s390_topology_realize(DeviceState *dev, Error **errp)
{
    S390Topology *topo = S390_CPU_TOPOLOGY(dev);

    topo->socket = g_new0(S390TopoSocket, topo->num_sockets);
}

static Property s390_topology_properties[] = {
    DEFINE_PROP_UINT32("num-cores", S390Topology, num_cores, 1),
    DEFINE_PROP_UINT32("num-sockets", S390Topology, num_sockets, 1),
    DEFINE_PROP_END_OF_LIST(),
};

/**
 * s390_topology_reset:
 * @dev: the device
 *
 * Calls the sysemu topology reset
 */
static void s390_topology_reset(DeviceState *dev)
{
    s390_cpu_topology_reset();
}

/**
 * topology_class_init:
 * @oc: Object class
 * @data: (not used)
 *
 * A very simple object we will need for reset and migration.
 */
static void topology_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = s390_topology_realize;
    dc->unrealize = s390_topology_unrealize;
    device_class_set_props(dc, s390_topology_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = s390_topology_reset;
}

static const TypeInfo cpu_topology_info = {
    .name          = TYPE_S390_CPU_TOPOLOGY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390Topology),
    .class_init    = topology_class_init,
};

static void topology_register(void)
{
    type_register_static(&cpu_topology_info);
}
type_init(topology_register);
