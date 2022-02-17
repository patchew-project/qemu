/*
 * QEMU S390x CPU Topology
 *
 * Copyright IBM Corp. 2021
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

static int stsi_15_container(void *p, int nl, int id)
{
    SysIBTl_container *tle = (SysIBTl_container *)p;

    tle->nl = nl;
    tle->id = id;

    return sizeof(*tle);
}

static int stsi_15_cpus(void *p, S390TopologyCores *cd)
{
    SysIBTl_cpu *tle = (SysIBTl_cpu *)p;

    tle->nl = 0;
    tle->dedicated = cd->dedicated;
    tle->polarity = cd->polarity;
    tle->type = cd->cputype;
    tle->origin = be16_to_cpu(cd->origin);
    tle->mask = be64_to_cpu(cd->mask);

    return sizeof(*tle);
}

static int set_socket(const MachineState *ms, void *p,
                      S390TopologySocket *socket)
{
    BusChild *kid;
    int l, len = 0;

    len += stsi_15_container(p, 1, socket->socket_id);
    p += len;

    QTAILQ_FOREACH_REVERSE(kid, &socket->bus->children, sibling) {
        l = stsi_15_cpus(p, S390_TOPOLOGY_CORES(kid->child));
        p += l;
        len += l;
    }
    return len;
}

static void setup_stsi(const MachineState *ms, void *p, int level)
{
    S390TopologyBook *book;
    SysIB_151x *sysib;
    BusChild *kid;
    int len, l;

    sysib = (SysIB_151x *)p;
    sysib->mnest = level;
    sysib->mag[TOPOLOGY_NR_MAG2] = ms->smp.sockets;
    sysib->mag[TOPOLOGY_NR_MAG1] = ms->smp.cores * ms->smp.threads;

    book = s390_get_topology();
    len = sizeof(SysIB_151x);
    p += len;

    QTAILQ_FOREACH_REVERSE(kid, &book->bus->children, sibling) {
        l = set_socket(ms, p, S390_TOPOLOGY_SOCKET(kid->child));
        p += l;
        len += l;
    }

    sysib->length = be16_to_cpu(len);
}

void insert_stsi_15_1_x(S390CPU *cpu, int sel2, __u64 addr, uint8_t ar)
{
    const MachineState *machine = MACHINE(qdev_get_machine());
    void *p;
    int ret, cc;

    /*
     * Until the SCLP STSI Facility reporting the MNEST value is used,
     * a sel2 value of 2 is the only value allowed in STSI 15.1.x.
     */
    if (sel2 != 2) {
        setcc(cpu, 3);
        return;
    }

    p = g_malloc0(TARGET_PAGE_SIZE);

    setup_stsi(machine, p, 2);

    if (s390_is_pv()) {
        ret = s390_cpu_pv_mem_write(cpu, 0, p, TARGET_PAGE_SIZE);
    } else {
        ret = s390_cpu_virt_mem_write(cpu, addr, ar, p, TARGET_PAGE_SIZE);
    }
    cc = ret ? 3 : 0;
    setcc(cpu, cc);
    g_free(p);
}

