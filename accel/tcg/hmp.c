#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "exec/exec-all.h"
#include "monitor/monitor.h"
#include "sysemu/tcg.h"

static void hmp_info_jit(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    g_autoptr(HumanReadableText) info = NULL;

    info = qmp_x_query_jit(&err);
    if (err) {
        error_report_err(err);
        return;
    }

    monitor_printf(mon, "%s", info->human_readable_text);
}

static void hmp_info_opcount(Monitor *mon, const QDict *qdict)
{
    dump_opcount_info();
}

static void hmp_tcg_register(void)
{
    monitor_register_hmp("jit", true, hmp_info_jit);
    monitor_register_hmp("opcount", true, hmp_info_opcount);
}

type_init(hmp_tcg_register);
