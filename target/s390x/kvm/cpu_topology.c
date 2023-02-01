/*
 * QEMU S390x CPU Topology
 *
 * Copyright IBM Corp. 2022
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/s390x/pv.h"
#include "hw/sysbus.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/cpu-topology.h"

/**
 * fill_container:
 * @p: The address of the container TLE to fill
 * @level: The level of nesting for this container
 * @id: The container receives a uniq ID inside its own container
 *
 * Returns the next free TLE entry.
 */
static char *fill_container(char *p, int level, int id)
{
    SysIBTl_container *tle = (SysIBTl_container *)p;

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
    SysIBTl_cpu *tle = (SysIBTl_cpu *)p;
    s390_topology_id topology_id = entry->id;

    tle->nl = 0;
    if (topology_id.dedicated) {
        tle->entitlement = SYSIB_TLE_DEDICATED;
    }
    tle->entitlement |= topology_id.polarity;
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
        if (data  > sizeof(SysIB)) {    \
            return -ENOSPC;             \
        }                               \
    } while (0)

/**
 * stsi_set_tle:
 * @p: A pointer to the position of the first TLE
 * @level: The nested level wanted by the guest
 *
 * Loop inside the s390_topology.list until the sentinelle entry
 * is found and for each entry:
 *   - Check using SYSIB_GUARD() that the size of the SysIB is not
 *     reached.
 *   - Add all the container TLE needed for the level
 *   - Add the CPU TLE.
 *
 * Return value:
 * s390_top_set_level returns the size of the SysIB_15x after being
 * filled with TLE on success.
 * It returns -ENOSPC in the case we would overrun the end of the SysIB.
 */
static int stsi_set_tle(char *p, int level)
{
    S390TopologyEntry *entry;
    int last_drawer = -1;
    int last_book = -1;
    int last_socket = -1;
    int drawer_id = 0;
    int book_id = 0;
    int socket_id = 0;
    int n = sizeof(SysIB_151x);

    QTAILQ_FOREACH(entry, &s390_topology.list, next) {
        int current_drawer = entry->id.drawer;
        int current_book = entry->id.book;
        int current_socket = entry->id.socket;
        bool drawer_change = last_drawer != current_drawer;
        bool book_change = drawer_change || last_book != current_book;
        bool socket_change = book_change || last_socket != current_socket;

        /* If we reach the guard get out */
        if (entry->id.level5) {
            break;
        }

        if (level > 3 && drawer_change) {
            SYSIB_GUARD(n, sizeof(SysIBTl_container));
            p = fill_container(p, 3, drawer_id++);
            book_id = 0;
        }
        if (level > 2 && book_change) {
            SYSIB_GUARD(n, sizeof(SysIBTl_container));
            p = fill_container(p, 2, book_id++);
            socket_id = 0;
        }
        if (socket_change) {
            SYSIB_GUARD(n, sizeof(SysIBTl_container));
            p = fill_container(p, 1, socket_id++);
        }

        SYSIB_GUARD(n, sizeof(SysIBTl_cpu));
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
 * Setup the SysIB_151x header before calling stsi_set_tle with
 * a pointer to the first TLE entry.
 */
static int setup_stsi(SysIB_151x *sysib, int level)
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

    return stsi_set_tle(sysib->tle, level);
}

/**
 * s390_topology_add_cpu_to_entry:
 * @entry: Topology entry to setup
 * @cpu: the S390CPU to add
 *
 * Set the core bit inside the topology mask and
 * increments the number of cores for the socket.
 */
static void s390_topology_add_cpu_to_entry(S390TopologyEntry *entry,
                                           S390CPU *cpu)
{
    set_bit(63 - (cpu->env.core_id % 64), &entry->mask);
}

/**
 * s390_topology_new_entry:
 * @id: s390_topology_id to add
 * @cpu: the S390CPU to add
 *
 * Allocate a new entry and initialize it.
 *
 * returns the newly allocated entry.
 */
static S390TopologyEntry *s390_topology_new_entry(s390_topology_id id,
                                                  S390CPU *cpu)
{
    S390TopologyEntry *entry;

    entry = g_malloc0(sizeof(S390TopologyEntry));
    entry->id.id = id.id;
    s390_topology_add_cpu_to_entry(entry, cpu);

    return entry;
}

/**
 * s390_topology_from_cpu:
 * @cpu: The S390CPU
 *
 * Initialize the topology id from the CPU environment.
 */
static s390_topology_id s390_topology_from_cpu(S390CPU *cpu)
{
    s390_topology_id topology_id = {0};

    topology_id.drawer = cpu->env.drawer_id;
    topology_id.book = cpu->env.book_id;
    topology_id.socket = cpu->env.socket_id;
    topology_id.origin = cpu->env.core_id / 64;
    topology_id.type = S390_TOPOLOGY_CPU_IFL;
    topology_id.dedicated = cpu->env.dedicated;

    if (s390_topology.polarity == POLARITY_VERTICAL) {
        /*
         * Vertical polarity with dedicated CPU implies
         * vertical high entitlement.
         */
        if (topology_id.dedicated) {
            topology_id.polarity |= POLARITY_VERTICAL_HIGH;
        } else {
            topology_id.polarity |= cpu->env.entitlement;
        }
    }

    return topology_id;
}

/**
 * s390_topology_insert:
 * @cpu: s390CPU insert.
 *
 * Parse the topology list to find if the entry already
 * exist and add the core in it.
 * If it does not exist, allocate a new entry and insert
 * it in the queue from lower id to greater id.
 */
static void s390_topology_insert(S390CPU *cpu)
{
    s390_topology_id id = s390_topology_from_cpu(cpu);
    S390TopologyEntry *entry = NULL;
    S390TopologyEntry *tmp = NULL;

    QTAILQ_FOREACH(tmp, &s390_topology.list, next) {
        if (id.id == tmp->id.id) {
            s390_topology_add_cpu_to_entry(tmp, cpu);
            return;
        } else if (id.id < tmp->id.id) {
            entry = s390_topology_new_entry(id, cpu);
            QTAILQ_INSERT_BEFORE(tmp, entry, next);
            return;
        }
    }
}

/**
 * s390_order_tle:
 *
 * Loop over all CPU and insert it at the right place
 * inside the TLE entry list.
 */
static void s390_order_tle(void)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        s390_topology_insert(S390_CPU(cs));
    }
}

/**
 * s390_free_tle:
 *
 * Loop over all TLE entries and free them.
 * Keep the sentinelle which is the only one with level5 != 0
 */
static void s390_free_tle(void)
{
    S390TopologyEntry *entry = NULL;
    S390TopologyEntry *tmp = NULL;

    QTAILQ_FOREACH_SAFE(entry, &s390_topology.list, next, tmp) {
        if (!entry->id.level5) {
            QTAILQ_REMOVE(&s390_topology.list, entry, next);
            g_free(entry);
        }
    }
}

/**
 * insert_stsi_15_1_x:
 * cpu: the CPU doing the call for which we set CC
 * sel2: the selector 2, containing the nested level
 * addr: Guest logical address of the guest SysIB
 * ar: the access register number
 *
 * Reserve a zeroed SysIB, let setup_stsi to fill it and
 * copy the SysIB to the guest memory.
 *
 * In case of overflow set CC(3) and no copy is done.
 */
void insert_stsi_15_1_x(S390CPU *cpu, int sel2, __u64 addr, uint8_t ar)
{
    SysIB sysib = {0};
    int len;

    if (!s390_has_topology() || sel2 < 2 || sel2 > SCLP_READ_SCP_INFO_MNEST) {
        setcc(cpu, 3);
        return;
    }

    s390_order_tle();

    len = setup_stsi(&sysib.sysib_151x, sel2);

    if (len < 0) {
        setcc(cpu, 3);
        return;
    }

    sysib.sysib_151x.length = cpu_to_be16(len);
    s390_cpu_virt_mem_write(cpu, addr, ar, &sysib, len);
    setcc(cpu, 0);

    s390_free_tle();
}
