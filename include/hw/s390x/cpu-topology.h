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

#ifndef CONFIG_USER_ONLY

#include "qemu/queue.h"
#include "hw/boards.h"
#include "qapi/qapi-types-machine-target.h"

#define S390_TOPOLOGY_CPU_IFL   0x03

typedef struct S390Topology {
    uint8_t *cores_per_socket;
    CpuTopology *smp;
} S390Topology;

#ifdef CONFIG_KVM
bool s390_has_topology(void);
void s390_topology_setup_cpu(MachineState *ms, S390CPU *cpu, Error **errp);
#else
static inline bool s390_has_topology(void)
{
       return false;
}
static inline void s390_topology_setup_cpu(MachineState *ms,
                                           S390CPU *cpu,
                                           Error **errp) {}
#endif

extern S390Topology s390_topology;
int s390_socket_nb(S390CPU *cpu);

static inline int s390_std_socket(int n, CpuTopology *smp)
{
    return (n / smp->cores) % smp->sockets;
}

static inline int s390_std_book(int n, CpuTopology *smp)
{
    return (n / (smp->cores * smp->sockets)) % smp->books;
}

static inline int s390_std_drawer(int n, CpuTopology *smp)
{
    return (n / (smp->cores * smp->sockets * smp->books)) % smp->drawers;
}

#endif /* CONFIG_USER_ONLY */

#endif
