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
#include "hw/s390x/cpu-topology.h"
#include "hw/s390x/sclp.h"

static char *fill_container(char *p, int level, int id)
{
    SysIBTl_container *tle = (SysIBTl_container *)p;

    tle->nl = level;
    tle->id = id;
    return p + sizeof(*tle);
}

static char *fill_tle_cpu(char *p, uint64_t mask, int origin)
{
    SysIBTl_cpu *tle = (SysIBTl_cpu *)p;

    tle->nl = 0;
    tle->dedicated = 1;
    tle->polarity = S390_TOPOLOGY_POLARITY_HORIZONTAL;
    tle->type = S390_TOPOLOGY_CPU_IFL;
    tle->origin = cpu_to_be64(origin * 64);
    tle->mask = cpu_to_be64(mask);
    return p + sizeof(*tle);
}

static char *s390_top_set_level2(S390Topology *topo, char *p)
{
    int i, origin;

    for (i = 0; i < topo->nr_sockets; i++) {
        if (!topo->socket[i].active_count) {
            continue;
        }
        p = fill_container(p, 1, i);
        for (origin = 0; origin < S390_TOPOLOGY_MAX_ORIGIN; origin++) {
            uint64_t mask = 0L;

            mask = topo->socket[i].mask[origin];
            if (mask) {
                p = fill_tle_cpu(p, mask, origin);
            }
        }
    }
    return p;
}

static int setup_stsi(S390CPU *cpu, SysIB_151x *sysib, int level)
{
    S390Topology *topo = (S390Topology *)cpu->topology;
    char *p = sysib->tle;

    sysib->mnest = level;
    switch (level) {
    case 2:
        sysib->mag[S390_TOPOLOGY_MAG2] = topo->nr_sockets;
        sysib->mag[S390_TOPOLOGY_MAG1] = topo->nr_cpus;
        p = s390_top_set_level2(topo, p);
        break;
    }

    return p - (char *)sysib;
}

#define S390_TOPOLOGY_MAX_MNEST 2

void insert_stsi_15_1_x(S390CPU *cpu, int sel2, __u64 addr, uint8_t ar)
{
    union {
        char place_holder[S390_TOPOLOGY_SYSIB_SIZE];
        SysIB_151x sysib;
    } buffer QEMU_ALIGNED(8);
    int len;

    if (s390_is_pv() || !s390_has_topology() ||
        sel2 < 2 || sel2 > S390_TOPOLOGY_MAX_MNEST) {
        setcc(cpu, 3);
        return;
    }

    len = setup_stsi(cpu, &buffer.sysib, sel2);

    buffer.sysib.length = cpu_to_be16(len);
    s390_cpu_virt_mem_write(cpu, addr, ar, &buffer.sysib, len);
    setcc(cpu, 0);
}

