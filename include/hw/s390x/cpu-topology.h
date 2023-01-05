/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_CPU_TOPOLOGY_H
#define HW_S390X_CPU_TOPOLOGY_H

#include "qemu/queue.h"
#include "hw/boards.h"

#define S390_TOPOLOGY_CPU_IFL   0x03
#define S390_TOPOLOGY_MAX_ORIGIN ((63 + S390_MAX_CPUS) / 64)

#define S390_TOPOLOGY_POLARITY_HORIZONTAL      0x00
#define S390_TOPOLOGY_POLARITY_VERTICAL_LOW    0x01
#define S390_TOPOLOGY_POLARITY_VERTICAL_MEDIUM 0x02
#define S390_TOPOLOGY_POLARITY_VERTICAL_HIGH   0x03

#define S390_TOPOLOGY_SHARED    0x00
#define S390_TOPOLOGY_DEDICATED 0x01

typedef union s390_topology_id {
    uint64_t id;
    struct {
        uint64_t level_6:8; /* byte 0 BE */
        uint64_t level_5:8; /* byte 1 BE */
        uint64_t drawer:8;  /* byte 2 BE */
        uint64_t book:8;    /* byte 3 BE */
        uint64_t socket:8;  /* byte 4 BE */
        uint64_t rsrv:5;
        uint64_t d:1;
        uint64_t p:2;       /* byte 5 BE */
        uint64_t type:8;    /* byte 6 BE */
        uint64_t origin:2;
        uint64_t core:6;    /* byte 7 BE */
    };
} s390_topology_id;
#define TOPO_CPU_MASK       0x000000000000003fUL
#define TOPO_SOCKET_MASK    0x0000ffffff000000UL
#define TOPO_BOOK_MASK      0x0000ffff00000000UL
#define TOPO_DRAWER_MASK    0x0000ff0000000000UL

typedef struct S390TopologyEntry {
    s390_topology_id id;
    QTAILQ_ENTRY(S390TopologyEntry) next;
    uint64_t mask;
} S390TopologyEntry;

typedef struct S390Topology {
    QTAILQ_HEAD(, S390TopologyEntry) list;
    uint8_t *sockets;
    CpuTopology *smp;
} S390Topology;

#ifdef CONFIG_KVM
bool s390_has_topology(void);
void s390_topology_set_cpu(MachineState *ms, S390CPU *cpu, Error **errp);
#else
static inline bool s390_has_topology(void)
{
       return false;
}
static inline void s390_topology_set_cpu(MachineState *ms,
                                         S390CPU *cpu,
                                         Error **errp) {}
#endif
extern S390Topology s390_topology;

#endif
