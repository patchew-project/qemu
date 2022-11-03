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

#include "hw/qdev-core.h"
#include "qom/object.h"

#define S390_TOPOLOGY_CPU_IFL 0x03
#define S390_TOPOLOGY_MAX_ORIGIN ((63 + S390_MAX_CPUS) / 64)

typedef struct S390TopoSocket {
    int active_count;
    uint64_t mask[S390_TOPOLOGY_MAX_ORIGIN];
} S390TopoSocket;

struct S390Topology {
    SysBusDevice parent_obj;
    uint32_t nr_cpus;
    uint32_t nr_sockets;
    S390TopoSocket *socket;
};

#define TYPE_S390_CPU_TOPOLOGY "s390-topology"
OBJECT_DECLARE_SIMPLE_TYPE(S390Topology, S390_CPU_TOPOLOGY)

void s390_topology_new_cpu(S390CPU *cpu);

static inline bool s390_has_topology(void)
{
    return false;
}

#endif
