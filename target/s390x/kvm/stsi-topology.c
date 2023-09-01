/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU S390x CPU Topology
 *
 * Copyright IBM Corp. 2022, 2023
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/cpu-topology.h"

QEMU_BUILD_BUG_ON(S390_CPU_ENTITLEMENT_LOW != 1);
QEMU_BUILD_BUG_ON(S390_CPU_ENTITLEMENT_MEDIUM != 2);
QEMU_BUILD_BUG_ON(S390_CPU_ENTITLEMENT_HIGH != 3);

/**
 * fill_container:
 * @p: The address of the container TLE to fill
 * @level: The level of nesting for this container
 * @id: The container receives a unique ID inside its own container
 *
 * Returns the next free TLE entry.
 */
static char *fill_container(char *p, int level, int id)
{
    SYSIBContainerListEntry *tle = (SYSIBContainerListEntry *)p;

    tle->nl = level;
    tle->id = id;
    return p + sizeof(*tle);
}

/**
 * fill_tle_cpu:
 * @p: The address of the CPU TLE to fill
 * @entry: a pointer to the S390TopologyEntry defining this
 *         CPU container.
 *
 * Returns the next free TLE entry.
 */
static char *fill_tle_cpu(char *p, S390TopologyEntry *entry)
{
    SysIBCPUListEntry *tle = (SysIBCPUListEntry *)p;
    s390_topology_id topology_id = entry->id;

    tle->nl = 0;
    tle->flags = 3 - topology_id.inv_polarization;
    if (!topology_id.not_dedicated) {
        tle->flags |= SYSIB_TLE_DEDICATED;
    }
    tle->type = topology_id.type;
    tle->origin = cpu_to_be16(topology_id.origin * 64);
    tle->mask = cpu_to_be64(entry->mask);
    return p + sizeof(*tle);
}

/*
 * Macro to check that the size of data after increment
 * will not get bigger than the size of the SysIB.
 */
#define SYSIB_GUARD(data, x) do {       \
        data += x;                      \
        if (data > sizeof(SysIB)) {     \
            return 0;                   \
        }                               \
    } while (0)

/**
 * stsi_topology_fill_sysib:
 * @p: A pointer to the position of the first TLE
 * @level: The nested level wanted by the guest
 *
 * Fill the SYSIB with the topology information as described in
 * the PoP, nesting containers as appropriate, with the maximum
 * nesting limited by @level.
 *
 * Return value:
 * On success: the size of the SysIB_15x after being filled with TLE.
 * On error: 0 in the case we would overrun the end of the SysIB.
 */
static int stsi_topology_fill_sysib(S390TopologyList *topology_list,
                                    char *p, int level)
{
    S390TopologyEntry *entry;
    int last_drawer = -1;
    int last_book = -1;
    int last_socket = -1;
    int drawer_id = 0;
    int book_id = 0;
    int socket_id = 0;
    int n = sizeof(SysIB_151x);

    QTAILQ_FOREACH(entry, topology_list, next) {
        bool drawer_change = last_drawer != entry->id.drawer;
        bool book_change = drawer_change || last_book != entry->id.book;
        bool socket_change = book_change || last_socket != entry->id.socket;

        if (level > 3 && drawer_change) {
            SYSIB_GUARD(n, sizeof(SYSIBContainerListEntry));
            p = fill_container(p, 3, drawer_id++);
            book_id = 0;
        }
        if (level > 2 && book_change) {
            SYSIB_GUARD(n, sizeof(SYSIBContainerListEntry));
            p = fill_container(p, 2, book_id++);
            socket_id = 0;
        }
        if (socket_change) {
            SYSIB_GUARD(n, sizeof(SYSIBContainerListEntry));
            p = fill_container(p, 1, socket_id++);
        }

        SYSIB_GUARD(n, sizeof(SysIBCPUListEntry));
        p = fill_tle_cpu(p, entry);
        last_drawer = entry->id.drawer;
        last_book = entry->id.book;
        last_socket = entry->id.socket;
    }

    return n;
}

/**
 * setup_stsi:
 * sysib: pointer to a SysIB to be filled with SysIB_151x data
 * level: Nested level specified by the guest
 *
 * Setup the SYSIB for STSI 15.1, the header as well as the description
 * of the topology.
 */
static int setup_stsi(S390TopologyList *topology_list, SysIB_151x *sysib,
                      int level)
{
    sysib->mnest = level;
    switch (level) {
    case 4:
        sysib->mag[S390_TOPOLOGY_MAG4] = current_machine->smp.drawers;
        sysib->mag[S390_TOPOLOGY_MAG3] = current_machine->smp.books;
        sysib->mag[S390_TOPOLOGY_MAG2] = current_machine->smp.sockets;
        sysib->mag[S390_TOPOLOGY_MAG1] = current_machine->smp.cores;
        break;
    case 3:
        sysib->mag[S390_TOPOLOGY_MAG3] = current_machine->smp.drawers *
                                         current_machine->smp.books;
        sysib->mag[S390_TOPOLOGY_MAG2] = current_machine->smp.sockets;
        sysib->mag[S390_TOPOLOGY_MAG1] = current_machine->smp.cores;
        break;
    case 2:
        sysib->mag[S390_TOPOLOGY_MAG2] = current_machine->smp.drawers *
                                         current_machine->smp.books *
                                         current_machine->smp.sockets;
        sysib->mag[S390_TOPOLOGY_MAG1] = current_machine->smp.cores;
        break;
    }

    return stsi_topology_fill_sysib(topology_list, sysib->tle, level);
}

/**
 * s390_topology_add_cpu_to_entry:
 * @entry: Topology entry to setup
 * @cpu: the S390CPU to add
 *
 * Set the core bit inside the topology mask.
 */
static void s390_topology_add_cpu_to_entry(S390TopologyEntry *entry,
                                           S390CPU *cpu)
{
    set_bit(63 - (cpu->env.core_id % 64), &entry->mask);
}

/**
 * s390_topology_from_cpu:
 * @cpu: S390CPU to calculate the topology id
 *
 * Initialize the topology id from the CPU environment.
 */
static s390_topology_id s390_topology_from_cpu(S390CPU *cpu)
{
    s390_topology_id topology_id = {0};

    topology_id.drawer = cpu->env.drawer_id;
    topology_id.book = cpu->env.book_id;
    topology_id.socket = cpu->env.socket_id;
    topology_id.type = S390_TOPOLOGY_CPU_IFL;
    topology_id.not_dedicated = !cpu->env.dedicated;

    topology_id.inv_polarization = 3;
    if (s390_topology.polarization == S390_CPU_POLARIZATION_VERTICAL) {
        topology_id.inv_polarization -= cpu->env.entitlement;
    }

    topology_id.origin = cpu->env.core_id / 64;

    return topology_id;
}

/**
 * s390_topology_fill_list_sorted:
 *
 * Loop over all CPU and insert it at the right place
 * inside the TLE entry list.
 * Fill the S390Topology list with entries according to the order
 * specified by the PoP.
 */
static void s390_topology_fill_list_sorted(S390TopologyList *topology_list)
{
    CPUState *cs;
    S390TopologyEntry sentinel;

    QTAILQ_INIT(topology_list);

    sentinel.id.id = cpu_to_be64(UINT64_MAX);
    QTAILQ_INSERT_HEAD(topology_list, &sentinel, next);

    CPU_FOREACH(cs) {
        s390_topology_id id = s390_topology_from_cpu(S390_CPU(cs));
        S390TopologyEntry *entry, *tmp;

        QTAILQ_FOREACH(tmp, topology_list, next) {
            if (id.id == tmp->id.id) {
                entry = tmp;
                break;
            } else if (be64_to_cpu(id.id) < be64_to_cpu(tmp->id.id)) {
                entry = g_malloc0(sizeof(*entry));
                entry->id.id = id.id;
                QTAILQ_INSERT_BEFORE(tmp, entry, next);
                break;
            }
        }
        s390_topology_add_cpu_to_entry(entry, S390_CPU(cs));
    }

    QTAILQ_REMOVE(topology_list, &sentinel, next);
}

/**
 * s390_topology_empty_list:
 *
 * Clear all entries in the S390Topology list.
 */
static void s390_topology_empty_list(S390TopologyList *topology_list)
{
    S390TopologyEntry *entry = NULL;
    S390TopologyEntry *tmp = NULL;

    QTAILQ_FOREACH_SAFE(entry, topology_list, next, tmp) {
        QTAILQ_REMOVE(topology_list, entry, next);
        g_free(entry);
    }
}

/**
 * insert_stsi_15_1_x:
 * cpu: the CPU doing the call for which we set CC
 * sel2: the selector 2, containing the nested level
 * addr: Guest logical address of the guest SysIB
 * ar: the access register number
 *
 * Emulate STSI 15.1.x, that is, perform all necessary checks and
 * fill the SYSIB.
 * In case the topology description is too long to fit into the SYSIB,
 * set CC=3 and abort without writing the SYSIB.
 */
void insert_stsi_15_1_x(S390CPU *cpu, int sel2, uint64_t addr, uint8_t ar, uintptr_t ra)
{
    S390TopologyList topology_list;
    SysIB sysib = {0};
    int length;

    if (!s390_has_topology() || sel2 < 2 || sel2 > SCLP_READ_SCP_INFO_MNEST) {
        setcc(cpu, 3);
        return;
    }

    s390_topology_fill_list_sorted(&topology_list);

    length = setup_stsi(&topology_list, &sysib.sysib_151x, sel2);

    if (!length) {
        s390_topology_empty_list(&topology_list);
        setcc(cpu, 3);
        return;
    }

    sysib.sysib_151x.length = cpu_to_be16(length);
    if (!s390_cpu_virt_mem_write(cpu, addr, ar, &sysib, length)) {
        setcc(cpu, 0);
    } else {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
    }

    s390_topology_empty_list(&topology_list);
}
