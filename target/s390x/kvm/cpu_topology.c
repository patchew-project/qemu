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

static char *fill_container(char *p, int level, int id)
{
    SysIBTl_container *tle = (SysIBTl_container *)p;

    tle->nl = level;
    tle->id = id;
    return p + sizeof(*tle);
}

static char *fill_tle_cpu(char *p, S390TopologyEntry *entry)
{
    SysIBTl_cpu *tle = (SysIBTl_cpu *)p;
    s390_topology_id topology_id = entry->id;

    tle->nl = 0;
    tle->dedicated = topology_id.d;
    tle->polarity = topology_id.p;
    tle->type = topology_id.type;
    tle->origin = topology_id.origin;
    tle->mask = cpu_to_be64(entry->mask);
    return p + sizeof(*tle);
}

static char *s390_top_set_level(char *p, int level)
{
    S390TopologyEntry *entry;
    uint64_t last_socket = -1UL;
    uint64_t last_book = -1UL;
    uint64_t last_drawer = -1UL;
    int drawer_cnt = 0;
    int book_cnt = 0;
    int socket_cnt = 0;

    QTAILQ_FOREACH(entry, &s390_topology.list, next) {

        if (level > 3 && (last_drawer != entry->id.drawer)) {
            book_cnt = 0;
            socket_cnt = 0;
            p = fill_container(p, 3, drawer_cnt++);
            last_drawer = entry->id.id & TOPO_DRAWER_MASK;
            p = fill_container(p, 2, book_cnt++);
            last_book = entry->id.id & TOPO_BOOK_MASK;
            p = fill_container(p, 1, socket_cnt++);
            last_socket = entry->id.id & TOPO_SOCKET_MASK;
            p = fill_tle_cpu(p, entry);
        } else if (level > 2 && (last_book !=
                                 (entry->id.id & TOPO_BOOK_MASK))) {
            socket_cnt = 0;
            p = fill_container(p, 2, book_cnt++);
            last_book = entry->id.id & TOPO_BOOK_MASK;
            p = fill_container(p, 1, socket_cnt++);
            last_socket = entry->id.id & TOPO_SOCKET_MASK;
            p = fill_tle_cpu(p, entry);
        } else if (last_socket != (entry->id.id & TOPO_SOCKET_MASK)) {
            p = fill_container(p, 1, socket_cnt++);
            last_socket = entry->id.id & TOPO_SOCKET_MASK;
            p = fill_tle_cpu(p, entry);
        } else {
            p = fill_tle_cpu(p, entry);
        }
    }

    return p;
}

static int setup_stsi(S390CPU *cpu, SysIB_151x *sysib, int level)
{
    char *p = sysib->tle;

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
    p = s390_top_set_level(p, level);

    return p - (char *)sysib;
}

void insert_stsi_15_1_x(S390CPU *cpu, int sel2, __u64 addr, uint8_t ar)
{
    union {
        char place_holder[S390_TOPOLOGY_SYSIB_SIZE];
        SysIB_151x sysib;
    } buffer QEMU_ALIGNED(8) = {};
    int len;

    if (!s390_has_topology() || sel2 < 2 || sel2 > SCLP_READ_SCP_INFO_MNEST) {
        setcc(cpu, 3);
        return;
    }

    len = setup_stsi(cpu, &buffer.sysib, sel2);

    if (len > 4096) {
        setcc(cpu, 3);
        return;
    }

    buffer.sysib.length = cpu_to_be16(len);
    s390_cpu_virt_mem_write(cpu, addr, ar, &buffer.sysib, len);
    setcc(cpu, 0);
}
