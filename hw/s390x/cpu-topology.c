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

/*
 * s390_topology is used to keep the topology information.
 * .list: queue the topology entries inside which
 *        we keep the information on the CPU topology.
 * .socket: tracks information on the count of cores per socket.
 * .smp: keeps track of the machine topology.
 *
 */
S390Topology s390_topology = {
    /* will be initialized after the cpu model is realized */
    .cores_per_socket = NULL,
    .smp = NULL,
    .list = QTAILQ_HEAD_INITIALIZER(s390_topology.list),
    .polarity = POLARITY_HORIZONTAL,
};

/**
 * s390_socket_nb:
 * @cpu: s390x CPU
 *
 * Returns the socket number used inside the cores_per_socket array
 * for a cpu.
 */
int s390_socket_nb(S390CPU *cpu)
{
    return (cpu->env.drawer_id * s390_topology.smp->books + cpu->env.book_id) *
           s390_topology.smp->sockets + cpu->env.socket_id;
}

/**
 * s390_has_topology:
 *
 * Return value: if the topology is supported by the machine.
 */
bool s390_has_topology(void)
{
    return s390_has_feat(S390_FEAT_CONFIGURATION_TOPOLOGY);
}

/**
 * s390_topology_init:
 * @ms: the machine state where the machine topology is defined
 *
 * Keep track of the machine topology.
 *
 * Allocate an array to keep the count of cores per socket.
 * The index of the array starts at socket 0 from book 0 and
 * drawer 0 up to the maximum allowed by the machine topology.
 *
 * Insert a sentinel entry using unused level5 with its maximum value.
 * This entry will never be free.
 */
static void s390_topology_init(MachineState *ms)
{
    CpuTopology *smp = &ms->smp;
    S390TopologyEntry *entry;

    s390_topology.smp = smp;
    s390_topology.cores_per_socket = g_new0(uint8_t, smp->sockets *
                                            smp->books * smp->drawers);

    entry = g_malloc0(sizeof(S390TopologyEntry));
    entry->id.level5 = 0xff;
    QTAILQ_INSERT_HEAD(&s390_topology.list, entry, next);
}

/**
 * s390_topology_set_cpus_polarity:
 * @polarity: polarity requested by the caller
 *
 * Set all CPU entitlement according to polarity and
 * dedication.
 * Default vertical entitlement is POLARITY_VERTICAL_MEDIUM as
 * it does not require host modification of the CPU provisioning
 * until the host decide to modify individual CPU provisioning
 * using QAPI interface.
 * However a dedicated vCPU will have a POLARITY_VERTICAL_HIGH
 * entitlement.
 */
static void s390_topology_set_cpus_polarity(int polarity)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        if (polarity == POLARITY_HORIZONTAL) {
            S390_CPU(cs)->env.entitlement = 0;
        } else if (S390_CPU(cs)->env.dedicated) {
            S390_CPU(cs)->env.entitlement = POLARITY_VERTICAL_HIGH;
        } else {
            S390_CPU(cs)->env.entitlement = POLARITY_VERTICAL_MEDIUM;
        }
    }
}

/*
 * s390_handle_ptf:
 *
 * @register 1: contains the function code
 *
 * Function codes 0 (horizontal) and 1 (vertical) define the CPU
 * polarization requested by the guest.
 *
 * Verify that the polarization really need to change and call
 * s390_topology_set_cpus_polarity() specifying the requested polarity
 * to set for all CPUs.
 *
 * Function code 2 is handling topology changes and is interpreted
 * by the SIE.
 */
void s390_handle_ptf(S390CPU *cpu, uint8_t r1, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint64_t reg = env->regs[r1];
    int fc = reg & S390_TOPO_FC_MASK;

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
    case POLARITY_VERTICAL:
    case POLARITY_HORIZONTAL:
        if (s390_topology.polarity == fc) {
            env->regs[r1] |= S390_PTF_REASON_DONE;
            setcc(cpu, 2);
        } else {
            s390_topology.polarity = fc;
            s390_cpu_topology_set_modified();
            s390_topology_set_cpus_polarity(fc);
            setcc(cpu, 0);
        }
        break;
    default:
        /* Note that fc == 2 is interpreted by the SIE */
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }
}

/**
 * s390_topology_reset:
 *
 * Generic reset for CPU topology, calls s390_topology_reset()
 * s390_topology_reset() to reset the kernel Modified Topology
 * change record.
 * Then set global and all CPUs polarity to POLARITY_HORIZONTAL.
 */
void s390_topology_reset(void)
{
    s390_cpu_topology_reset();
    /* Set global polarity to POLARITY_HORIZONTAL */
    s390_topology.polarity = POLARITY_HORIZONTAL;
    /* Set all CPU polarity to POLARITY_HORIZONTAL */
    s390_topology_set_cpus_polarity(POLARITY_HORIZONTAL);
}

/**
 * s390_topology_cpu_default:
 * @cpu: pointer to a S390CPU
 * @errp: Error pointer
 *
 * Setup the default topology for unset attributes.
 *
 * The function accept only all all default values or all set values
 * for the geometry topology.
 *
 * The function calculates the (drawer_id, book_id, socket_id)
 * topology by filling the cores starting from the first socket
 * (0, 0, 0) up to the last (smp->drawers, smp->books, smp->sockets).
 *
 * CPU type, polarity and dedication have defaults values set in the
 * s390x_cpu_properties.
 */
static void s390_topology_cpu_default(S390CPU *cpu, Error **errp)
{
    CpuTopology *smp = s390_topology.smp;
    CPUS390XState *env = &cpu->env;

    /* All geometry topology attributes must be set or all unset */
    if ((env->socket_id < 0 || env->book_id < 0 || env->drawer_id < 0) &&
        (env->socket_id >= 0 || env->book_id >= 0 || env->drawer_id >= 0)) {
        error_setg(errp,
                   "Please define all or none of the topology geometry attributes");
        return;
    }

    /* Check if one of the geometry topology is unset */
    if (env->socket_id < 0) {
        /* Calculate default geometry topology attributes */
        env->socket_id = (env->core_id / smp->cores) % smp->sockets;
        env->book_id = (env->core_id / (smp->sockets * smp->cores)) %
                       smp->books;
        env->drawer_id = (env->core_id /
                          (smp->books * smp->sockets * smp->cores)) %
                         smp->drawers;
    }

    /*
     * Machine polarity is set inside the global s390_topology structure.
     * In the case the polarity is set as horizontal set the entitlement
     * to POLARITY_VERTICAL_MEDIUM which is the better equivalent when
     * machine polarity is set to vertical or POLARITY_VERTICAL_HIGH if
     * the vCPU is dedicated.
     */
    if (s390_topology.polarity && !env->entitlement) {
        if (env->dedicated) {
            env->entitlement = POLARITY_VERTICAL_HIGH;
        } else {
            env->entitlement = POLARITY_VERTICAL_MEDIUM;
        }
    }
}

/**
 * s390_topology_check:
 * @cpu: s390x CPU to be verified
 * @errp: Error pointer
 *
 * The function first setup default values and then checks if the cpu
 * fits inside the system topology.
 */
static void s390_topology_check(S390CPU *cpu, Error **errp)
{
    CpuTopology *smp = s390_topology.smp;
    ERRP_GUARD();

    s390_topology_cpu_default(cpu, errp);
    if (*errp) {
        return;
    }

    if (cpu->env.socket_id > smp->sockets) {
        error_setg(errp, "Unavailable socket: %d", cpu->env.socket_id);
        return;
    }
    if (cpu->env.book_id > smp->books) {
        error_setg(errp, "Unavailable book: %d", cpu->env.book_id);
        return;
    }
    if (cpu->env.drawer_id > smp->drawers) {
        error_setg(errp, "Unavailable drawer: %d", cpu->env.drawer_id);
        return;
    }
    if (cpu->env.entitlement >= POLARITY_MAX) {
        error_setg(errp, "Unknown polarity: %d", cpu->env.entitlement);
        return;
    }

    /* Dedication, boolean, can not be wrong. */
}

/**
 * s390_set_core_in_socket:
 * @cpu: the new S390CPU to insert in the topology structure
 * @drawer_id: new drawer_id
 * @book_id: new book_id
 * @socket_id: new socket_id
 * @creation: if is true the CPU is a new CPU and there is no old socket
 *            to handle.
 *            if is false, this is a moving the CPU and old socket count
 *            must be decremented.
 * @errp: the error pointer
 *
 */
static void s390_set_core_in_socket(S390CPU *cpu, int drawer_id, int book_id,
                                    int socket_id, bool creation, Error **errp)
{
    int old_socket = s390_socket_nb(cpu);
    int new_socket;

    if (creation) {
        new_socket = old_socket;
    } else {
        new_socket = drawer_id * s390_topology.smp->books +
                     book_id * s390_topology.smp->sockets +
                     socket_id;
    }

    /* Check for space on new socket */
    if ((new_socket != old_socket) &&
        (s390_topology.cores_per_socket[new_socket] >=
         s390_topology.smp->cores)) {
        error_setg(errp, "No more space on this socket");
        return;
    }

    /* Update the count of cores in sockets */
    s390_topology.cores_per_socket[new_socket] += 1;
    if (!creation) {
        s390_topology.cores_per_socket[old_socket] -= 1;
    }
}

/**
 * s390_update_cpu_props:
 * @ms: the machine state
 * @cpu: the CPU for which to update the properties from the environment.
 *
 */
static void s390_update_cpu_props(MachineState *ms, S390CPU *cpu)
{
    CpuInstanceProperties *props;

    props = &ms->possible_cpus->cpus[cpu->env.core_id].props;

    props->socket_id = cpu->env.socket_id;
    props->book_id = cpu->env.book_id;
    props->drawer_id = cpu->env.drawer_id;
}

/**
 * s390_topology_set_cpu:
 * @ms: MachineState used to initialize the topology structure on
 *      first call.
 * @cpu: the new S390CPU to insert in the topology structure
 * @errp: the error pointer
 *
 * Called from CPU Hotplug to check and setup the CPU attributes
 * before to insert the CPU in the topology.
 */
void s390_topology_set_cpu(MachineState *ms, S390CPU *cpu, Error **errp)
{
    ERRP_GUARD();

    /*
     * We do not want to initialize the topology if the cpu model
     * does not support topology consequently, we have to wait for
     * the first CPU to be realized, which realizes the CPU model
     * to initialize the topology structures.
     *
     * s390_topology_set_cpu() is called from the cpu hotplug.
     */
    if (!s390_topology.cores_per_socket) {
        s390_topology_init(ms);
    }

    s390_topology_check(cpu, errp);
    if (*errp) {
        return;
    }

    /* Set the CPU inside the socket */
    s390_set_core_in_socket(cpu, 0, 0, 0, true, errp);
    if (*errp) {
        return;
    }

    /* topology tree is reflected in props */
    s390_update_cpu_props(ms, cpu);
}
