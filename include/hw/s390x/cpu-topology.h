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

typedef struct S390Topology {
    uint8_t *cores_per_socket;
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
int s390_socket_nb(S390CPU *cpu);

#endif
