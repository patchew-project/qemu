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
 *
 * .smp: keeps track of the machine topology.
 *
 * .socket: tracks information on the count of cores per socket.
 *
 */
S390Topology s390_topology = {
    .list = QTAILQ_HEAD_INITIALIZER(s390_topology.list),
    .sockets = NULL, /* will be initialized after the cpu model is realized */
};

/**
 * s390_socket_nb:
 * @id: s390_topology_id
 *
 * Returns the socket number used inside the socket array.
 */
static int s390_socket_nb(s390_topology_id id)
{
    return (id.socket + 1) * (id.book + 1) * (id.drawer + 1);
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
 * Allocate an array to keep the count of cores per socket.
 * The index of the array starts at socket 0 from book 0 and
 * drawer 0 up to the maximum allowed by the machine topology.
 */
static void s390_topology_init(MachineState *ms)
{
    CpuTopology *smp = &ms->smp;

    s390_topology.smp = smp;
    if (!s390_topology.sockets) {
        s390_topology.sockets = g_new0(uint8_t, smp->sockets *
                                       smp->books * smp->drawers);
    }
}

/**
 * s390_topology_from_cpu:
 * @cpu: The S390CPU
 *
 * Initialize the topology id from the CPU environment.
 */
static s390_topology_id s390_topology_from_cpu(S390CPU *cpu)
{
    s390_topology_id topology_id;

    topology_id.core = cpu->env.core_id;
    topology_id.type = cpu->env.cpu_type;
    topology_id.p = cpu->env.polarity;
    topology_id.d = cpu->env.dedicated;
    topology_id.socket = cpu->env.socket_id;
    topology_id.book = cpu->env.book_id;
    topology_id.drawer = cpu->env.drawer_id;

    return topology_id;
}

/**
 * s390_topology_set_polarity
 * @polarity: horizontal or vertical
 *
 * Changes the polarity of all the CPU in the configuration.
 *
 * If the dedicated CPU modifier attribute is set a vertical
 * polarization is always high (Architecture).
 * Otherwise we decide to set it as medium.
 *
 * Once done, advertise a topology change.
 */
void s390_topology_set_polarity(int polarity)
{
    S390TopologyEntry *entry;

    QTAILQ_FOREACH(entry, &s390_topology.list, next) {
        if (polarity == S390_TOPOLOGY_POLARITY_HORIZONTAL) {
            entry->id.p = polarity;
        } else {
            if (entry->id.d) {
                entry->id.p = S390_TOPOLOGY_POLARITY_VERTICAL_HIGH;
            } else {
                entry->id.p = S390_TOPOLOGY_POLARITY_VERTICAL_MEDIUM;
            }
        }
    }
    s390_cpu_topology_set();
}

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
        if (s390_topology.polarity == S390_TOPOLOGY_POLARITY_HORIZONTAL) {
            env->regs[r1] |= S390_PTF_REASON_DONE;
            setcc(cpu, 2);
        } else {
            s390_topology_set_polarity(S390_TOPOLOGY_POLARITY_HORIZONTAL);
            s390_topology.polarity = S390_TOPOLOGY_POLARITY_HORIZONTAL;
            setcc(cpu, 0);
        }
        break;
    case 1:    /* Vertical polarization is not supported */
        if (s390_topology.polarity != S390_TOPOLOGY_POLARITY_HORIZONTAL) {
            env->regs[r1] |= S390_PTF_REASON_DONE;
            setcc(cpu, 2);
        } else {
            s390_topology_set_polarity(S390_TOPOLOGY_POLARITY_VERTICAL_LOW);
            s390_topology.polarity = S390_TOPOLOGY_POLARITY_VERTICAL_LOW;
            setcc(cpu, 0);
        }
        break;
    default:
        /* Note that fc == 2 is interpreted by the SIE */
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }
}

 /**
 * s390_topology_set_entry:
 * @entry: Topology entry to setup
 * @id: topology id to use for the setup
 *
 * Set the core bit inside the topology mask and
 * increments the number of cores for the socket.
 */
static void s390_topology_set_entry(S390TopologyEntry *entry,
                                    s390_topology_id id)
{
    set_bit(63 - id.core, &entry->mask);
    s390_topology.sockets[s390_socket_nb(id)]++;
}

/**
 * s390_topology_new_entry:
 * @id: s390_topology_id to add
 *
 * Allocate a new entry and initialize it.
 *
 * returns the newly allocated entry.
 */
static S390TopologyEntry *s390_topology_new_entry(s390_topology_id id)
{
    S390TopologyEntry *entry;

    entry = g_malloc0(sizeof(S390TopologyEntry));
    entry->id.id = id.id & ~TOPO_CPU_MASK;
    s390_topology_set_entry(entry, id);

    return entry;
}

/**
 * s390_topology_insert:
 *
 * @id: s390_topology_id to insert.
 *
 * Parse the topology list to find if the entry already
 * exist and add the core in it.
 * If it does not exist, allocate a new entry and insert
 * it in the queue from lower id to greater id.
 */
static void s390_topology_insert(s390_topology_id id)
{
    S390TopologyEntry *entry;
    S390TopologyEntry *tmp = NULL;
    uint64_t new_id;

    new_id = id.id & ~TOPO_CPU_MASK;

    /* First CPU to add to an entry */
    if (QTAILQ_EMPTY(&s390_topology.list)) {
        entry = s390_topology_new_entry(id);
        QTAILQ_INSERT_HEAD(&s390_topology.list, entry, next);
        return;
    }

    QTAILQ_FOREACH(tmp, &s390_topology.list, next) {
        if (new_id == tmp->id.id) {
            s390_topology_set_entry(tmp, id);
            return;
        } else if (new_id < tmp->id.id) {
            entry = s390_topology_new_entry(id);
            QTAILQ_INSERT_BEFORE(tmp, entry, next);
            return;
        }
    }

    entry = s390_topology_new_entry(id);
    QTAILQ_INSERT_TAIL(&s390_topology.list, entry, next);
}

/**
 * s390_topology_check:
 * @errp: Error pointer
 * id: s390_topology_id to be verified
 *
 * The function checks if the topology id fits inside the
 * system topology.
 */
static void s390_topology_check(Error **errp, s390_topology_id id)
{
    CpuTopology *smp = s390_topology.smp;

    if (id.socket > smp->sockets) {
            error_setg(errp, "Unavailable socket: %d", id.socket);
            return;
    }
    if (id.book > smp->books) {
            error_setg(errp, "Unavailable book: %d", id.book);
            return;
    }
    if (id.drawer > smp->drawers) {
            error_setg(errp, "Unavailable drawer: %d", id.drawer);
            return;
    }
    if (id.type != S390_TOPOLOGY_CPU_IFL) {
            error_setg(errp, "Unknown cpu type: %d", id.type);
            return;
    }
    /* Polarity and dedication can never be wrong */
}

/**
 * s390_topology_cpu_default:
 * @errp: Error pointer
 * @cpu: pointer to a S390CPU
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
 */
static void s390_topology_cpu_default(Error **errp, S390CPU *cpu)
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
    Error *local_error = NULL;
    s390_topology_id id;

    /*
     * We do not want to initialize the topology if the cpu model
     * does not support topology consequently, we have to wait for
     * the first CPU to be realized, which realizes the CPU model
     * to initialize the topology structures.
     *
     * s390_topology_set_cpu() is called from the cpu hotplug.
     */
    if (!s390_topology.sockets) {
        s390_topology_init(ms);
    }

    s390_topology_cpu_default(&local_error, cpu);
    if (local_error) {
        error_propagate(errp, local_error);
        return;
    }

    id = s390_topology_from_cpu(cpu);

    /* Check for space on the socket */
    if (s390_topology.sockets[s390_socket_nb(id)] >=
        s390_topology.smp->sockets) {
        error_setg(&local_error, "No more space on socket");
        return;
    }

    s390_topology_check(&local_error, id);
    if (local_error) {
        error_propagate(errp, local_error);
        return;
    }

    s390_topology_insert(id);
}
