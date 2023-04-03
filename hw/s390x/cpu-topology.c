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
 * .cores_per_socket: tracks information on the count of cores
 *                    per socket.
 * .smp: keeps track of the machine topology.
 * .list: queue the topology entries inside which
 *        we keep the information on the CPU topology.
 * .polarization: the current subsystem polarization
 *
 */
S390Topology s390_topology = {
    /* will be initialized after the cpu model is realized */
    .cores_per_socket = NULL,
    .smp = NULL,
    .list = QTAILQ_HEAD_INITIALIZER(s390_topology.list),
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
    return false;
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
 * Insert a sentinel entry with a non null value.
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
    entry->id.sentinel = 0xff;
    QTAILQ_INSERT_HEAD(&s390_topology.list, entry, next);
}

/**
 * s390_topology_reset:
 *
 * Generic reset for CPU topology, calls s390_topology_reset()
 * to reset the kernel Modified Topology Change Record.
 */
void s390_topology_reset(void)
{
    s390_cpu_topology_set_changed(false);
}

/**
 * s390_topology_cpu_default:
 * @cpu: pointer to a S390CPU
 * @errp: Error pointer
 *
 * Setup the default topology if no attributes are already set.
 * Passing a CPU with some, but not all, attributes set is considered
 * an error.
 *
 * The function calculates the (drawer_id, book_id, socket_id)
 * topology by filling the cores starting from the first socket
 * (0, 0, 0) up to the last (smp->drawers, smp->books, smp->sockets).
 *
 * CPU type and dedication have defaults values set in the
 * s390x_cpu_properties, entitlement must be adjust depending on the
 * dedication.
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
        env->socket_id = s390_std_socket(env->core_id, smp);
        env->book_id = s390_std_book(env->core_id, smp);
        env->drawer_id = s390_std_drawer(env->core_id, smp);
    }

    /*
     * Even the user can specify the entitlement as horizontal on the
     * command line, qemu will only use env->entitlement during vertical
     * polarization.
     * Medium entitlement is chosen as the default entitlement when the CPU
     * is not dedicated.
     * A dedicated CPU always receives a high entitlement.
     */
    if (env->entitlement >= S390_CPU_ENTITLEMENT__MAX ||
        env->entitlement == S390_CPU_ENTITLEMENT_HORIZONTAL) {
        if (env->dedicated) {
            env->entitlement = S390_CPU_ENTITLEMENT_HIGH;
        } else {
            env->entitlement = S390_CPU_ENTITLEMENT_MEDIUM;
        }
    }
}

/**
 * s390_topology_check:
 * @socket_id: socket to check
 * @book_id: book to check
 * @drawer_id: drawer to check
 * @entitlement: entitlement to check
 * @dedicated: dedication to check
 * @errp: Error pointer
 *
 * The function checks if the topology
 * attributes fits inside the system topology.
 */
static void s390_topology_check(uint16_t socket_id, uint16_t book_id,
                                uint16_t drawer_id, uint16_t entitlement,
                                bool dedicated, Error **errp)
{
    CpuTopology *smp = s390_topology.smp;
    ERRP_GUARD();

    if (socket_id >= smp->sockets) {
        error_setg(errp, "Unavailable socket: %d", socket_id);
        return;
    }
    if (book_id >= smp->books) {
        error_setg(errp, "Unavailable book: %d", book_id);
        return;
    }
    if (drawer_id >= smp->drawers) {
        error_setg(errp, "Unavailable drawer: %d", drawer_id);
        return;
    }
    if (entitlement >= S390_CPU_ENTITLEMENT__MAX) {
        error_setg(errp, "Unknown entitlement: %d", entitlement);
        return;
    }
    if (dedicated && (entitlement == S390_CPU_ENTITLEMENT_LOW ||
                      entitlement == S390_CPU_ENTITLEMENT_MEDIUM)) {
        error_setg(errp, "A dedicated cpu implies high entitlement");
        return;
    }
}

/**
 * s390_topology_add_core_to_socket:
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
static void s390_topology_add_core_to_socket(S390CPU *cpu, int drawer_id,
                                             int book_id, int socket_id,
                                             bool creation, Error **errp)
{
    int old_socket_entry = s390_socket_nb(cpu);
    int new_socket_entry;

    if (creation) {
        new_socket_entry = old_socket_entry;
    } else {
        new_socket_entry = (drawer_id * s390_topology.smp->books + book_id) *
                            s390_topology.smp->sockets + socket_id;
    }

    /* Check for space on new socket */
    if ((new_socket_entry != old_socket_entry) &&
        (s390_topology.cores_per_socket[new_socket_entry] >=
         s390_topology.smp->cores)) {
        error_setg(errp, "No more space on this socket");
        return;
    }

    /* Update the count of cores in sockets */
    s390_topology.cores_per_socket[new_socket_entry] += 1;
    if (!creation) {
        s390_topology.cores_per_socket[old_socket_entry] -= 1;
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
 * s390_topology_setup_cpu:
 * @ms: MachineState used to initialize the topology structure on
 *      first call.
 * @cpu: the new S390CPU to insert in the topology structure
 * @errp: the error pointer
 *
 * Called from CPU Hotplug to check and setup the CPU attributes
 * before to insert the CPU in the topology.
 * There is no use to update the MTCR explicitely here because it
 * will be updated by KVM on creation of the new vCPU.
 */
void s390_topology_setup_cpu(MachineState *ms, S390CPU *cpu, Error **errp)
{
    ERRP_GUARD();

    /*
     * We do not want to initialize the topology if the cpu model
     * does not support topology, consequently, we have to wait for
     * the first CPU to be realized, which realizes the CPU model
     * to initialize the topology structures.
     *
     * s390_topology_setup_cpu() is called from the cpu hotplug.
     */
    if (!s390_topology.cores_per_socket) {
        s390_topology_init(ms);
    }

    s390_topology_cpu_default(cpu, errp);
    if (*errp) {
        return;
    }

    s390_topology_check(cpu->env.socket_id, cpu->env.book_id,
                        cpu->env.drawer_id, cpu->env.entitlement,
                        cpu->env.dedicated, errp);
    if (*errp) {
        return;
    }

    /* Set the CPU inside the socket */
    s390_topology_add_core_to_socket(cpu, 0, 0, 0, true, errp);
    if (*errp) {
        return;
    }

    /* topology tree is reflected in props */
    s390_update_cpu_props(ms, cpu);
}
