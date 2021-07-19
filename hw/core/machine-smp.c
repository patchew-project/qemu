/*
 * QEMU Machine (related to SMP configuration)
 *
 * Copyright (C) 2014 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel.a@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi/error.h"

/*
 * smp_parse - Generic function used to parse the given SMP configuration
 *
 * The topology parameters must be specified equal to or great than one
 * or just omitted, explicit configuration like "cpus=0" is not allowed.
 * The omitted parameters will be calculated based on the provided ones.
 *
 * maxcpus will default to the value of cpus if omitted and will be used
 * to compute the missing sockets/cores/threads. cpus will be calculated
 * from the computed parametrs if omitted.
 *
 * In calculation of omitted arch-netural sockets/cores/threads, we prefer
 * sockets over cores over threads before 6.2, while prefer cores over
 * sockets over threads since 6.2 on. The arch-specific dies will directly
 * default to 1 if omitted.
 */
void smp_parse(MachineState *ms, SMPConfiguration *config, Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    unsigned cpus    = config->has_cpus ? config->cpus : 0;
    unsigned sockets = config->has_sockets ? config->sockets : 0;
    unsigned dies    = config->has_dies ? config->dies : 1;
    unsigned cores   = config->has_cores ? config->cores : 0;
    unsigned threads = config->has_threads ? config->threads : 0;
    unsigned maxcpus = config->has_maxcpus ? config->maxcpus : 0;

    if ((config->has_cpus && config->cpus == 0) ||
        (config->has_sockets && config->sockets == 0) ||
        (config->has_dies && config->dies == 0) ||
        (config->has_cores && config->cores == 0) ||
        (config->has_threads && config->threads == 0) ||
        (config->has_maxcpus && config->maxcpus == 0)) {
        error_setg(errp, "parameters must be equal to or greater than one"
                   "if provided");
        return;
    }

    if (!mc->smp_dies_supported && dies > 1) {
        error_setg(errp, "dies not supported by this machine's CPU topology");
        return;
    }

    maxcpus = maxcpus > 0 ? maxcpus : cpus;

    /* prefer sockets over cores over threads before 6.2 */
    if (mc->smp_prefer_sockets) {
        if (sockets == 0) {
            cores = cores > 0 ? cores : 1;
            threads = threads > 0 ? threads : 1;
            sockets = maxcpus / (dies * cores * threads);
            sockets = sockets > 0 ? sockets : 1;
        } else if (cores == 0) {
            threads = threads > 0 ? threads : 1;
            cores = maxcpus / (sockets * dies * threads);
            cores = cores > 0 ? cores : 1;
        } else if (threads == 0) {
            threads = maxcpus / (sockets * dies * cores);
            threads = threads > 0 ? threads : 1;
        }
    /* prefer cores over sockets over threads since 6.2 */
    } else {
        if (cores == 0) {
            sockets = sockets > 0 ? sockets : 1;
            threads = threads > 0 ? threads : 1;
            cores = maxcpus / (sockets * dies * threads);
            cores = cores > 0 ? cores : 1;
        } else if (sockets == 0) {
            threads = threads > 0 ? threads : 1;
            sockets = maxcpus / (dies * cores * threads);
            sockets = sockets > 0 ? sockets : 1;
        } else if (threads == 0) {
            threads = maxcpus / (sockets * dies * cores);
            threads = threads > 0 ? threads : 1;
        }
    }

    /* use the computed parameters to calculate the omitted cpus */
    cpus = cpus > 0 ? cpus : sockets * dies * cores * threads;
    maxcpus = maxcpus > 0 ? maxcpus : cpus;

    if (sockets * dies * cores * threads != maxcpus) {
        g_autofree char *dies_msg = g_strdup_printf(
            mc->smp_dies_supported ? " * dies (%u)" : "", dies);
        error_setg(errp, "Invalid CPU topology: "
                   "sockets (%u)%s * cores (%u) * threads (%u) "
                   "!= maxcpus (%u)",
                   sockets, dies_msg, cores, threads,
                   maxcpus);
        return;
    }

    if (sockets * dies * cores * threads < cpus) {
        g_autofree char *dies_msg = g_strdup_printf(
            mc->smp_dies_supported ? " * dies (%u)" : "", dies);
        error_setg(errp, "Invalid CPU topology: "
                   "sockets (%u)%s * cores (%u) * threads (%u) < "
                   "smp_cpus (%u)",
                   sockets, dies_msg, cores, threads, cpus);
        return;
    }

    ms->smp.cpus = cpus;
    ms->smp.sockets = sockets;
    ms->smp.dies = dies;
    ms->smp.cores = cores;
    ms->smp.threads = threads;
    ms->smp.max_cpus = maxcpus;
}
