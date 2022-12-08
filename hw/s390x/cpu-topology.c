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
#include "migration/vmstate.h"
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"

/*
 * s390_handle_ptf:
 *
 * @register 1: contains the function code
 *
 * Function codes 0 and 1 handle the CPU polarization.
 * We assume an horizontal topology, the only one supported currently
 * by Linux, consequently we answer to function code 0, requesting
 * horizontal polarization that it is already the current polarization
 * and reject vertical polarization request without further explanation.
 *
 * Function code 2 is handling topology changes and is interpreted
 * by the SIE.
 */
void s390_handle_ptf(S390CPU *cpu, uint8_t r1, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint64_t reg = env->regs[r1];
    uint8_t fc = reg & S390_TOPO_FC_MASK;

    if (!s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY)) {
        s390_program_interrupt(env, PGM_OPERATION, ra);
        return;
    }

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return;
    }

    if (reg & ~S390_TOPO_FC_MASK) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return;
    }

    switch (fc) {
    case 0:    /* Horizontal polarization is already set */
        env->regs[r1] |= S390_PTF_REASON_DONE;
        setcc(cpu, 2);
        break;
    case 1:    /* Vertical polarization is not supported */
        env->regs[r1] |= S390_PTF_REASON_NONE;
        setcc(cpu, 2);
        break;
    default:
        /* Note that fc == 2 is interpreted by the SIE */
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }
}

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
    int ret;

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
 * cpu_topology_needed:
 * @opaque: The pointer to the S390Topology
 *
 * We always need to know if source and destination use the topology.
 */
static bool cpu_topology_needed(void *opaque)
{
    return s390_has_topology();
}

const VMStateDescription vmstate_cpu_topology = {
    .name = "cpu_topology",
    .version_id = 1,
    .post_load = cpu_topology_postload,
    .minimum_version_id = 1,
    .needed = cpu_topology_needed,
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
