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

/*
 * s390_topology_new_cpu:
 * @cpu: a pointer to the new CPU
 *
 * The topology pointed by S390CPU, gives us the CPU topology
 * established by the -smp QEMU aruments.
 * The core-id is used to calculate the position of the CPU inside
 * the topology:
 *  - the socket, container TLE, containing the CPU, we have one socket
 *    for every nr_cpus (nr_cpus = smp.cores * smp.threads)
 *  - the CPU TLE inside the socket, we have potentionly up to 4 CPU TLE
 *    in a container TLE with the assumption that all CPU are identical
 *    with the same polarity and entitlement because we have maximum 256
 *    CPUs and each TLE can hold up to 64 identical CPUs.
 *  - the bit in the 64 bit CPU TLE core mask
 */
void s390_topology_new_cpu(S390CPU *cpu)
{
    S390Topology *topo = (S390Topology *)cpu->topology;
    int core_id = cpu->env.core_id;
    int bit, origin;
    int socket_id;

    socket_id = core_id / topo->nr_cpus;
    /*
     * At the core level, each CPU is represented by a bit in a 64bit
     * uint64_t which represent the presence of a CPU.
     * The firmware assume that all CPU in a CPU TLE have the same
     * type, polarization and are all dedicated or shared.
     * In that case the origin variable represents the offset of the first
     * CPU in the CPU container.
     * More than 64 CPUs per socket are represented in several CPU containers
     * inside the socket container.
     * The only reason to have several S390TopologyCores inside a socket is
     * to have more than 64 CPUs.
     * In that case the origin variable represents the offset of the first CPU
     * in the CPU container. More than 64 CPUs per socket are represented in
     * several CPU containers inside the socket container.
     */
    bit = core_id;
    origin = bit / 64;
    bit %= 64;
    bit = 63 - bit;

    topo->socket[socket_id].active_count++;
    set_bit(bit, &topo->socket[socket_id].mask[origin]);
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
    MachineState *ms = MACHINE(qdev_get_machine());
    S390Topology *topo = S390_CPU_TOPOLOGY(dev);

    topo->nr_cpus = ms->smp.cores * ms->smp.threads;
    topo->nr_sockets = ms->smp.sockets;
    topo->socket = g_new0(S390TopoSocket, topo->nr_sockets);
}

static Property s390_topology_properties[] = {
    DEFINE_PROP_UINT32("nr_cpus", S390Topology, nr_cpus, 1),
    DEFINE_PROP_UINT32("nr_sockets", S390Topology, nr_sockets, 1),
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
