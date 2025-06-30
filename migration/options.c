/*
 * QEMU migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *   Orit Wasserman <owasserm@redhat.com>
 *   Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "exec/target_page.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/visitor.h"
#include "qobject/qdict.h"
#include "qobject/qnull.h"
#include "system/runstate.h"
#include "migration/colo.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "ram.h"
#include "options.h"
#include "system/kvm.h"

/* Maximum migrate downtime set to 2000 seconds */
#define MAX_MIGRATE_DOWNTIME_SECONDS 2000
#define MAX_MIGRATE_DOWNTIME (MAX_MIGRATE_DOWNTIME_SECONDS * 1000)

#define MAX_THROTTLE  (128 << 20)      /* Migration transfer speed throttling */

/* Time in milliseconds we are allowed to stop the source,
 * for sending the last part */
#define DEFAULT_MIGRATE_SET_DOWNTIME 300

/* Define default autoconverge cpu throttle migration parameters */
#define DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD 50
#define DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL 20
#define DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT 10
#define DEFAULT_MIGRATE_MAX_CPU_THROTTLE 99

/* Migration XBZRLE default cache size */
#define DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE (64 * 1024 * 1024)

/* The delay time (in ms) between two COLO checkpoints */
#define DEFAULT_MIGRATE_X_CHECKPOINT_DELAY (200 * 100)
#define DEFAULT_MIGRATE_MULTIFD_CHANNELS 2
#define DEFAULT_MIGRATE_MULTIFD_COMPRESSION MULTIFD_COMPRESSION_NONE
/* 0: means nocompress, 1: best speed, ... 9: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL 1
/*
 * 1: best speed, ... 9: best compress ratio
 * There is some nuance here. Refer to QATzip documentation to understand
 * the mapping of QATzip levels to standard deflate levels.
 */
#define DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL 1

/* 0: means nocompress, 1: best speed, ... 20: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL 1

/* Background transfer rate for postcopy, 0 means unlimited, note
 * that page requests can still exceed this limit.
 */
#define DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH 0

/*
 * Parameters for self_announce_delay giving a stream of RARP/ARP
 * packets after migration.
 */
#define DEFAULT_MIGRATE_ANNOUNCE_INITIAL  50
#define DEFAULT_MIGRATE_ANNOUNCE_MAX     550
#define DEFAULT_MIGRATE_ANNOUNCE_ROUNDS    5
#define DEFAULT_MIGRATE_ANNOUNCE_STEP    100

const PropertyInfo qdev_prop_StrOrNull;
#define DEFINE_PROP_STR_OR_NULL(_name, _state, _field)                  \
    DEFINE_PROP(_name, _state, _field, qdev_prop_StrOrNull, StrOrNull *, \
                .set_default = true)

#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD     1000    /* milliseconds */
#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT            1       /* MB/s */

const Property migration_properties[] = {
    DEFINE_PROP_BOOL("store-global-state", MigrationState,
                     store_global_state, true),
    DEFINE_PROP_BOOL("send-configuration", MigrationState,
                     send_configuration, true),
    DEFINE_PROP_BOOL("send-section-footer", MigrationState,
                     send_section_footer, true),
    DEFINE_PROP_BOOL("send-switchover-start", MigrationState,
                     send_switchover_start, true),
    DEFINE_PROP_BOOL("multifd-flush-after-each-section", MigrationState,
                      multifd_flush_after_each_section, false),
    DEFINE_PROP_UINT8("x-clear-bitmap-shift", MigrationState,
                      clear_bitmap_shift, CLEAR_BITMAP_SHIFT_DEFAULT),
    DEFINE_PROP_BOOL("x-preempt-pre-7-2", MigrationState,
                     preempt_pre_7_2, false),
    DEFINE_PROP_BOOL("multifd-clean-tls-termination", MigrationState,
                     multifd_clean_tls_termination, true),

    /* Migration parameters */
    DEFINE_PROP_UINT8("x-throttle-trigger-threshold", MigrationState,
                      parameters.throttle_trigger_threshold,
                      DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD),
    DEFINE_PROP_UINT8("x-cpu-throttle-initial", MigrationState,
                      parameters.cpu_throttle_initial,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL),
    DEFINE_PROP_UINT8("x-cpu-throttle-increment", MigrationState,
                      parameters.cpu_throttle_increment,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT),
    DEFINE_PROP_BOOL("x-cpu-throttle-tailslow", MigrationState,
                      parameters.cpu_throttle_tailslow, false),
    DEFINE_PROP_SIZE("x-max-bandwidth", MigrationState,
                      parameters.max_bandwidth, MAX_THROTTLE),
    DEFINE_PROP_SIZE("avail-switchover-bandwidth", MigrationState,
                      parameters.avail_switchover_bandwidth, 0),
    DEFINE_PROP_UINT64("x-downtime-limit", MigrationState,
                      parameters.downtime_limit,
                      DEFAULT_MIGRATE_SET_DOWNTIME),
    DEFINE_PROP_UINT32("x-checkpoint-delay", MigrationState,
                      parameters.x_checkpoint_delay,
                      DEFAULT_MIGRATE_X_CHECKPOINT_DELAY),
    DEFINE_PROP_UINT8("multifd-channels", MigrationState,
                      parameters.multifd_channels,
                      DEFAULT_MIGRATE_MULTIFD_CHANNELS),
    DEFINE_PROP_MULTIFD_COMPRESSION("multifd-compression", MigrationState,
                      parameters.multifd_compression,
                      DEFAULT_MIGRATE_MULTIFD_COMPRESSION),
    DEFINE_PROP_UINT8("multifd-zlib-level", MigrationState,
                      parameters.multifd_zlib_level,
                      DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL),
    DEFINE_PROP_UINT8("multifd-qatzip-level", MigrationState,
                      parameters.multifd_qatzip_level,
                      DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL),
    DEFINE_PROP_UINT8("multifd-zstd-level", MigrationState,
                      parameters.multifd_zstd_level,
                      DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL),
    DEFINE_PROP_SIZE("xbzrle-cache-size", MigrationState,
                      parameters.xbzrle_cache_size,
                      DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE),
    DEFINE_PROP_SIZE("max-postcopy-bandwidth", MigrationState,
                      parameters.max_postcopy_bandwidth,
                      DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH),
    DEFINE_PROP_UINT8("max-cpu-throttle", MigrationState,
                      parameters.max_cpu_throttle,
                      DEFAULT_MIGRATE_MAX_CPU_THROTTLE),
    DEFINE_PROP_SIZE("announce-initial", MigrationState,
                      parameters.announce_initial,
                      DEFAULT_MIGRATE_ANNOUNCE_INITIAL),
    DEFINE_PROP_SIZE("announce-max", MigrationState,
                      parameters.announce_max,
                      DEFAULT_MIGRATE_ANNOUNCE_MAX),
    DEFINE_PROP_SIZE("announce-rounds", MigrationState,
                      parameters.announce_rounds,
                      DEFAULT_MIGRATE_ANNOUNCE_ROUNDS),
    DEFINE_PROP_SIZE("announce-step", MigrationState,
                      parameters.announce_step,
                      DEFAULT_MIGRATE_ANNOUNCE_STEP),
    DEFINE_PROP_STR_OR_NULL("tls-creds", MigrationState, parameters.tls_creds),
    DEFINE_PROP_STR_OR_NULL("tls-hostname", MigrationState,
                            parameters.tls_hostname),
    DEFINE_PROP_STR_OR_NULL("tls-authz", MigrationState, parameters.tls_authz),
    DEFINE_PROP_UINT64("x-vcpu-dirty-limit-period", MigrationState,
                       parameters.x_vcpu_dirty_limit_period,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD),
    DEFINE_PROP_UINT64("vcpu-dirty-limit", MigrationState,
                       parameters.vcpu_dirty_limit,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT),
    DEFINE_PROP_MIG_MODE("mode", MigrationState,
                      parameters.mode,
                      MIG_MODE_NORMAL),
    DEFINE_PROP_ZERO_PAGE_DETECTION("zero-page-detection", MigrationState,
                       parameters.zero_page_detection,
                       ZERO_PAGE_DETECTION_MULTIFD),
    DEFINE_PROP_BOOL("x-xbzrle",
                     MigrationState, parameters.xbzrle, false),
    DEFINE_PROP_BOOL("x-rdma-pin-all",
                     MigrationState, parameters.rdma_pin_all, false),
    DEFINE_PROP_BOOL("x-auto-converge",
                     MigrationState, parameters.auto_converge, false),
    DEFINE_PROP_BOOL("x-zero-blocks",
                     MigrationState, parameters.zero_blocks, false),
    DEFINE_PROP_BOOL("x-events",
                     MigrationState, parameters.events, false),
    DEFINE_PROP_BOOL("x-postcopy-ram",
                     MigrationState, parameters.postcopy_ram, false),
    DEFINE_PROP_BOOL("x-postcopy-preempt",
                     MigrationState, parameters.postcopy_preempt, false),
    DEFINE_PROP_BOOL("x-colo",
                     MigrationState, parameters.x_colo, false),
    DEFINE_PROP_BOOL("x-release-ram",
                     MigrationState, parameters.release_ram, false),
    DEFINE_PROP_BOOL("x-return-path",
                     MigrationState, parameters.return_path, false),
    DEFINE_PROP_BOOL("x-multifd",
                     MigrationState, parameters.multifd, false),
    DEFINE_PROP_BOOL("x-background-snapshot",
                     MigrationState, parameters.background_snapshot, false),
#ifdef CONFIG_LINUX
    DEFINE_PROP_BOOL("x-zero-copy-send",
                     MigrationState, parameters.zero_copy_send, false),
#endif
    DEFINE_PROP_BOOL("x-switchover-ack",
                     MigrationState, parameters.switchover_ack, false),
    DEFINE_PROP_BOOL("x-dirty-limit",
                     MigrationState, parameters.dirty_limit, false),
    DEFINE_PROP_BOOL("mapped-ram",
                     MigrationState, parameters.mapped_ram, false),
};
const size_t migration_properties_count = ARRAY_SIZE(migration_properties);

/*
 * qdev property for TLS options handling via '-global migration'
 * command line.
 */
static void set_StrOrNull(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    const Property *prop = opaque;
    StrOrNull **ptr = object_field_prop_ptr(obj, prop);
    StrOrNull *str_or_null = g_new0(StrOrNull, 1);

    /*
     * Only str to keep compatibility, QNULL was never used via
     * command line.
     */
    str_or_null->type = QTYPE_QSTRING;
    if (!visit_type_str(v, name, &str_or_null->u.s, errp)) {
        return;
    }

    qapi_free_StrOrNull(*ptr);
    *ptr = str_or_null;
}

static void release_StrOrNull(Object *obj, const char *name, void *opaque)
{
    const Property *prop = opaque;
    qapi_free_StrOrNull(*(StrOrNull **)object_field_prop_ptr(obj, prop));
}

static void set_default_value_tls_opt(ObjectProperty *op, const Property *prop)
{
    /*
     * Initialization to the empty string here is important so
     * query-migrate-parameters doesn't need to deal with a NULL value
     * when it's called before any TLS option has been set.
     */
    object_property_set_default_str(op, "");
}

const PropertyInfo qdev_prop_StrOrNull = {
    .type  = "StrOrNull",
    .set = set_StrOrNull,
    .release = release_StrOrNull,
    .set_default_value = set_default_value_tls_opt,
};

bool migrate_auto_converge(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.auto_converge;
}

bool migrate_send_switchover_start(void)
{
    MigrationState *s = migrate_get_current();

    return s->send_switchover_start;
}

bool migrate_background_snapshot(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.background_snapshot;
}

bool migrate_colo(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.x_colo;
}

bool migrate_dirty_bitmaps(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.dirty_bitmaps;
}

bool migrate_dirty_limit(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.dirty_limit;
}

bool migrate_events(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.events;
}

bool migrate_mapped_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.mapped_ram;
}

bool migrate_ignore_shared(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.x_ignore_shared;
}

bool migrate_late_block_activate(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.late_block_activate;
}

bool migrate_multifd(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.multifd;
}

bool migrate_pause_before_switchover(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.pause_before_switchover;
}

bool migrate_postcopy_blocktime(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.postcopy_blocktime;
}

bool migrate_postcopy_preempt(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.postcopy_preempt;
}

bool migrate_postcopy_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.postcopy_ram;
}

bool migrate_rdma_pin_all(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.rdma_pin_all;
}

bool migrate_release_ram(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.release_ram;
}

bool migrate_return_path(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.return_path;
}

bool migrate_switchover_ack(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.switchover_ack;
}

bool migrate_validate_uuid(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.validate_uuid;
}

bool migrate_xbzrle(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.xbzrle;
}

bool migrate_zero_copy_send(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.zero_copy_send;
}

bool migrate_multifd_flush_after_each_section(void)
{
    MigrationState *s = migrate_get_current();

    return s->multifd_flush_after_each_section;
}

bool migrate_postcopy(void)
{
    return migrate_postcopy_ram() || migrate_dirty_bitmaps();
}

bool migrate_rdma(void)
{
    MigrationState *s = migrate_get_current();

    return s->rdma_migration;
}

typedef enum WriteTrackingSupport {
    WT_SUPPORT_UNKNOWN = 0,
    WT_SUPPORT_ABSENT,
    WT_SUPPORT_AVAILABLE,
    WT_SUPPORT_COMPATIBLE
} WriteTrackingSupport;

static
WriteTrackingSupport migrate_query_write_tracking(void)
{
    /* Check if kernel supports required UFFD features */
    if (!ram_write_tracking_available()) {
        return WT_SUPPORT_ABSENT;
    }
    /*
     * Check if current memory configuration is
     * compatible with required UFFD features.
     */
    if (!ram_write_tracking_compatible()) {
        return WT_SUPPORT_AVAILABLE;
    }

    return WT_SUPPORT_COMPATIBLE;
}

static bool migrate_incoming_started(void)
{
    return !!migration_incoming_get_current()->transport_data;
}

bool migrate_rdma_caps_check(const MigrationParameters *params, Error **errp)
{
    if (params->xbzrle) {
        error_setg(errp, "RDMA and XBZRLE can't be used together");
        return false;
    }
    if (params->multifd) {
        error_setg(errp, "RDMA and multifd can't be used together");
        return false;
    }
    if (params->postcopy_ram) {
        error_setg(errp, "RDMA and postcopy-ram can't be used together");
        return false;
    }

    return true;
}

bool migrate_caps_check(const MigrationParameters *new, Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();
    bool postcopy_already_on = s->parameters.postcopy_ram;
    ERRP_GUARD();

    if (new->zero_blocks) {
        warn_report("zero-blocks capability is deprecated");
    }

#ifndef CONFIG_REPLICATION
    if (new->x_colo) {
        error_setg(errp, "QEMU compiled without replication module"
                   " can't enable COLO");
        error_append_hint(errp, "Please enable replication before COLO.\n");
        return false;
    }
#endif

    if (new->postcopy_ram) {
        /* This check is reasonably expensive, so only when it's being
         * set the first time, also it's only the destination that needs
         * special support.
         */
        if (!postcopy_already_on &&
            runstate_check(RUN_STATE_INMIGRATE) &&
            !postcopy_ram_supported_by_host(mis, errp)) {
            error_prepend(errp, "Postcopy is not supported: ");
            return false;
        }

        if (new->x_ignore_shared) {
            error_setg(errp, "Postcopy is not compatible with ignore-shared");
            return false;
        }
    }

    if (new->background_snapshot) {
        WriteTrackingSupport wt_support;

        /*
         * Check if 'background-snapshot' capability is supported by
         * host kernel and compatible with guest memory configuration.
         */
        wt_support = migrate_query_write_tracking();
        if (wt_support < WT_SUPPORT_AVAILABLE) {
            error_setg(errp, "Background-snapshot is not supported by host kernel");
            return false;
        }
        if (wt_support < WT_SUPPORT_COMPATIBLE) {
            error_setg(errp, "Background-snapshot is not compatible "
                    "with guest memory configuration");
            return false;
        }

        if (new->postcopy_ram ||
            new->dirty_bitmaps ||
            new->postcopy_blocktime ||
            new->late_block_activate ||
            new->return_path ||
            new->multifd ||
            new->pause_before_switchover ||
            new->auto_converge ||
            new->release_ram ||
            new->rdma_pin_all ||
            new->xbzrle ||
            new->x_colo ||
            new->validate_uuid ||
            new->zero_copy_send) {
            error_setg(errp,
                       "Background-snapshot is not compatible with "
                       "currently set capabilities");
            return false;
        }
    }

#ifdef CONFIG_LINUX
    if (new->zero_copy_send &&
        (!new->multifd || new->xbzrle ||
         migrate_multifd_compression() || migrate_tls())) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#else
    if (new->zero_copy_send) {
        error_setg(errp,
                   "Zero copy currently only available on Linux");
        return false;
    }
#endif

    if (new->postcopy_preempt) {
        if (!new->postcopy_ram) {
            error_setg(errp, "Postcopy preempt requires postcopy-ram");
            return false;
        }

        if (!migrate_postcopy_preempt() && migrate_incoming_started()) {
            error_setg(errp,
                       "Postcopy preempt must be set before incoming starts");
            return false;
        }
    }

    if (new->multifd) {
        if (!migrate_multifd() && migrate_incoming_started()) {
            error_setg(errp, "Multifd must be set before incoming starts");
            return false;
        }
    }

    if (new->switchover_ack) {
        if (!new->return_path) {
            error_setg(errp, "Capability 'switchover-ack' requires capability "
                             "'return-path'");
            return false;
        }
    }
    if (new->dirty_limit) {
        if (new->auto_converge) {
            error_setg(errp, "dirty-limit conflicts with auto-converge"
                       " either of then available currently");
            return false;
        }

        if (!kvm_enabled() || !kvm_dirty_ring_enabled()) {
            error_setg(errp, "dirty-limit requires KVM with accelerator"
                   " property 'dirty-ring-size' set");
            return false;
        }
    }

    if (new->multifd) {
        if (new->xbzrle) {
            error_setg(errp, "Multifd is not compatible with xbzrle");
            return false;
        }
    }

    if (new->mapped_ram) {
        if (new->xbzrle) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with xbzrle");
            return false;
        }

        if (new->postcopy_ram) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with postcopy");
            return false;
        }
    }

    /*
     * On destination side, check the cases that capability is being set
     * after incoming thread has started.
     */
    if (migrate_rdma() && !migrate_rdma_caps_check(new, errp)) {
        return false;
    }
    return true;
}

static bool *migrate_capability_get_addr(MigrationParameters *params, int i)
{
    bool *cap_addr = NULL;

    switch (i) {
    case MIGRATION_CAPABILITY_XBZRLE:
        cap_addr = &params->xbzrle;
        break;
    case MIGRATION_CAPABILITY_RDMA_PIN_ALL:
        cap_addr = &params->rdma_pin_all;
        break;
    case MIGRATION_CAPABILITY_AUTO_CONVERGE:
        cap_addr = &params->auto_converge;
        break;
    case MIGRATION_CAPABILITY_ZERO_BLOCKS:
        cap_addr = &params->zero_blocks;
        break;
    case MIGRATION_CAPABILITY_EVENTS:
        cap_addr = &params->events;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_RAM:
        cap_addr = &params->postcopy_ram;
        break;
    case MIGRATION_CAPABILITY_X_COLO:
        cap_addr = &params->x_colo;
        break;
    case MIGRATION_CAPABILITY_RELEASE_RAM:
        cap_addr = &params->release_ram;
        break;
    case MIGRATION_CAPABILITY_RETURN_PATH:
        cap_addr = &params->return_path;
        break;
    case MIGRATION_CAPABILITY_PAUSE_BEFORE_SWITCHOVER:
        cap_addr = &params->pause_before_switchover;
        break;
    case MIGRATION_CAPABILITY_MULTIFD:
        cap_addr = &params->multifd;
        break;
    case MIGRATION_CAPABILITY_DIRTY_BITMAPS:
        cap_addr = &params->dirty_bitmaps;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_BLOCKTIME:
        cap_addr = &params->postcopy_blocktime;
        break;
    case MIGRATION_CAPABILITY_LATE_BLOCK_ACTIVATE:
        cap_addr = &params->late_block_activate;
        break;
    case MIGRATION_CAPABILITY_X_IGNORE_SHARED:
        cap_addr = &params->x_ignore_shared;
        break;
    case MIGRATION_CAPABILITY_VALIDATE_UUID:
        cap_addr = &params->validate_uuid;
        break;
    case MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT:
        cap_addr = &params->background_snapshot;
        break;
    case MIGRATION_CAPABILITY_ZERO_COPY_SEND:
        cap_addr = &params->zero_copy_send;
        break;
    case MIGRATION_CAPABILITY_POSTCOPY_PREEMPT:
        cap_addr = &params->postcopy_preempt;
        break;
    case MIGRATION_CAPABILITY_SWITCHOVER_ACK:
        cap_addr = &params->switchover_ack;
        break;
    case MIGRATION_CAPABILITY_DIRTY_LIMIT:
        cap_addr = &params->dirty_limit;
        break;
    case MIGRATION_CAPABILITY_MAPPED_RAM:
        cap_addr = &params->mapped_ram;
        break;
    default:
        g_assert_not_reached();
    }

    return cap_addr;
}

/* Compatibility for code that reads capabilities in a loop */
bool migrate_capability_get_compat(MigrationParameters *params, int i)
{
    return *(migrate_capability_get_addr(params, i));
}

/* Compatibility for code that writes capabilities in a loop */
void migrate_capability_set_compat(MigrationParameters *params, int i, bool val)
{
    *(migrate_capability_get_addr(params, i)) = val;
}

/*
 * Set capabilities for compatibility with the old
 * migrate-set-capabilities command.
 */
void migrate_capabilities_set_compat(MigrationParameters *params,
                                     MigrationCapabilityStatusList *caps)
{
    MigrationCapabilityStatusList *cap;

    for (cap = caps; cap; cap = cap->next) {
        migrate_capability_set_compat(params, cap->value->capability,
                                      cap->value->state);
    }
}

MigrationCapabilityStatusList *qmp_query_migrate_capabilities(Error **errp)
{
    MigrationCapabilityStatusList *head = NULL, **tail = &head;
    MigrationCapabilityStatus *caps;
    MigrationState *s = migrate_get_current();
    int i;

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        caps = g_malloc0(sizeof(*caps));
        caps->capability = i;
        caps->state = migrate_capability_get_compat(&s->parameters, i);
        QAPI_LIST_APPEND(tail, caps);
    }

    return head;
}

void qmp_migrate_set_capabilities(MigrationCapabilityStatusList *capabilities,
                                  Error **errp)
{
    MigrationState *s = migrate_get_current();
    g_autoptr(MigrationParameters) params = NULL;

    if (migration_is_running() || migration_in_colo_state()) {
        error_setg(errp, "There's a migration process in progress");
        return;
    }

    /*
     * Capabilities validation needs to first copy from s->parameters
     * in case the incoming capabilities have a capability that
     * conflicts with another that's already set.
     */
    params = QAPI_CLONE(MigrationParameters, &s->parameters);
    migrate_capabilities_set_compat(params, capabilities);

    if (!migrate_caps_check(params, errp)) {
        return;
    }

    migrate_capabilities_set_compat(&s->parameters, capabilities);
}

/* parameters */

const BitmapMigrationNodeAliasList *migrate_block_bitmap_mapping(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.block_bitmap_mapping;
}

bool migrate_has_block_bitmap_mapping(void)
{
    MigrationState *s = migrate_get_current();

    return s->has_block_bitmap_mapping;
}

uint32_t migrate_checkpoint_delay(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.x_checkpoint_delay;
}

uint8_t migrate_cpu_throttle_increment(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.cpu_throttle_increment;
}

uint8_t migrate_cpu_throttle_initial(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.cpu_throttle_initial;
}

bool migrate_cpu_throttle_tailslow(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.cpu_throttle_tailslow;
}

bool migrate_direct_io(void)
{
    MigrationState *s = migrate_get_current();

    /*
     * O_DIRECT is only supported with mapped-ram and multifd.
     *
     * mapped-ram is needed because filesystems impose restrictions on
     * O_DIRECT IO alignment (see MAPPED_RAM_FILE_OFFSET_ALIGNMENT).
     *
     * multifd is needed to keep the unaligned portion of the stream
     * isolated to the main migration thread while multifd channels
     * process the aligned data with O_DIRECT enabled.
     */
    return s->parameters.direct_io && s->parameters.mapped_ram &&
        s->parameters.multifd;
}

uint64_t migrate_downtime_limit(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.downtime_limit;
}

uint8_t migrate_max_cpu_throttle(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.max_cpu_throttle;
}

uint64_t migrate_max_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.max_bandwidth;
}

uint64_t migrate_avail_switchover_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.avail_switchover_bandwidth;
}

uint64_t migrate_max_postcopy_bandwidth(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.max_postcopy_bandwidth;
}

MigMode migrate_mode(void)
{
    MigMode mode = cpr_get_incoming_mode();

    if (mode == MIG_MODE_NONE) {
        mode = migrate_get_current()->parameters.mode;
    }

    assert(mode >= 0 && mode < MIG_MODE__MAX);
    return mode;
}

int migrate_multifd_channels(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.multifd_channels;
}

MultiFDCompression migrate_multifd_compression(void)
{
    MigrationState *s = migrate_get_current();

    assert(s->parameters.multifd_compression < MULTIFD_COMPRESSION__MAX);
    return s->parameters.multifd_compression;
}

int migrate_multifd_zlib_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.multifd_zlib_level;
}

int migrate_multifd_qatzip_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.multifd_qatzip_level;
}

int migrate_multifd_zstd_level(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.multifd_zstd_level;
}

uint8_t migrate_throttle_trigger_threshold(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.throttle_trigger_threshold;
}

const char *migrate_tls_authz(void)
{
    MigrationState *s = migrate_get_current();

    if (*s->parameters.tls_authz->u.s) {
        return s->parameters.tls_authz->u.s;
    }

    return NULL;
}

const char *migrate_tls_creds(void)
{
    MigrationState *s = migrate_get_current();

    if (*s->parameters.tls_creds->u.s) {
        return s->parameters.tls_creds->u.s;
    }

    return NULL;
}

const char *migrate_tls_hostname(void)
{
    MigrationState *s = migrate_get_current();

    if (*s->parameters.tls_hostname->u.s) {
        return s->parameters.tls_hostname->u.s;
    }

    return NULL;
}

bool migrate_tls(void)
{
    return !!migrate_tls_creds();
}

uint64_t migrate_vcpu_dirty_limit_period(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.x_vcpu_dirty_limit_period;
}

uint64_t migrate_xbzrle_cache_size(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.xbzrle_cache_size;
}

ZeroPageDetection migrate_zero_page_detection(void)
{
    MigrationState *s = migrate_get_current();

    return s->parameters.zero_page_detection;
}

/* parameters helpers */

AnnounceParameters *migrate_announce_params(void)
{
    static AnnounceParameters ap;

    MigrationState *s = migrate_get_current();

    ap.initial = s->parameters.announce_initial;
    ap.max = s->parameters.announce_max;
    ap.rounds = s->parameters.announce_rounds;
    ap.step = s->parameters.announce_step;

    return &ap;
}

void migrate_tls_opts_free(MigrationParameters *params)
{
    qapi_free_StrOrNull(params->tls_creds);
    qapi_free_StrOrNull(params->tls_hostname);
    qapi_free_StrOrNull(params->tls_authz);
}

/* either non-empty or empty string */
static void tls_opt_to_str(StrOrNull **tls_opt)
{
    StrOrNull *opt = *tls_opt;

    if (!opt) {
        return;
    }

    switch (opt->type) {
    case QTYPE_QSTRING:
        return;
    case QTYPE_QNULL:
        qobject_unref(opt->u.n);
        break;
    default:
        g_assert_not_reached();
    }

    opt->type = QTYPE_QSTRING;
    opt->u.s = g_strdup("");
    *tls_opt = opt;
}

/*
 * query-migrate-parameters expects all members of MigrationParameters
 * to be present, but we cannot mark them non-optional in QAPI because
 * the structure is also used for migrate-set-parameters, which needs
 * the optionality. Force all parameters to be seen as present
 * now. Note that this depends on some form of default being set for
 * every member of MigrationParameters, currently done during qdev
 * init using migration_properties defined in this file. The TLS
 * options are a special case because they don't have a default and
 * need to be normalized before use.
 */
static void migrate_mark_all_params_present(MigrationParameters *p)
{
    int len, n_str_args = 3; /* tls-creds, tls-hostname, tls-authz */
    bool *has_fields[] = {
        &p->has_throttle_trigger_threshold, &p->has_cpu_throttle_initial,
        &p->has_cpu_throttle_increment, &p->has_cpu_throttle_tailslow,
        &p->has_max_bandwidth, &p->has_avail_switchover_bandwidth,
        &p->has_downtime_limit, &p->has_x_checkpoint_delay,
        &p->has_multifd_channels, &p->has_multifd_compression,
        &p->has_multifd_zlib_level, &p->has_multifd_qatzip_level,
        &p->has_multifd_zstd_level, &p->has_xbzrle_cache_size,
        &p->has_max_postcopy_bandwidth, &p->has_max_cpu_throttle,
        &p->has_announce_initial, &p->has_announce_max, &p->has_announce_rounds,
        &p->has_announce_step, &p->has_block_bitmap_mapping,
        &p->has_x_vcpu_dirty_limit_period, &p->has_vcpu_dirty_limit,
        &p->has_mode, &p->has_zero_page_detection, &p->has_direct_io,
        &p->has_xbzrle, &p->has_rdma_pin_all, &p->has_auto_converge,
        &p->has_zero_blocks, &p->has_events, &p->has_postcopy_ram,
        &p->has_x_colo, &p->has_release_ram, &p->has_return_path,
        &p->has_pause_before_switchover, &p->has_multifd, &p->has_dirty_bitmaps,
        &p->has_postcopy_blocktime, &p->has_late_block_activate,
        &p->has_x_ignore_shared, &p->has_validate_uuid,
        &p->has_background_snapshot, &p->has_zero_copy_send,
        &p->has_postcopy_preempt, &p->has_switchover_ack, &p->has_dirty_limit,
        &p->has_mapped_ram,
    };

    len = ARRAY_SIZE(has_fields);
    assert(len + n_str_args == MIGRATION_PARAMETER__MAX);

    for (int i = 0; i < len; i++) {
        *has_fields[i] = true;
    }
}

MigrationParameters *qmp_query_migrate_parameters(Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationParameters *params = QAPI_CLONE(MigrationParameters,
                                             &s->parameters);

    /*
     * The block-bitmap-mapping breaks the expected API of
     * query-migrate-parameters of having all members present. To keep
     * compatibility, only emit this field if it's actually been
     * set. The empty list is a valid value.
     */
    if (!s->has_block_bitmap_mapping) {
        params->has_block_bitmap_mapping = false;
        qapi_free_BitmapMigrationNodeAliasList(params->block_bitmap_mapping);
    }

    return params;
}

void migrate_params_init(MigrationParameters *params)
{
    migrate_mark_all_params_present(params);
}

static void migrate_post_update_params(MigrationParameters *new, Error **errp)
{
    MigrationState *s = migrate_get_current();

    if (new->has_max_bandwidth) {
        if (s->to_dst_file && !migration_in_postcopy()) {
            migration_rate_set(new->max_bandwidth);
        }
    }

    if (new->has_x_checkpoint_delay) {
        colo_checkpoint_delay_set();
    }

    if (new->has_xbzrle_cache_size) {
        xbzrle_cache_resize(new->xbzrle_cache_size, errp);
    }

    if (new->has_max_postcopy_bandwidth) {
        if (s->to_dst_file && migration_in_postcopy()) {
            migration_rate_set(new->max_postcopy_bandwidth);
        }
    }
}

/*
 * Check whether the parameters are valid. Error will be put into errp
 * (if provided). Return true if valid, otherwise false.
 */
bool migrate_params_check(const MigrationParameters *params, Error **errp)
{
    ERRP_GUARD();

    if (params->throttle_trigger_threshold < 1 ||
        params->throttle_trigger_threshold > 100) {
        error_setg(errp, "Option throttle_trigger_threshold expects "
                   "an integer in the range of 1 to 100");
        return false;
    }

    if (params->cpu_throttle_initial < 1 ||
        params->cpu_throttle_initial > 99) {
        error_setg(errp, "Option cpu_throttle_initial expects "
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->cpu_throttle_increment < 1 ||
        params->cpu_throttle_increment > 99) {
        error_setg(errp, "Option cpu_throttle_increment expects "
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->max_bandwidth > SIZE_MAX) {
        error_setg(errp, "Option max_bandwidth expects "
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->avail_switchover_bandwidth > SIZE_MAX) {
        error_setg(errp, "Option avail_switchover_bandwidth expects "
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->downtime_limit > MAX_MIGRATE_DOWNTIME) {
        error_setg(errp, "Option downtime_limit expects "
                   "an integer in the range of 0 to "
                    stringify(MAX_MIGRATE_DOWNTIME)" ms");
        return false;
    }

    if (params->multifd_channels < 1) {
        error_setg(errp, "Option multifd_channels expects "
                   "a value between 1 and 255");
        return false;
    }

    if (params->multifd_zlib_level > 9) {
        error_setg(errp, "Option multifd_zlib_level expects "
                   "a value between 0 and 9");
        return false;
    }

    if (params->multifd_qatzip_level > 9 ||
        params->multifd_qatzip_level < 1) {
        error_setg(errp, "Option multifd_qatzip_level expects "
                   "a value between 1 and 9");
        return false;
    }

    if (params->multifd_zstd_level > 20) {
        error_setg(errp, "Option multifd_zstd_level expects "
                   "a value between 0 and 20");
        return false;
    }

    if (params->xbzrle_cache_size < qemu_target_page_size() ||
        !is_power_of_2(params->xbzrle_cache_size)) {
        error_setg(errp, "Option xbzrle_cache_size expects "
                   "a power of two no less than the target page size");
        return false;
    }

    if (params->max_cpu_throttle < params->cpu_throttle_initial ||
        params->max_cpu_throttle > 99) {
        error_setg(errp, "max_Option cpu_throttle expects "
                   "an integer in the range of cpu_throttle_initial to 99");
        return false;
    }

    if (params->announce_initial > 100000) {
        error_setg(errp, "Option announce_initial expects "
                   "a value between 0 and 100000");
        return false;
    }
    if (params->announce_max > 100000) {
        error_setg(errp, "Option announce_max expects "
                   "a value between 0 and 100000");
        return false;
    }
    if (params->announce_rounds > 1000) {
        error_setg(errp, "Option announce_rounds expects "
                   "a value between 0 and 1000");
        return false;
    }
    if (params->announce_step < 1 ||
        params->announce_step > 10000) {
        error_setg(errp, "Option announce_step expects "
                   "a value between 0 and 10000");
        return false;
    }

    if (!check_dirty_bitmap_mig_alias_map(params->block_bitmap_mapping, errp)) {
        error_prepend(errp, "Invalid mapping given for block-bitmap-mapping: ");
        return false;
    }

#ifdef CONFIG_LINUX
    if (migrate_zero_copy_send() &&
        (params->multifd_compression || *params->tls_creds->u.s)) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#endif

    if (migrate_mapped_ram() &&
        (migrate_multifd_compression() || migrate_tls())) {
        error_setg(errp,
                   "Mapped-ram only available for non-compressed non-TLS multifd migration");
        return false;
    }

    if (params->x_vcpu_dirty_limit_period < 1 ||
        params->x_vcpu_dirty_limit_period > 1000) {
        error_setg(errp, "Option x-vcpu-dirty-limit-period expects "
                   "a value between 1 and 1000");
        return false;
    }

    if (params->vcpu_dirty_limit < 1) {
        error_setg(errp,
                   "Parameter 'vcpu_dirty_limit' must be greater than 1 MB/s");
        return false;
    }

    if (params->direct_io && !qemu_has_direct_io()) {
        error_setg(errp, "No build-time support for direct-io");
        return false;
    }

    if (!migrate_caps_check(params, errp)) {
        return false;
    }

    return true;
}

static bool migrate_params_merge(MigrationParameters *dst,
                                 MigrationParameters *src, Error **errp)
{
    QObject *ret_out = NULL;
    Visitor *v;
    bool ok;

    /* free memory from pointers that are about to be assigned */
    if (src->has_block_bitmap_mapping) {
        qapi_free_BitmapMigrationNodeAliasList(dst->block_bitmap_mapping);
    }

    if (src->tls_creds) {
        qapi_free_StrOrNull(dst->tls_creds);
    }

    if (src->tls_hostname) {
        qapi_free_StrOrNull(dst->tls_hostname);
    }

    if (src->tls_authz) {
        qapi_free_StrOrNull(dst->tls_authz);
    }

    /* read in from src */
    v = qobject_output_visitor_new(&ret_out);
    ok = visit_type_MigrationParameters(v, NULL, &src, errp);
    if (!ok) {
        goto out;
    }
    visit_complete(v, &ret_out);
    visit_free(v);

    /*
     * Write to dst but leave existing fields intact (except for has_*
     * which will be updated according to their presence in src).
     */
    v = qobject_input_visitor_new(ret_out);
    ok = visit_start_struct(v, NULL, NULL, 0, errp);
    if (!ok) {
        goto out;
    }
    ok = visit_type_MigrationParameters_members(v, dst, errp);
    if (!ok) {
        goto out;
    }
    ok = visit_check_struct(v, errp);
    visit_end_struct(v, NULL);
    if (!ok) {
        goto out;
    }

out:
    visit_free(v);
    qobject_unref(ret_out);

    return ok;
}

static void migrate_params_apply(const MigrationParameters *params)
{
    MigrationState *s = migrate_get_current();
    MigrationParameters *cur = &s->parameters;

    assert(bql_locked());

    migrate_tls_opts_free(cur);
    qapi_free_BitmapMigrationNodeAliasList(cur->block_bitmap_mapping);
    QAPI_CLONE_MEMBERS(MigrationParameters, cur, params);
}

void migrate_params_store_defaults(MigrationState *s)
{
    assert(!s->initial_params);

    /*
     * The defaults set for each qdev property in migration_properties
     * will be stored as the default values for each migration
     * parameter. For debugging, using -global can override the
     * defaults.
     */
    s->initial_params = QAPI_CLONE(MigrationParameters, &s->parameters);
}

void qmp_migrate_set_parameters(MigrationParameters *params, Error **errp)
{
    MigrationState *s = migrate_get_current();
    g_autoptr(MigrationParameters) tmp = QAPI_CLONE(MigrationParameters,
                                                    &s->parameters);

    /*
     * Convert QTYPE_QNULL and NULL to the empty string (""). Even
     * though NULL is cleaner to deal with in C code, that would force
     * query-migrate-parameters to convert it once more to the empty
     * string, so avoid that. The migrate_tls_*() helpers that expose
     * the options to the rest of the migration code already use
     * return NULL when the empty string is found.
     */
    tls_opt_to_str(&params->tls_creds);
    tls_opt_to_str(&params->tls_hostname);
    tls_opt_to_str(&params->tls_authz);

    if (!migrate_params_merge(tmp, params, errp)) {
        return;
    }

    /*
     * Mark block_bitmap_mapping as present now while we have the
     * params structure with the user input around.
     */
    if (params->has_block_bitmap_mapping) {
        migrate_get_current()->has_block_bitmap_mapping = true;
    }

    if (migrate_params_check(tmp, errp)) {
        /* mark all present, so they're all copied */
        migrate_mark_all_params_present(tmp);
        migrate_params_apply(tmp);
        migrate_post_update_params(params, errp);
    }
}
