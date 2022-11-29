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
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "qemu/typedefs.h"
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/cpu-topology.h"

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
