/*
 * CPU Topology
 *
 * Copyright 2022 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_CPU_TOPOLOGY_H
#define HW_S390X_CPU_TOPOLOGY_H

#include "hw/qdev-core.h"
#include "qom/object.h"

typedef struct S390TopoContainer {
    int active_count;
} S390TopoContainer;

#define S390_TOPOLOGY_CPU_IFL 0x03
#define S390_TOPOLOGY_MAX_ORIGIN ((63 + S390_MAX_CPUS) / 64)
typedef struct S390TopoTLE {
    uint64_t mask[S390_TOPOLOGY_MAX_ORIGIN];
} S390TopoTLE;

struct S390Topology {
    SysBusDevice parent_obj;
    int cpus;
    S390TopoContainer *socket;
    S390TopoTLE *tle;
    MachineState *ms;
};

#define TYPE_S390_CPU_TOPOLOGY "s390-topology"
OBJECT_DECLARE_SIMPLE_TYPE(S390Topology, S390_CPU_TOPOLOGY)

S390Topology *s390_get_topology(void);
void s390_topology_new_cpu(int core_id);

static inline bool s390_has_topology(void)
{
    return false;
}

#endif
