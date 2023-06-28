/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  QEMU TCG monitor
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "monitor/hmp-target.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "tcg/tcg.h"
#include "exec/tb-stats.h"
#include "exec/tb-flush.h"
#include "disas/disas.h"
#include "tb-context.h"
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

struct tblist_dump_info {
    int count;
    int sortedby;
    Monitor *mon;
};

static void do_dump_tblist_info_safe(CPUState *cpu, run_on_cpu_data info)
{
    struct tblist_dump_info *tbdi = info.host_ptr;
    g_autoptr(GString) buf = g_string_new("");

    dump_tblist_info(buf, tbdi->count, tbdi->sortedby);
    monitor_printf(tbdi->mon, "%s", buf->str);

    g_free(tbdi);
}

void hmp_info_tblist(Monitor *mon, const QDict *qdict)
{
    int number_int;
    const char *sortedby_str = NULL;

    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }
    if (!tb_ctx.tb_stats.map) {
        monitor_printf(mon, "no TB information recorded\n");
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
        monitor_printf(mon, "valid sort options are: hotness hg spills\n");
        return;
    }

    struct tblist_dump_info *tbdi = g_new(struct tblist_dump_info, 1);
    tbdi->count = number_int;
    tbdi->sortedby = sortedby;
    tbdi->mon = mon;
    async_safe_run_on_cpu(first_cpu, do_dump_tblist_info_safe,
                          RUN_ON_CPU_HOST_PTR(tbdi));
}

struct tb_dump_info {
    int id;
    Monitor *mon;
};

static void do_dump_tb_info_safe(CPUState *cpu, run_on_cpu_data info)
{
    struct tb_dump_info *tbdi = info.host_ptr;
    int id = tbdi->id;
    Monitor *mon = tbdi->mon;
    g_autoptr(GString) buf = g_string_new("");

    TBStatistics *tbs = get_tbstats_by_id(id);
    if (tbs == NULL) {
        monitor_printf(mon, "TB %d information is not recorded\n", id);
        return;
    }

    monitor_printf(mon, "\n------------------------------\n\n");

    int valid_tb_num = dump_tb_info(buf, tbs, id);
    monitor_printf(mon, "%s", buf->str);

    if (valid_tb_num > 0) {
        for (int i = tbs->tbs->len - 1; i >= 0; --i) {
            TranslationBlock *tb = g_ptr_array_index(tbs->tbs, i);
            if (!(tb->cflags & CF_INVALID)) {
                monitor_disas(mon, mon_get_cpu(mon), tbs->phys_pc, tb->icount,
                              DISAS_GRA);
                break;
            }
        }
    }
    monitor_printf(mon, "\n------------------------------\n\n");

    g_free(tbdi);
}

void hmp_info_tb(Monitor *mon, const QDict *qdict)
{
    const int id = qdict_get_int(qdict, "id");

    if (!tcg_enabled()) {
        monitor_printf(mon, "Only available with accel=tcg\n");
        return;
    }

    struct tb_dump_info *tbdi = g_new(struct tb_dump_info, 1);
    tbdi->id = id;
    tbdi->mon = mon;
    async_safe_run_on_cpu(first_cpu, do_dump_tb_info_safe,
                          RUN_ON_CPU_HOST_PTR(tbdi));
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp_info_hrt("jit", qmp_x_query_jit);
    monitor_register_hmp_info_hrt("opcount", qmp_x_query_opcount);
}

type_init(hmp_tcg_register);
