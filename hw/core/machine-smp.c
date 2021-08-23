/*
 * QEMU Machine (related to SMP)
 *
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qemu/cutils.h"

static char *cpu_topology_hierarchy(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    SMPCompatProps *smp_props = &mc->smp_props;
    char topo_msg[256] = "";

    /*
     * Topology members should be ordered from the largest to the smallest.
     * Concept of sockets/cores/threads is supported by default and will be
     * reported in the hierarchy. Unsupported members will not be reported.
     */
    g_autofree char *sockets_msg = g_strdup_printf(
            " * sockets (%u)", ms->smp.sockets);
    pstrcat(topo_msg, sizeof(topo_msg), sockets_msg);

    if (smp_props->dies_supported) {
        g_autofree char *dies_msg = g_strdup_printf(
                " * dies (%u)", ms->smp.dies);
        pstrcat(topo_msg, sizeof(topo_msg), dies_msg);
    }

    g_autofree char *cores_msg = g_strdup_printf(
            " * cores (%u)", ms->smp.cores);
    pstrcat(topo_msg, sizeof(topo_msg), cores_msg);

    g_autofree char *threads_msg = g_strdup_printf(
            " * threads (%u)", ms->smp.threads);
    pstrcat(topo_msg, sizeof(topo_msg), threads_msg);

    return g_strdup_printf("%s", topo_msg + 3);
}

/*
 * smp_parse - Generic function used to parse the given SMP configuration
 *
 * If not supported by the machine, a topology parameter must be omitted
 * or specified equal to 1. Concept of sockets/cores/threads is supported
 * by default. Unsupported members will not be reported in the topology
 * hierarchy message.
 *
 * For compatibility, omitted arch-specific members (e.g. dies) will not
 * be computed, but will directly default to 1 instead. This logic should
 * also apply to future introduced ones.
 *
 * Omitted arch-neutral parameters (i.e. cpus/sockets/cores/threads/maxcpus)
 * will be computed based on the provided ones. When both maxcpus and cpus
 * are omitted, maxcpus will be computed from the given parameters and cpus
 * will be set equal to maxcpus. When only one of maxcpus and cpus is given
 * then the omitted one will be set to its given counterpart's value.
 * Both maxcpus and cpus may be specified, but maxcpus must be equal to or
 * greater than cpus.
 *
 * In calculation of omitted sockets/cores/threads, we prefer sockets over
 * cores over threads before 6.2, while preferring cores over sockets over
 * threads since 6.2.
 */
void smp_parse(MachineState *ms, SMPConfiguration *config, Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    unsigned cpus    = config->has_cpus ? config->cpus : 0;
    unsigned sockets = config->has_sockets ? config->sockets : 0;
    unsigned dies    = config->has_dies ? config->dies : 0;
    unsigned cores   = config->has_cores ? config->cores : 0;
    unsigned threads = config->has_threads ? config->threads : 0;
    unsigned maxcpus = config->has_maxcpus ? config->maxcpus : 0;

    /*
     * Specified CPU topology parameters must be greater than zero,
     * explicit configuration like "cpus=0" is not allowed.
     */
    if ((config->has_cpus && config->cpus == 0) ||
        (config->has_sockets && config->sockets == 0) ||
        (config->has_dies && config->dies == 0) ||
        (config->has_cores && config->cores == 0) ||
        (config->has_threads && config->threads == 0) ||
        (config->has_maxcpus && config->maxcpus == 0)) {
        warn_report("Invalid CPU topology deprecated: "
                    "CPU topology parameters must be greater than zero");
    }

    /*
     * If not supported by the machine, a topology parameter must be
     * omitted or specified equal to 1.
     */
    if (!mc->smp_props.dies_supported && dies > 1) {
        error_setg(errp, "dies not supported by this machine's CPU topology");
        return;
    }

    /*
     * Omitted arch-specific members will not be computed, but will
     * directly default to 1 instead.
     */
    dies = dies > 0 ? dies : 1;

    /* compute missing values based on the provided ones */
    if (cpus == 0 && maxcpus == 0) {
        sockets = sockets > 0 ? sockets : 1;
        cores = cores > 0 ? cores : 1;
        threads = threads > 0 ? threads : 1;
    } else {
        maxcpus = maxcpus > 0 ? maxcpus : cpus;

        if (mc->smp_props.prefer_sockets) {
            /* prefer sockets over cores before 6.2 */
            if (sockets == 0) {
                cores = cores > 0 ? cores : 1;
                threads = threads > 0 ? threads : 1;
                sockets = maxcpus / (dies * cores * threads);
            } else if (cores == 0) {
                threads = threads > 0 ? threads : 1;
                cores = maxcpus / (sockets * dies * threads);
            }
        } else {
            /* prefer cores over sockets since 6.2 */
            if (cores == 0) {
                sockets = sockets > 0 ? sockets : 1;
                threads = threads > 0 ? threads : 1;
                cores = maxcpus / (sockets * dies * threads);
            } else if (sockets == 0) {
                threads = threads > 0 ? threads : 1;
                sockets = maxcpus / (dies * cores * threads);
            }
        }

        /* try to calculate omitted threads at last */
        if (threads == 0) {
            threads = maxcpus / (sockets * dies * cores);
        }
    }

    maxcpus = maxcpus > 0 ? maxcpus : sockets * dies * cores * threads;
    cpus = cpus > 0 ? cpus : maxcpus;

    ms->smp.cpus = cpus;
    ms->smp.sockets = sockets;
    ms->smp.dies = dies;
    ms->smp.cores = cores;
    ms->smp.threads = threads;
    ms->smp.max_cpus = maxcpus;

    /* sanity-check of the computed topology */
    if (sockets * dies * cores * threads != maxcpus) {
        g_autofree char *topo_msg = cpu_topology_hierarchy(ms);
        error_setg(errp, "Invalid CPU topology: "
                   "product of the hierarchy must match maxcpus: "
                   "%s != maxcpus (%u)",
                   topo_msg, maxcpus);
        return;
    }

    if (maxcpus < cpus) {
        g_autofree char *topo_msg = cpu_topology_hierarchy(ms);
        error_setg(errp, "Invalid CPU topology: "
                   "maxcpus must be equal to or greater than smp: "
                   "%s == maxcpus (%u) < smp_cpus (%u)",
                   topo_msg, maxcpus, cpus);
        return;
    }

    if (ms->smp.cpus < mc->min_cpus) {
        error_setg(errp, "Invalid SMP CPUs %d. The min CPUs "
                   "supported by machine '%s' is %d",
                   ms->smp.cpus,
                   mc->name, mc->min_cpus);
        return;
    }

    if (ms->smp.max_cpus > mc->max_cpus) {
        error_setg(errp, "Invalid SMP CPUs %d. The max CPUs "
                   "supported by machine '%s' is %d",
                   ms->smp.max_cpus,
                   mc->name, mc->max_cpus);
        return;
    }
}
