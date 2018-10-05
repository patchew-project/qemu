/*
 * Plugin Support
 *
 *
 * Copyright (c) 2018 Pavel Dovgalyuk <Pavel.Dovgaluk@ispras.ru>
 * Copyright (c) 2018 Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <gmodule.h>
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/plugins.h"
#include "qemu/queue.h"
#include "qemu/option.h"
#include "trace/control.h"

typedef bool (*PluginInitFunc)(const char *);
typedef char * (*PluginStatusFunc)(void);

typedef struct QemuPluginInfo {
    const char *filename;
    const char *args;
    GModule *g_module;

    PluginInitFunc init;
    PluginStatusFunc status;

    GPtrArray *events;

    QLIST_ENTRY(QemuPluginInfo) next;
} QemuPluginInfo;

static QLIST_HEAD(, QemuPluginInfo) qemu_plugins
                                = QLIST_HEAD_INITIALIZER(qemu_plugins);

static QemuOptsList qemu_plugin_opts = {
    .name = "plugin",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_plugin_opts.head),
    .desc = {
        {
            .name = "file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "args",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

void qemu_plugin_parse_cmd_args(const char *optarg)
{
    QemuOpts *opts = qemu_opts_parse_noisily(&qemu_plugin_opts, optarg, false);
    qemu_plugin_load(qemu_opt_get(opts, "file"),
                     qemu_opt_get(opts, "args"));
}

static int bind_to_tracepoints(GModule *g_module, GPtrArray *events)
{
    int count = 0;
    TraceEventIter iter;
    TraceEvent *ev;

    trace_event_iter_init(&iter, "*");
    while ((ev = trace_event_iter_next(&iter)) != NULL) {
        const char *name = trace_event_get_name(ev);
        gpointer fn;

        if (g_module_symbol(g_module, name, &fn)) {
            ev->plugin = (uintptr_t) fn;
            trace_event_set_state_dynamic(ev, true);
            count++;
        }
    }

    return count;
}

void qemu_plugin_load(const char *filename, const char *args)
{
    GModule *g_module;
    QemuPluginInfo *info = NULL;
    if (!filename) {
        error_report("plugin name was not specified");
        return;
    }
    g_module = g_module_open(filename,
        G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (!g_module) {
        error_report("can't load plugin '%s'", filename);
        return;
    }
    info = g_new0(QemuPluginInfo, 1);
    info->filename = g_strdup(filename);
    info->g_module = g_module;

    if (!g_module_symbol(g_module, "plugin_init",
                         (gpointer *) &info->init)) {
        error_report("all plugins must provide a plugin_init hook");
        return;
    }

    if (!g_module_symbol(g_module, "plugin_status",
                         (gpointer *) &info->status)) {
        error_report("all plugins must provide a plugin_status hook");
        return;
    }

    /* OK we can now see how many events might have bindings */
    info->events = g_ptr_array_new();

    if (bind_to_tracepoints(g_module, info->events) < 0) {
        error_report("failed to bind any events");
        return;
    }

    /* Save the args, we will initialise later on once everything is
       set up */
    if (args) {
        info->args = g_strdup(args);
    }

    QLIST_INSERT_HEAD(&qemu_plugins, info, next);

    return;
}

void qemu_plugins_init(void)
{
    QemuPluginInfo *info;
    QLIST_FOREACH(info, &qemu_plugins, next) {
        if (info->init) {
            info->init(info->args);
        }
    }
}
