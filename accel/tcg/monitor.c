/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "tcg/tcg.h"
#include "exec/tb-stats.h"
#include "exec/tb-flush.h"
#include "internal.h"
#include "tb-context.h"

enum TbstatsCmd { TBS_CMD_START, TBS_CMD_STOP, TBS_CMD_STATUS };

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

static void dump_accel_info(GString *buf)
{
    AccelState *accel = current_accel();
    bool one_insn_per_tb = object_property_get_bool(OBJECT(accel),
                                                    "one-insn-per-tb",
                                                    &error_fatal);

    g_string_append_printf(buf, "Accelerator settings:\n");
    g_string_append_printf(buf, "one-insn-per-tb: %s\n\n",
                           one_insn_per_tb ? "on" : "off");
}

HumanReadableText *qmp_x_query_jit(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tcg_enabled()) {
        error_setg(errp, "JIT information is only available with accel=tcg");
        return NULL;
    }

    dump_accel_info(buf);
    dump_exec_info(buf);
    dump_drift_info(buf);

    return human_readable_text_from_str(buf);
}

HumanReadableText *qmp_x_query_opcount(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    if (!tb_stats_collection_enabled()) {
        error_setg(errp, "TB information not being recorded");
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

struct TbstatsCommand {
    enum TbstatsCmd cmd;
    uint32_t flag;
    Monitor *mon;
};

static void do_hmp_tbstats_safe(CPUState *cpu, run_on_cpu_data icmd)
{
    struct TbstatsCommand *cmdinfo = icmd.host_ptr;
    int cmd = cmdinfo->cmd;
    uint32_t flag = cmdinfo->flag;
    Monitor *mon = cmdinfo->mon;

    switch (cmd) {
    case TBS_CMD_START:
        if (tb_stats_collection_enabled()) {
            monitor_printf(mon, "TB information already being recorded\n");
            break;
        }

        set_tbstats_flag(flag);
        enable_collect_tb_stats();
        tb_flush(cpu);
        break;
    case TBS_CMD_STOP:
        if (tb_stats_collection_disabled()) {
            monitor_printf(mon, "TB information not being recorded\n");
            break;
        }

        /* Dissalloc all TBStatistics structures and stop creating new ones */
        disable_collect_tb_stats();
        clean_tbstats();
        tb_flush(cpu);
        break;
    case TBS_CMD_STATUS:
        if (tb_stats_collection_enabled()) {
            uint32_t flag = get_tbstats_flag();
            monitor_printf(mon, "tb_stats is enabled with flag:\n");
            monitor_printf(mon, "    EXEC: %d\n", !!(flag & TB_EXEC_STATS));
            monitor_printf(mon, "     JIT: %d\n", !!(flag & TB_JIT_STATS));
        } else {
            monitor_printf(mon, "tb_stats is disabled\n");
        }
        break;
    default: /* INVALID */
        g_assert_not_reached();
        break;
    }

    g_free(cmdinfo);
}

void hmp_tbstats(Monitor *mon, const QDict *qdict)
{
    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }

    char *cmd = (char *) qdict_get_try_str(qdict, "command");
    enum TbstatsCmd icmd = -1;

    if (strcmp(cmd, "start") == 0) {
        icmd = TBS_CMD_START;
    } else if (strcmp(cmd, "stop") == 0) {
        icmd = TBS_CMD_STOP;
    } else if (strcmp(cmd, "status") == 0) {
        icmd = TBS_CMD_STATUS;
    } else {
        monitor_printf(mon, "Invalid command\n");
        return;
    }

    char *sflag = (char *) qdict_get_try_str(qdict, "flag");
    uint32_t flag = TB_EXEC_STATS | TB_JIT_STATS;
    if (sflag) {
        if (strcmp(sflag, "jit") == 0) {
            flag = TB_JIT_STATS;
        } else if (strcmp(sflag, "exec") == 0) {
            flag = TB_EXEC_STATS;
        }
    }

    struct TbstatsCommand *tbscommand = g_new0(struct TbstatsCommand, 1);
    tbscommand->cmd = icmd;
    tbscommand->flag = flag;
    tbscommand->mon = mon;
    async_safe_run_on_cpu(first_cpu, do_hmp_tbstats_safe,
                          RUN_ON_CPU_HOST_PTR(tbscommand));

}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
}

type_init(hmp_tcg_register);
