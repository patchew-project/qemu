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

/*
 * s390_topology_add_cpu:
 * @topo: pointer to the topology
 * @cpu : pointer to the new CPU
 *
 * The topology pointed by S390CPU, gives us the CPU topology
 * established by the -smp QEMU arguments.
 * The core-id is used to calculate the position of the CPU inside
 * the topology:
 *  - the socket, container TLE, containing the CPU, we have one socket
 *    for every num_cores cores.
 *  - the CPU TLE inside the socket, we have potentionly up to 4 CPU TLE
 *    in a container TLE with the assumption that all CPU are identical
 *    with the same polarity and entitlement because we have maximum 256
 *    CPUs and each TLE can hold up to 64 identical CPUs.
 *  - the bit in the 64 bit CPU TLE core mask
 */
static void s390_topology_add_cpu(S390Topology *topo, S390CPU *cpu)
{
    int core_id = cpu->env.core_id;
    int bit, origin;
    int socket_id;

    socket_id = core_id / topo->num_cores;
/*
 * At the core level, each CPU is represented by a bit in a 64bit
 * unsigned long.
 * The architecture specifies that all CPU in a CPU TLE have the same
 * type, polarization and are all dedicated or shared.
 * In the case that a socket contains CPUs with different type, polarization
 * or entitlement then they will be defined in different CPU containers.
 * Currently we assume all CPU are identical IFL CPUs and that they are
 * all dedicated CPUs with horizontal polarization.
 * Therefore, the only reason to have several CPU TLE inside a socket is
 * to support CPU id differences > 64.
 * In that case, the origin field in a container represents the offset of
 * the first CPU in that CPU container, thereby allowing representation
 * of all CPUs via multiple containers.
 */
    bit = core_id;
    origin = bit / 64;
    bit %= 64;
    bit = 63 - bit;

    topo->socket[socket_id].active_count++;
    set_bit(bit, &topo->socket[socket_id].mask[origin]);
}

/*
 * s390_prepare_topology:
 * @s390ms : pointer to the S390CcwMachite State
 *
 * Calls s390_topology_add_cpu to organize the topology
 * inside the topology device before writing the SYSIB.
 *
 * The topology is currently fixed on boot and does not change
 * even on migration.
 */
static void s390_prepare_topology(S390CcwMachineState *s390ms)
{
    const MachineState *ms = MACHINE(s390ms);
    S390Topology *topo = s390_get_topology();
    static bool done;
    int i;

    if (done) {
        return;
    }

    for (i = 0; i < ms->possible_cpus->len; i++) {
        if (ms->possible_cpus->cpus[i].cpu) {
            s390_topology_add_cpu(topo,
                                  S390_CPU(ms->possible_cpus->cpus[i].cpu));
        }
    }

    done = true;
}

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

    for (i = 0; i < topo->num_sockets; i++) {
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
    S390Topology *topo = s390_get_topology();
    char *p = sysib->tle;

    sysib->mnest = level;
    switch (level) {
    case 2:
        sysib->mag[S390_TOPOLOGY_MAG2] = topo->num_sockets;
        sysib->mag[S390_TOPOLOGY_MAG1] = topo->num_cores;
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

    s390_prepare_topology(S390_CCW_MACHINE(current_machine));

    len = setup_stsi(cpu, &buffer.sysib, sel2);

    if (len > 4096) {
        setcc(cpu, 3);
        return;
    }

    buffer.sysib.length = cpu_to_be16(len);
    s390_cpu_virt_mem_write(cpu, addr, ar, &buffer.sysib, len);
    setcc(cpu, 0);
}

