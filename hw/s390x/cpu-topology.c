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
#include "migration/vmstate.h"

S390Topology *s390_get_topology(void)
{
    static S390Topology *s390Topology;

    if (!s390Topology) {
        s390Topology = S390_CPU_TOPOLOGY(
            object_resolve_path(TYPE_S390_CPU_TOPOLOGY, NULL));
    }

    return s390Topology;
}

/*
 * s390_topology_new_cpu:
 * @core_id: the core ID is machine wide
 *
 * The topology returned by s390_get_topology(), gives us the CPU
 * topology established by the -smp QEMU aruments.
 * The core-id gives:
 *  - the Container TLE (Topology List Entry) containing the CPU TLE.
 *  - in the CPU TLE the origin, or offset of the first bit in the core mask
 *  - the bit in the CPU TLE core mask
 */
void s390_topology_new_cpu(int core_id)
{
    S390Topology *topo = s390_get_topology();
    int socket_id;
    int bit, origin;

    /* In the case no Topology is used nothing is to be done here */
    if (!topo) {
        return;
    }

    /*
     * At the core level, each CPU is represented by a bit in a 64bit
     * unsigned long which represent the presence of a CPU.
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

    qemu_mutex_lock(&topo->topo_mutex);

    socket_id = core_id / topo->cpus;
    topo->socket[socket_id].active_count++;
    set_bit(bit, &topo->tle[socket_id].mask[origin]);

    qemu_mutex_unlock(&topo->topo_mutex);
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

    topo->cpus = ms->smp.cores * ms->smp.threads;

    topo->socket = g_new0(S390TopoContainer, ms->smp.sockets);
    topo->tle = g_new0(S390TopoTLE, ms->smp.max_cpus);

    topo->ms = ms;
    qemu_mutex_init(&topo->topo_mutex);
}

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
 * cpu_topology_postload
 * @opaque: a pointer to the S390Topology
 * @version_id: version identifier
 *
 * We check that the topology is used or is not used
 * on both side identically.
 *
 * If the topology is in use we set the Modified Topology Change Report
 * on the destination host.
 */
static int cpu_topology_postload(void *opaque, int version_id)
{
    S390Topology *topo = opaque;
    int ret;

    if (topo->topology_needed != s390_has_topology()) {
        if (topo->topology_needed) {
            error_report("Topology facility is needed in destination");
        } else {
            error_report("Topology facility can not be used in destination");
        }
        return -EINVAL;
    }

    /* We do not support CPU Topology, all is good */
    if (!s390_has_topology()) {
        return 0;
    }

    /* We support CPU Topology, set the MTCR unconditionally */
    ret = s390_cpu_topology_mtcr_set();
    if (ret) {
        error_report("Failed to set MTCR: %s", strerror(-ret));
    }
    return ret;
}

/**
 * cpu_topology_presave:
 * @opaque: The pointer to the S390Topology
 *
 * Save the usage of the CPU Topology in the VM State.
 */
static int cpu_topology_presave(void *opaque)
{
    S390Topology *topo = opaque;

    topo->topology_needed = s390_has_topology();
    return 0;
}

/**
 * cpu_topology_needed:
 * @opaque: The pointer to the S390Topology
 *
 * We always need to know if source and destination use the topology.
 */
static bool cpu_topology_needed(void *opaque)
{
    return true;
}


const VMStateDescription vmstate_cpu_topology = {
    .name = "cpu_topology",
    .version_id = 1,
    .post_load = cpu_topology_postload,
    .pre_save = cpu_topology_presave,
    .minimum_version_id = 1,
    .needed = cpu_topology_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(topology_needed, S390Topology),
        VMSTATE_END_OF_LIST()
    }
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
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = s390_topology_reset;
    dc->vmsd = &vmstate_cpu_topology;
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
