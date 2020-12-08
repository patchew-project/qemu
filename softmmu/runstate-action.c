/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/runstate-action.h"
#include "sysemu/sysemu.h"
#include "sysemu/watchdog.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qemu/option_int.h"
#include "qapi/qapi-commands-run-state.h"

static void runstate_action_help(void)
{
    int idx;

    printf("Events for which an action can be specified:\n");
    for (idx = 0; idx < RUN_STATE_EVENT_TYPE__MAX; idx++) {
        printf("%10s\n", RunStateEventType_str(idx));
    }
}

/*
 * Set the internal state to react to a guest reboot event
 * as specified by the action parameter.
 */
static void qmp_reboot_set_action(RebootAction act, Error **errp)
{
    switch (act) {
    case REBOOT_ACTION_NONE:
        no_reboot = 0;
        break;
    case REBOOT_ACTION_SHUTDOWN:
        no_reboot = 1;
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Set the internal state to react to a guest shutdown event
 * as specified by the action parameter.
 */
static void qmp_shutdown_set_action(ShutdownAction act, Error **errp)
{
    switch (act) {
    case SHUTDOWN_ACTION_PAUSE:
        no_shutdown = 1;
        break;
    case SHUTDOWN_ACTION_POWEROFF:
        no_shutdown = 0;
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Set the internal state to react to a guest panic event
 * as specified by the action parameter.
 */
static void qmp_panic_set_action(PanicAction action, Error **errp)
{
    switch (action) {
    case PANIC_ACTION_NONE:
        pause_on_panic = 0;
        break;
    case PANIC_ACTION_PAUSE:
        pause_on_panic = 1;
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Process an event|action pair and set the appropriate internal
 * state if event and action are valid.
 */
static int set_runstate_action(void *opaque, const char *event,
                                const char *action, Error **errp)
{
    int event_idx, act_idx;

    event_idx = qapi_enum_parse(&RunStateEventType_lookup, event, -1, errp);

    switch (event_idx) {
    case RUN_STATE_EVENT_TYPE_REBOOT:
        act_idx = qapi_enum_parse(&RebootAction_lookup, action, -1, errp);
        qmp_reboot_set_action(act_idx, NULL);
        break;
    case RUN_STATE_EVENT_TYPE_SHUTDOWN:
        act_idx = qapi_enum_parse(&ShutdownAction_lookup, action, -1, errp);
        qmp_shutdown_set_action(act_idx, NULL);
        break;
    case RUN_STATE_EVENT_TYPE_PANIC:
        act_idx = qapi_enum_parse(&PanicAction_lookup, action, -1, errp);
        qmp_panic_set_action(act_idx, NULL);
        break;
    case RUN_STATE_EVENT_TYPE_WATCHDOG:
        if (select_watchdog_action(action) == -1) {
            error_report("unknown watchdog action parameter");
            exit(1);
        }
        break;
    default:
        /*
         * The event and action types are checked for validity in the calls to
         * qapi_enum_parse(), which will cause an exit if the requested event or
         * action are invalid, since error_fatal is used as the error parameter.
         * This case is unreachable unless those conditions change.
         */
        g_assert_not_reached();
    }

    return 0;
}

/*
 * Parse provided -action arguments from cmdline.
 */
int runstate_action_parse(QemuOptsList *opts_list, const char *optarg)
{
    QemuOpts *opts;

    if (!strcmp(optarg, "help")) {
        runstate_action_help();
        return -1;
    }

    opts = qemu_opts_parse_noisily(opts_list, optarg, false);
    if (!opts) {
        return -1;
    }
    return 0;
}

/*
 * Process all the -action parameters parsed from cmdline.
 */
int process_runstate_actions(void *opaque, QemuOpts *opts, Error **errp)
{
    if (qemu_opt_foreach(opts, set_runstate_action, NULL, errp)) {
        return -1;
    }
    return 0;
}
