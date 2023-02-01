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

enum s390_topology_polarity {
    POLARITY_HORIZONTAL,
    POLARITY_VERTICAL,
    POLARITY_VERTICAL_LOW = 1,
    POLARITY_VERTICAL_MEDIUM,
    POLARITY_VERTICAL_HIGH,
    POLARITY_MAX,
};

typedef union s390_topology_id {
    uint64_t id;
    struct {
        uint8_t level5;
        uint8_t drawer;
        uint8_t book;
        uint8_t socket;
        uint8_t dedicated;
        uint8_t polarity;
        uint8_t type;
        uint8_t origin;
    };
} s390_topology_id;

typedef struct S390TopologyEntry {
    QTAILQ_ENTRY(S390TopologyEntry) next;
    s390_topology_id id;
    uint64_t mask;
} S390TopologyEntry;

typedef struct S390Topology {
    uint8_t *cores_per_socket;
    QTAILQ_HEAD(, S390TopologyEntry) list;
    CpuTopology *smp;
    uint8_t polarity;
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
int s390_socket_nb(S390CPU *cpu);

#endif
