/*
 * HMP commands related to migration
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "block/qapi.h"
#include "block/block-global-state.h"
#include "migration/snapshot.h"
#include "monitor/hmp.h"
#include "monitor/hmp-completion.h"
#include "monitor/monitor.h"
#include "monitor/monitor-internal.h"
#include "monitor/monitor-hmp-internal.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "system/runstate.h"
#include "ui/qemu-spice.h"
#include "system/system.h"
#include "options.h"
#include "migration.h"

static void migration_global_dump(MonitorHMP *hmp)
{
    MigrationState *ms = migrate_get_current();

    monitor_hmp_printf(hmp, "Globals:\n");
    monitor_hmp_printf(hmp, "  store-global-state: %s\n",
                       ms->store_global_state ? "on" : "off");
    monitor_hmp_printf(hmp, "  only-migratable: %s\n",
                       only_migratable ? "on" : "off");
    monitor_hmp_printf(hmp, "  send-configuration: %s\n",
                       ms->send_configuration ? "on" : "off");
    monitor_hmp_printf(hmp, "  send-section-footer: %s\n",
                       ms->send_section_footer ? "on" : "off");
    monitor_hmp_printf(hmp, "  send-switchover-start: %s\n",
                       ms->send_switchover_start ? "on" : "off");
    monitor_hmp_printf(hmp, "  clear-bitmap-shift: %u\n",
                       ms->clear_bitmap_shift);
}

static const gchar *format_time_str(uint64_t us)
{
    const char *units[] = {"us", "ms", "sec"};
    int index = 0;

    while (us >= 1000 && index + 1 < ARRAY_SIZE(units)) {
        us /= 1000;
        index++;
    }

    return g_strdup_printf("%"PRIu64" %s", us, units[index]);
}

static void migration_dump_blocktime(MonitorHMP *hmp, MigrationInfo *info)
{
    if (info->has_postcopy_blocktime) {
        monitor_hmp_printf(hmp, "Postcopy Blocktime (ms): %" PRIu32 "\n",
                           info->postcopy_blocktime);
    }

    if (info->has_postcopy_vcpu_blocktime) {
        uint32List *item = info->postcopy_vcpu_blocktime;
        const char *sep = "";
        int count = 0;

        monitor_hmp_printf(hmp, "Postcopy vCPU Blocktime (ms):\n [");

        while (item) {
            monitor_hmp_printf(hmp, "%s%"PRIu32, sep, item->value);
            item = item->next;
            /* Each line 10 vcpu results, newline if there's more */
            sep = ((++count % 10 == 0) && item) ? ",\n  " : ", ";
        }
        monitor_hmp_printf(hmp, "]\n");
    }

    if (info->has_postcopy_latency) {
        monitor_hmp_printf(hmp, "Postcopy Latency (ns): %" PRIu64 "\n",
                           info->postcopy_latency);
    }

    if (info->has_postcopy_non_vcpu_latency) {
        monitor_hmp_printf(hmp, "Postcopy non-vCPU Latency (ns): %" PRIu64 "\n",
                           info->postcopy_non_vcpu_latency);
    }

    if (info->has_postcopy_vcpu_latency) {
        uint64List *item = info->postcopy_vcpu_latency;
        const char *sep = "";
        int count = 0;

        monitor_hmp_printf(hmp, "Postcopy vCPU Latencies (ns):\n [");

        while (item) {
            monitor_hmp_printf(hmp, "%s%"PRIu64, sep, item->value);
            item = item->next;
            /* Each line 10 vcpu results, newline if there's more */
            sep = ((++count % 10 == 0) && item) ? ",\n  " : ", ";
        }
        monitor_hmp_printf(hmp, "]\n");
    }

    if (info->has_postcopy_latency_dist) {
        uint64List *item = info->postcopy_latency_dist;
        int count = 0;

        monitor_hmp_printf(hmp, "Postcopy Latency Distribution:\n");

        while (item) {
            g_autofree const gchar *from = format_time_str(1UL << count);
            g_autofree const gchar *to = format_time_str(1UL << (count + 1));

            monitor_hmp_printf(hmp, "  [ %8s - %8s ]: %10"PRIu64"\n",
                               from, to, item->value);
            item = item->next;
            count++;
        }
    }
}

void hmp_info_migrate(MonitorHMP *hmp, const QDict *qdict)
{
    bool show_all = qdict_get_try_bool(qdict, "all", false);
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);

    if (info->blocked_reasons) {
        strList *reasons = info->blocked_reasons;
        monitor_hmp_printf(hmp, "Outgoing migration blocked:\n");
        while (reasons) {
            monitor_hmp_printf(hmp, "  %s\n", reasons->value);
            reasons = reasons->next;
        }
    }

    if (info->has_status) {
        monitor_hmp_printf(hmp, "Status: \t\t%s",
                           MigrationStatus_str(info->status));
        if ((info->status == MIGRATION_STATUS_FAILED ||
             info->status == MIGRATION_STATUS_POSTCOPY_PAUSED) &&
            info->error_desc) {
            monitor_hmp_printf(hmp, " (%s)\n", info->error_desc);
        } else {
            monitor_hmp_printf(hmp, "\n");
        }

        if (info->total_time) {
            monitor_hmp_printf(hmp, "Time (ms): \t\ttotal=%" PRIu64,
                               info->total_time);
            if (info->has_setup_time) {
                monitor_hmp_printf(hmp, ", setup=%" PRIu64,
                                   info->setup_time);
            }
            if (info->has_expected_downtime) {
                monitor_hmp_printf(hmp, ", exp_down=%" PRIu64,
                                   info->expected_downtime);
            }
            if (info->has_downtime) {
                monitor_hmp_printf(hmp, ", down=%" PRIu64,
                                   info->downtime);
            }
            monitor_hmp_printf(hmp, "\n");
        }
    }

    if (info->has_remaining) {
        g_autofree char *remaining = size_to_str(info->remaining);
        monitor_hmp_printf(hmp, "Remaining: \t\t%s\n", remaining);
    }

    if (info->has_socket_address) {
        SocketAddressList *addr;

        monitor_hmp_printf(hmp, "Sockets: [\n");

        for (addr = info->socket_address; addr; addr = addr->next) {
            char *s = socket_uri(addr->value);
            monitor_hmp_printf(hmp, "\t%s\n", s);
            g_free(s);
        }
        monitor_hmp_printf(hmp, "]\n");
    }

    if (info->ram) {
        g_autofree char *str_psize = size_to_str(info->ram->page_size);
        g_autofree char *str_total = size_to_str(info->ram->total);
        g_autofree char *str_transferred = size_to_str(info->ram->transferred);
        g_autofree char *str_remaining = size_to_str(info->ram->remaining);
        g_autofree char *str_precopy = size_to_str(info->ram->precopy_bytes);
        g_autofree char *str_multifd = size_to_str(info->ram->multifd_bytes);
        g_autofree char *str_postcopy = size_to_str(info->ram->postcopy_bytes);

        monitor_hmp_printf(hmp, "RAM info:\n");
        monitor_hmp_printf(hmp, "  Throughput (Mbps): \t%0.2f\n",
                           info->ram->mbps);
        monitor_hmp_printf(hmp, "  Sizes: \t\tpagesize=%s, total=%s\n",
                           str_psize, str_total);
        monitor_hmp_printf(hmp, "  Transfers: \t\ttransferred=%s, remain=%s\n",
                           str_transferred, str_remaining);
        monitor_hmp_printf(hmp, "    Channels: \t\tprecopy=%s, "
                           "multifd=%s, postcopy=%s",
                           str_precopy, str_multifd, str_postcopy);

        if (info->vfio) {
            g_autofree char *str_vfio = size_to_str(info->vfio->transferred);

            monitor_hmp_printf(hmp, ", vfio=%s", str_vfio);
        }
        monitor_hmp_printf(hmp, "\n");

        monitor_hmp_printf(hmp, "    Page Types: \tnormal=%" PRIu64
                           ", zero=%" PRIu64 "\n",
                           info->ram->normal, info->ram->duplicate);
        monitor_hmp_printf(hmp, "  Page Rates (pps): \ttransfer=%" PRIu64,
                           info->ram->pages_per_second);
        if (info->ram->dirty_pages_rate) {
            monitor_hmp_printf(hmp, ", dirty=%" PRIu64,
                               info->ram->dirty_pages_rate);
        }
        monitor_hmp_printf(hmp, "\n");

        monitor_hmp_printf(hmp, "  Others: \t\tdirty_syncs=%" PRIu64,
                           info->ram->dirty_sync_count);
        if (info->ram->postcopy_requests) {
            monitor_hmp_printf(hmp, ", postcopy_req=%" PRIu64,
                               info->ram->postcopy_requests);
        }
        if (info->ram->downtime_bytes) {
            monitor_hmp_printf(hmp, ", downtime_bytes=%" PRIu64,
                               info->ram->downtime_bytes);
        }
        if (info->ram->dirty_sync_missed_zero_copy) {
            monitor_hmp_printf(hmp, ", zerocopy_fallbacks=%" PRIu64,
                               info->ram->dirty_sync_missed_zero_copy);
        }
        monitor_hmp_printf(hmp, "\n");
    }

    if (!show_all) {
        goto out;
    }

    migration_global_dump(hmp);

    if (info->xbzrle_cache) {
        monitor_hmp_printf(hmp, "XBZRLE: size=%" PRIu64
                           ", transferred=%" PRIu64
                           ", pages=%" PRIu64
                           ", miss=%" PRIu64 "\n"
                           "  miss_rate=%0.2f"
                           ", encode_rate=%0.2f"
                           ", overflow=%" PRIu64 "\n",
                           info->xbzrle_cache->cache_size,
                           info->xbzrle_cache->bytes,
                           info->xbzrle_cache->pages,
                           info->xbzrle_cache->cache_miss,
                           info->xbzrle_cache->cache_miss_rate,
                           info->xbzrle_cache->encoding_rate,
                           info->xbzrle_cache->overflow);
    }

    if (info->has_cpu_throttle_percentage) {
        monitor_hmp_printf(hmp, "CPU Throttle (%%): %" PRIu64 "\n",
                           info->cpu_throttle_percentage);
    }

    if (info->has_dirty_limit_throttle_time_per_round) {
        monitor_hmp_printf(hmp, "Dirty-limit Throttle (us): %" PRIu64 "\n",
                           info->dirty_limit_throttle_time_per_round);
    }

    if (info->has_dirty_limit_ring_full_time) {
        monitor_hmp_printf(hmp, "Dirty-limit Ring Full (us): %" PRIu64 "\n",
                           info->dirty_limit_ring_full_time);
    }

    migration_dump_blocktime(hmp, info);
out:
    qapi_free_MigrationInfo(info);
}

void hmp_info_migrate_capabilities(MonitorHMP *hmp, const QDict *qdict)
{
    MigrationCapabilityStatusList *caps, *cap;

    warn_report("info migrate_capabilities is deprecated;"
                " use info migrate_parameters instead");

    caps = qmp_query_migrate_capabilities(NULL);

    if (caps) {
        for (cap = caps; cap; cap = cap->next) {
            monitor_hmp_printf(hmp, "%s: %s\n",
                               MigrationCapability_str(cap->value->capability),
                               cap->value->state ? "on" : "off");
        }
    }

    qapi_free_MigrationCapabilityStatusList(caps);
}

static void hmp_migrate_print_qobject(MonitorHMP *hmp, const char *label,
                                      QObject *obj)
{
    const char *sep;

    if (!obj) {
        return;
    }

    /*
     * Put a space after labels
     * foo: bar
     *     ^
     */
    if (label && label[0] && label[strlen(label) - 1] == ':') {
        sep = " ";
    } else {
        sep = "";
    }

    switch (qobject_type(obj)) {
    case QTYPE_QNUM: {
        g_autofree char *str = qnum_to_string(qobject_to(QNum, obj));

        monitor_hmp_printf(hmp, "%s%s%s", label, sep, str);
        break;
    }
    case QTYPE_QSTRING:
        monitor_hmp_printf(hmp, "%s%s%s", label, sep,
                           qstring_get_str(qobject_to(QString, obj)));
        break;
    case QTYPE_QDICT: {
        QDict *d = qobject_to(QDict, obj);
        const QDictEntry *e;
        int i = 0;

        for (e = qdict_first(d); e; e = qdict_next(d, e), i++) {
            g_autofree char *l = g_strdup_printf("%s:", qdict_entry_key(e));
            if (i) {
                monitor_hmp_printf(hmp, " ");
            }
            hmp_migrate_print_qobject(hmp, l, qdict_entry_value(e));
        }
        break;
    }
    case QTYPE_QLIST: {
        const QListEntry *e;

        monitor_hmp_printf(hmp, "%s", label);

        for (e = qlist_first(qobject_to(QList, obj)); e; e = qlist_next(e)) {
            /*
             * In the first iteration, this is the space after the
             * colon, otherwise it's the space between list
             * elements.
             */
            monitor_hmp_printf(hmp, " ");
            hmp_migrate_print_qobject(hmp, "", e->value);
        }

        break;
    }
    case QTYPE_QBOOL:
        monitor_hmp_printf(hmp, "%s%s%s", label, sep,
                           qbool_get_bool(qobject_to(QBool, obj)) ?
                           "on" : "off");
        break;
    case QTYPE_NONE:
    case QTYPE_QNULL:
    default:
        g_assert_not_reached();
        break;
    }
}

void hmp_info_migrate_parameters(MonitorHMP *hmp, const QDict *qdict)
{
    MigrationParameters *params = qmp_query_migrate_parameters(NULL);
    g_autoptr(QDict) d;
    const QDictEntry *e;

    assert(params);

    d = migrate_params_to_dict(params, NULL);
    for (e = qdict_first(d); e; e = qdict_next(d, e)) {
        g_autofree char *label = g_strdup_printf("%s:", qdict_entry_key(e));

        hmp_migrate_print_qobject(hmp, label, qdict_entry_value(e));
        monitor_hmp_printf(hmp, "\n");
    }

    qapi_free_MigrationParameters(params);
}

void hmp_loadvm(MonitorHMP *hmp, const QDict *qdict)
{
    RunState saved_state = runstate_get();

    const char *name = qdict_get_str(qdict, "name");
    Error *err = NULL;

    vm_stop(RUN_STATE_RESTORE_VM);

    if (load_snapshot(name, NULL, false, NULL, &err)) {
        load_snapshot_resume(saved_state);
    }

    hmp_handle_error(hmp, err);
}

void hmp_savevm(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;

    save_snapshot(qdict_get_try_str(qdict, "name"),
                  true, NULL, false, NULL, &err);
    hmp_handle_error(hmp, err);
}

void hmp_delvm(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *name = qdict_get_str(qdict, "name");

    delete_snapshot(name, false, NULL, &err);
    hmp_handle_error(hmp, err);
}

void hmp_migrate_cancel(MonitorHMP *hmp, const QDict *qdict)
{
    qmp_migrate_cancel(NULL);
}

void hmp_migrate_continue(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *state = qdict_get_str(qdict, "state");
    int val = qapi_enum_parse(&MigrationStatus_lookup, state, -1, &err);

    if (val >= 0) {
        qmp_migrate_continue(val, &err);
    }

    hmp_handle_error(hmp, err);
}

void hmp_migrate_incoming(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *uri = qdict_get_str(qdict, "uri");
    MigrationChannelList *caps = NULL;
    g_autoptr(MigrationChannel) channel = NULL;

    if (!migrate_uri_parse(uri, &channel, &err)) {
        goto end;
    }
    QAPI_LIST_PREPEND(caps, g_steal_pointer(&channel));

    qmp_migrate_incoming(NULL, true, caps, true, false, &err);
    qapi_free_MigrationChannelList(caps);

end:
    hmp_handle_error(hmp, err);
}

void hmp_migrate_recover(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *uri = qdict_get_str(qdict, "uri");

    qmp_migrate_recover(uri, &err);

    hmp_handle_error(hmp, err);
}

void hmp_migrate_pause(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;

    qmp_migrate_pause(&err);

    hmp_handle_error(hmp, err);
}


void hmp_migrate_set_capability(MonitorHMP *hmp, const QDict *qdict)
{
    const char *cap = qdict_get_str(qdict, "capability");
    bool state = qdict_get_bool(qdict, "state");
    Error *err = NULL;
    MigrationCapabilityStatusList *caps = NULL;
    MigrationCapabilityStatus *value;
    int val;

    warn_report("migrate_set_capability is deprecated;"
                " use migrate_set_parameter instead");

    val = qapi_enum_parse(&MigrationCapability_lookup, cap, -1, &err);
    if (val < 0) {
        goto end;
    }

    value = g_malloc0(sizeof(*value));
    value->capability = val;
    value->state = state;
    QAPI_LIST_PREPEND(caps, value);
    qmp_migrate_set_capabilities(caps, &err);
    qapi_free_MigrationCapabilityStatusList(caps);

end:
    hmp_handle_error(hmp, err);
}

static void hmp_migrate_set_parameter_legacy(MonitorHMP *hmp, const QDict *qdict)
{
    const char *param = qdict_get_str(qdict, "parameter");
    const char *valuestr = qdict_get_str(qdict, "value");
    MigrationParameters *p = g_new0(MigrationParameters, 1);
    Error *err = NULL;

    if (g_str_equal(param, "cpr-exec-command")) {
        /*
         * NOTE: g_autofree will only auto g_free() the strv array when
         * needed, it will not free the strings within the array. It's
         * intentional: when strv is set, the ownership of the strings will
         * always be passed to p->cpr_exec_command via QAPI_LIST_APPEND().
         */
        g_autofree char **strv = NULL;
        g_autoptr(GError) gerr = NULL;
        strList **tail = &p->cpr_exec_command;

        if (!g_shell_parse_argv(valuestr, NULL, &strv, &gerr)) {
            error_setg(&err, "%s", gerr->message);
            goto cleanup;
        }
        for (int i = 0; strv[i]; i++) {
            QAPI_LIST_APPEND(tail, strv[i]);
        }
        p->has_cpr_exec_command = true;

    } else {
        g_assert_not_reached();
    }

    if (err) {
        goto cleanup;
    }

    qmp_migrate_set_parameters(p, &err);

cleanup:
    qapi_free_MigrationParameters(p);
    hmp_handle_error(hmp, err);
}

static void hmp_migrate_set_parameter_qapi(MonitorHMP *hmp, const QDict *qdict)
{
    const char *param = qdict_get_str(qdict, "parameter");
    const char *valuestr = qdict_get_str(qdict, "value");
    g_autoptr(QDict) input = qdict_new();
    g_autoptr(MigrationParameters) p = NULL;
    Visitor *v;
    Error *err = NULL;

    /* the same as keyval_parse(), but here there's no need to parse */
    qdict_put_obj(input, param, QOBJECT(qstring_from_str(valuestr)));

    v = qobject_input_visitor_new_keyval(QOBJECT(input));
    if (visit_type_MigrationParameters(v, NULL, &p, &err)) {
        qmp_migrate_set_parameters(p, &err);
    }

    visit_free(v);
    hmp_handle_error(hmp, err);
}

void hmp_migrate_set_parameter(MonitorHMP *hmp, const QDict *qdict)
{
    const char *param = qdict_get_str(qdict, "parameter");

    if (g_str_equal(param, "block-bitmap-mapping")) {
        Error *err = NULL;

        error_setg(&err, "The %s parameter can only be set through QMP", param);
        hmp_handle_error(hmp, err);
        return;
    }

    /* this has a non-standard setter */
    if (g_str_equal(param, "cpr-exec-command")) {
        return hmp_migrate_set_parameter_legacy(hmp, qdict);
    }

    hmp_migrate_set_parameter_qapi(hmp, qdict);
}

void hmp_migrate_start_postcopy(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    qmp_migrate_start_postcopy(&err);
    hmp_handle_error(hmp, err);
}

#ifdef CONFIG_REPLICATION
void hmp_x_colo_lost_heartbeat(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;

    qmp_x_colo_lost_heartbeat(&err);
    hmp_handle_error(hmp, err);
}
#endif

typedef struct HMPMigrationStatus {
    QEMUTimer *timer;
    Monitor *mon;
} HMPMigrationStatus;

static void hmp_migrate_status_cb(void *opaque)
{
    HMPMigrationStatus *status = opaque;
    MigrationInfo *info;

    info = qmp_query_migrate(NULL);
    if (!info->has_status || info->status == MIGRATION_STATUS_ACTIVE ||
        info->status == MIGRATION_STATUS_SETUP) {
        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 1000);
    } else {
        if (info->error_desc) {
            error_report("%s", info->error_desc);
        }
        monitor_resume(status->mon);
        timer_free(status->timer);
        g_free(status);
    }

    qapi_free_MigrationInfo(info);
}

void hmp_migrate(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    bool detach = qdict_get_try_bool(qdict, "detach", false);
    bool resume = qdict_get_try_bool(qdict, "resume", false);
    const char *uri = qdict_get_str(qdict, "uri");
    const char *uri_cpr = qdict_get_try_str(qdict, "uri-cpr");
    Error *err = NULL;
    g_autoptr(MigrationChannelList) caps = NULL;
    g_autoptr(MigrationChannel) channel = NULL;
    g_autoptr(MigrationChannel) channel_cpr = NULL;

    if (!migrate_uri_parse(uri, &channel, &err)) {
        hmp_handle_error(hmp, err);
        return;
    }
    QAPI_LIST_PREPEND(caps, g_steal_pointer(&channel));

    if (uri_cpr) {
        if (migrate_mode() != MIG_MODE_CPR_TRANSFER) {
            error_setg(&err, "-c can only be used in cpr-transfer mode");
            hmp_handle_error(hmp, err);
            return;
        }

        if (!migrate_uri_parse(uri_cpr, &channel_cpr, &err)) {
            hmp_handle_error(hmp, err);
            return;
        }

        channel_cpr->channel_type = MIGRATION_CHANNEL_TYPE_CPR;
        QAPI_LIST_PREPEND(caps, g_steal_pointer(&channel_cpr));
    }

    qmp_migrate(NULL, true, caps, true, resume, &err);
    if (hmp_handle_error(hmp, err)) {
        return;
    }

    if (!detach) {
        HMPMigrationStatus *status;

        if (!hmp->use_readline) {
            monitor_hmp_printf(hmp, "terminal does not allow synchronous "
                               "migration, continuing detached\n");
            return;
        }
        monitor_suspend(mon);

        status = g_malloc0(sizeof(*status));
        status->mon = mon;
        status->timer = timer_new_ms(QEMU_CLOCK_REALTIME, hmp_migrate_status_cb,
                                          status);
        timer_mod(status->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }
}

void migrate_set_capability_completion(ReadLineState *rs, int nb_args,
                                       const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
            readline_add_completion_of(rs, str, MigrationCapability_str(i));
        }
    } else if (nb_args == 3) {
        readline_add_completion_of(rs, str, "on");
        readline_add_completion_of(rs, str, "off");
    }
}

void migrate_set_parameter_completion(ReadLineState *rs, int nb_args,
                                      const char *str)
{
    g_autoptr(QDict) d = NULL;
    const QDictEntry *e;
    size_t len;

    /* Temporarily borrow the global parameters */
    d = migrate_params_to_dict(&migrate_get_current()->parameters,
                               &error_abort);
    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        for (e = qdict_first(d); e; e = qdict_next(d, e)) {
            const char *key = qdict_entry_key(e);
            readline_add_completion_of(rs, str, key);
        }
    }
}

static void vm_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    BlockDriverState *bs;
    BdrvNextIterator it;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    len = strlen(str);
    readline_set_completion_index(rs, len);

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        SnapshotInfoList *snapshots, *snapshot;
        bool ok = false;

        if (bdrv_can_snapshot(bs)) {
            ok = bdrv_query_snapshot_info_list(bs, &snapshots, NULL) == 0;
        }
        if (!ok) {
            continue;
        }

        snapshot = snapshots;
        while (snapshot) {
            readline_add_completion_of(rs, str, snapshot->value->name);
            readline_add_completion_of(rs, str, snapshot->value->id);
            snapshot = snapshot->next;
        }
        qapi_free_SnapshotInfoList(snapshots);
    }

}

void delvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

void loadvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}
