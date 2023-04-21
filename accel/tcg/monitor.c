/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "exec/tb-stats.h"
#include "tb-context.h"
#include "internal.h"


static void dump_drift_info(GString *buf)
{
    if (!icount_enabled()) {
        return;
    }

    g_string_append_printf(buf, "Host - Guest clock  %"PRIi64" ms\n",
                           (cpu_get_clock() - icount_get()) / SCALE_MS);
    if (icount_align_option) {
        g_string_append_printf(buf, "Max guest delay     %"PRIi64" ms\n",
                               -max_delay / SCALE_MS);
        g_string_append_printf(buf, "Max guest advance   %"PRIi64" ms\n",
                               max_advance / SCALE_MS);
    } else {
        g_string_append_printf(buf, "Max guest delay     NA\n");
        g_string_append_printf(buf, "Max guest advance   NA\n");
    }
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp, "JIT information is only available with accel=tcg");
        return NULL;
    }

    dump_exec_info(buf);
    dump_drift_info(buf);

    return human_readable_text_from_str(buf);
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tb_stats_collection_enabled()) {
        error_report("TB information not being recorded.");
        return NULL;
    }

    if (!tcg_enabled()) {
        error_setg(errp,
                   "Opcode count information is only available with accel=tcg");
        return NULL;
    }

    tcg_dump_op_count(buf);

    return human_readable_text_from_str(buf);
}

#ifdef CONFIG_TCG
void hmp_tbstats(Monitor *mon, const QDict *qdict)
{
    if (!tcg_enabled()) {
        error_report("TB information is only available with accel=tcg");
        return;
    }

    char *cmd = (char *) qdict_get_try_str(qdict, "command");
    enum TbstatsCmd icmd = -1;

    if (strcmp(cmd, "start") == 0) {
        icmd = START;
    } else if (strcmp(cmd, "pause") == 0) {
        icmd = PAUSE;
    } else if (strcmp(cmd, "stop") == 0) {
        icmd = STOP;
    } else if (strcmp(cmd, "filter") == 0) {
        icmd = FILTER;
    } else {
        error_report("invalid command!");
        return;
    }

    char *slevel = (char *) qdict_get_try_str(qdict, "level");
    uint32_t level = TB_EXEC_STATS | TB_JIT_STATS | TB_JIT_TIME;
    if (slevel) {
        if (strcmp(slevel, "jit") == 0) {
            level = TB_JIT_STATS;
        } else if (strcmp(slevel, "exec") == 0) {
            level = TB_EXEC_STATS;
        } else if (strcmp(slevel, "time") == 0) {
            level = TB_JIT_TIME;
        }
    }

    struct TbstatsCommand *tbscommand = g_new0(struct TbstatsCommand, 1);
    tbscommand->cmd = icmd;
    tbscommand->level = level;
    async_safe_run_on_cpu(first_cpu, do_hmp_tbstats_safe,
                          RUN_ON_CPU_HOST_PTR(tbscommand));

}

void hmp_info_tblist(Monitor *mon, const QDict *qdict)
{
    int number_int;
    const char *sortedby_str = NULL;
    if (!tcg_enabled()) {
        error_report("TB information is only available with accel=tcg");
        return;
    }
    if (!tb_ctx.tb_stats.map) {
        error_report("no TB information recorded");
        return;
    }

    number_int = qdict_get_try_int(qdict, "number", 10);
    sortedby_str = qdict_get_try_str(qdict, "sortedby");

    int sortedby = SORT_BY_HOTNESS;
    if (sortedby_str == NULL || strcmp(sortedby_str, "hotness") == 0) {
        sortedby = SORT_BY_HOTNESS;
    } else if (strcmp(sortedby_str, "hg") == 0) {
        sortedby = SORT_BY_HG;
    } else if (strcmp(sortedby_str, "spills") == 0) {
        sortedby = SORT_BY_SPILLS;
    } else {
        error_report("valid sort options are: hotness hg spills");
        return;
    }

    dump_tbs_info(number_int, sortedby, true);
}

void hmp_info_tb(Monitor *mon, const QDict *qdict)
{
    const int id = qdict_get_int(qdict, "id");
    const char *flags = qdict_get_try_str(qdict, "flags");
    int mask;

    if (!tcg_enabled()) {
        error_report("TB information is only available with accel=tcg");
        return;
    }

    mask = flags ? qemu_str_to_log_mask(flags) : CPU_LOG_TB_IN_ASM;

    if (!mask) {
        error_report("Unable to parse log flags, see 'help log'");
        return;
    }

    dump_tb_info(id, mask, true);
}

#endif

HumanReadableText *qmp_x_query_profile(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

#ifdef CONFIG_TCG
    dump_jit_exec_time_info(dev_time);
    dev_time = 0;
#else
    error_report("TCG should be enabled!");
#endif

    return human_readable_text_from_str(buf);
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
}

type_init(hmp_tcg_register);
