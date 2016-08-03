/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "migration/colo.h"
#include "io/channel-buffer.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration/failover.h"
#include "qapi-event.h"
#include "net/net.h"
#include "net/filter.h"
#include "net/vhost_net.h"

static bool vmstate_loading;

#define COLO_BUFFER_BASE_SIZE (4 * 1024 * 1024)

typedef struct COLOListNode {
    void *opaque;
    QLIST_ENTRY(COLOListNode) node;
} COLOListNode;

static QLIST_HEAD(, COLOListNode) COLOBufferFilters =
    QLIST_HEAD_INITIALIZER(COLOBufferFilters);

bool colo_supported(void)
{
    return true;
}

bool migration_in_colo_state(void)
{
    MigrationState *s = migrate_get_current();

    return (s->state == MIGRATION_STATUS_COLO);
}

bool migration_incoming_in_colo_state(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();

    return mis && (mis->state == MIGRATION_STATUS_COLO);
}

static bool colo_runstate_is_stopped(void)
{
    return runstate_check(RUN_STATE_COLO) || !runstate_is_running();
}

static void secondary_vm_do_failover(void)
{
    int old_state;
    MigrationIncomingState *mis = migration_incoming_get_current();

    /* Can not do failover during the process of VM's loading VMstate, Or
     * it will break the secondary VM.
     */
    if (vmstate_loading) {
        old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                       FAILOVER_STATUS_RELAUNCH);
        if (old_state != FAILOVER_STATUS_HANDLING) {
            error_report("Unknown error while do failover for secondary VM,"
                         "old_state: %d", old_state);
        }
        return;
    }

    migrate_set_state(&mis->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    if (!autostart) {
        error_report("\"-S\" qemu option will be ignored in secondary side");
        /* recover runstate to normal migration finish state */
        autostart = true;
    }
    /*
     * Make sure COLO incoming thread not block in recv or send,
     * If mis->from_src_file and mis->to_src_file use the same fd,
     * The second shutdown() will return -1, we ignore this value,
     * It is harmless.
     */
    if (mis->from_src_file) {
        qemu_file_shutdown(mis->from_src_file);
    }
    if (mis->to_src_file) {
        qemu_file_shutdown(mis->to_src_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_HANDLING) {
        error_report("Incorrect state (%d) while doing failover for "
                     "secondary VM", old_state);
        return;
    }
    /* Notify COLO incoming thread that failover work is finished */
    qemu_sem_post(&mis->colo_incoming_sem);
    /* For Secondary VM, jump to incoming co */
    if (mis->migration_incoming_co) {
        qemu_coroutine_enter(mis->migration_incoming_co);
    }
}

static void colo_set_filter_status(const char *status, Error **errp)
{
    struct COLOListNode *e, *next;
    NetFilterState *nf;

    QLIST_FOREACH_SAFE(e, &COLOBufferFilters, node, next) {
        nf = e->opaque;
        object_property_set_str(OBJECT(nf), status, "status", errp);
    }
}

static void primary_vm_do_failover(void)
{
    MigrationState *s = migrate_get_current();
    int old_state;
    Error *local_err = NULL;

    migrate_set_state(&s->state, MIGRATION_STATUS_COLO,
                      MIGRATION_STATUS_COMPLETED);

    /*
     * Wake up COLO thread which may blocked in recv() or send(),
     * The s->rp_state.from_dst_file and s->to_dst_file may use the
     * same fd, but we still shutdown the fd for twice, it is harmless.
     */
    if (s->to_dst_file) {
        qemu_file_shutdown(s->to_dst_file);
    }
    if (s->rp_state.from_dst_file) {
        qemu_file_shutdown(s->rp_state.from_dst_file);
    }

    old_state = failover_set_state(FAILOVER_STATUS_HANDLING,
                                   FAILOVER_STATUS_COMPLETED);
    if (old_state != FAILOVER_STATUS_HANDLING) {
        error_report("Incorrect state (%d) while doing failover for Primary VM",
                     old_state);
        return;
    }

    colo_set_filter_status("off", &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    /* Notify COLO thread that failover work is finished */
    qemu_sem_post(&s->colo_exit_sem);
}

void colo_do_failover(MigrationState *s)
{
    /* Make sure VM stopped while failover happened. */
    if (!colo_runstate_is_stopped()) {
        vm_stop_force_state(RUN_STATE_COLO);
    }

    if (get_colo_mode() == COLO_MODE_PRIMARY) {
        primary_vm_do_failover();
    } else {
        secondary_vm_do_failover();
    }
}

static void colo_send_message(QEMUFile *f, COLOMessage msg,
                              Error **errp)
{
    int ret;

    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return;
    }
    qemu_put_be32(f, msg);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't send COLO message");
    }
    trace_colo_send_message(COLOMessage_lookup[msg]);
}

static void colo_send_message_value(QEMUFile *f, COLOMessage msg,
                                    uint64_t value, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    colo_send_message(f, msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_put_be64(f, value);
    qemu_fflush(f);

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to send value for message:%s",
                         COLOMessage_lookup[msg]);
    }
}

static COLOMessage colo_receive_message(QEMUFile *f, Error **errp)
{
    COLOMessage msg;
    int ret;

    msg = qemu_get_be32(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Can't receive COLO message");
        return msg;
    }
    if (msg >= COLO_MESSAGE__MAX) {
        error_setg(errp, "%s: Invalid message", __func__);
        return msg;
    }
    trace_colo_receive_message(COLOMessage_lookup[msg]);
    return msg;
}

static void colo_receive_check_message(QEMUFile *f, COLOMessage expect_msg,
                                       Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msg != expect_msg) {
        error_setg(errp, "Unexpected COLO message %d, expected %d",
                          msg, expect_msg);
    }
}

static uint64_t colo_receive_message_value(QEMUFile *f, uint32_t expect_msg,
                                           Error **errp)
{
    Error *local_err = NULL;
    uint64_t value;
    int ret;

    colo_receive_check_message(f, expect_msg, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return 0;
    }

    value = qemu_get_be64(f);
    ret = qemu_file_get_error(f);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to get value for COLO message: %s",
                         COLOMessage_lookup[expect_msg]);
    }
    return value;
}

static int colo_do_checkpoint_transaction(MigrationState *s,
                                          QIOChannelBuffer *bioc,
                                          QEMUFile *fb)
{
    Error *local_err = NULL;
    int ret = -1;

    colo_send_message(s->to_dst_file, COLO_MESSAGE_CHECKPOINT_REQUEST,
                      &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                    COLO_MESSAGE_CHECKPOINT_REPLY, &local_err);
    if (local_err) {
        goto out;
    }
    /* Reset channel-buffer directly */
    qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);
    bioc->usage = 0;

    qemu_mutex_lock_iothread();
    if (failover_request_is_active()) {
        qemu_mutex_unlock_iothread();
        goto out;
    }
    vm_stop_force_state(RUN_STATE_COLO);
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("run", "stop");
    /*
     * Failover request bh could be called after vm_stop_force_state(),
     * So we need check failover_request_is_active() again.
     */
    if (failover_request_is_active()) {
        goto out;
    }
    /* Stop buffer filter and flush the buffered packets */
    colo_set_filter_status("off", &local_err);
    if (local_err) {
        goto out;
    }
    colo_send_message(s->to_dst_file, COLO_MESSAGE_VMSTATE_SEND, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_mutex_lock_iothread();
    /*
     * Only save VM's live state, which not including device state.
     * TODO: We may need a timeout mechanism to prevent COLO process
     * to be blocked here.
     */
    qemu_savevm_live_state(s->to_dst_file);
    /* Note: device state is saved into buffer */
    ret = qemu_save_device_state(fb);
    qemu_mutex_unlock_iothread();
    if (ret < 0) {
        error_report("Save device state error");
        goto out;
    }
    qemu_fflush(fb);

    /*
     * We need the size of the VMstate data in Secondary side,
     * With which we can decide how much data should be read.
     */
    colo_send_message_value(s->to_dst_file, COLO_MESSAGE_VMSTATE_SIZE,
                            bioc->usage, &local_err);
    if (local_err) {
        goto out;
    }

    qemu_put_buffer(s->to_dst_file, bioc->data, bioc->usage);
    qemu_fflush(s->to_dst_file);
    ret = qemu_file_get_error(s->to_dst_file);
    if (ret < 0) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_RECEIVED, &local_err);
    if (local_err) {
        goto out;
    }

    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_VMSTATE_LOADED, &local_err);
    if (local_err) {
        goto out;
    }

    if (colo_shutdown_requested) {
        colo_send_message(s->to_dst_file, COLO_MESSAGE_GUEST_SHUTDOWN,
                          &local_err);
        if (local_err) {
            error_free(local_err);
            /* Go on the shutdown process and throw the error message */
            error_report("Failed to send shutdown message to SVM");
        }
        qemu_fflush(s->to_dst_file);
        colo_shutdown_requested = 0;
        qemu_system_shutdown_request_core();
        /* Fix me: Just let the colo thread exit ? */
        qemu_thread_exit(0);
    }

    colo_set_filter_status("on", &local_err);
    if (local_err) {
        goto out;
    }
    ret = 0;

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

out:
    if (local_err) {
        error_report_err(local_err);
    }
    return ret;
}

static int colo_prepare_before_save(MigrationState *s)
{
    int ret;

    /* Disable block migration */
    s->params.blk = 0;
    s->params.shared = 0;
    qemu_savevm_state_begin(s->to_dst_file, &s->params);
    ret = qemu_file_get_error(s->to_dst_file);
    if (ret < 0) {
        error_report("Save VM state begin error");
    }
    return ret;
}

void colo_add_buffer_filter(Notifier *notifier, void *data)
{
    char *netdev_id = data;
    NetFilterState *nf;
    char filter_name[128];
    Object *filter;
    COLOListNode *filternode;

    snprintf(filter_name, sizeof(filter_name),
            "%scolo", netdev_id);

    filter = object_new_with_props(TYPE_FILTER_BUFFER,
                        object_get_objects_root(),
                        filter_name, NULL,
                        "netdev", netdev_id,
                        "status", "off",
                        NULL);
    if (!filter) {
        return;
    }
    nf =  NETFILTER(filter);
    /* Only buffer the packets that sent out by VM */
    nf->direction = NET_FILTER_DIRECTION_RX;
    filternode = g_new0(COLOListNode, 1);
    filternode->opaque = nf;
    QLIST_INSERT_HEAD(&COLOBufferFilters, filternode, node);
}

static void colo_process_checkpoint(MigrationState *s)
{
    QIOChannelBuffer *bioc;
    QEMUFile *fb = NULL;
    int64_t current_time, checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    Error *local_err = NULL;
    int ret;

    failover_init_state();

    colo_set_filter_status("on", &local_err);
    if (local_err) {
        goto out;
    }

    s->rp_state.from_dst_file = qemu_file_get_return_path(s->to_dst_file);
    if (!s->rp_state.from_dst_file) {
        error_report("Open QEMUFile from_dst_file failed");
        goto out;
    }

    ret = colo_prepare_before_save(s);
    if (ret < 0) {
        goto out;
    }

    /*
     * Wait for Secondary finish loading VM states and enter COLO
     * restore.
     */
    colo_receive_check_message(s->rp_state.from_dst_file,
                       COLO_MESSAGE_CHECKPOINT_READY, &local_err);
    if (local_err) {
        goto out;
    }
    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_fopen_channel_output(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    qemu_mutex_lock_iothread();
    vm_start();
    qemu_mutex_unlock_iothread();
    trace_colo_vm_state_change("stop", "run");

    ret = global_state_store();
    if (ret < 0) {
        goto out;
    }

    while (s->state == MIGRATION_STATUS_COLO) {
        if (failover_request_is_active()) {
            error_report("failover request");
            goto out;
        }

        current_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
        if ((current_time - checkpoint_time <
            s->parameters.x_checkpoint_delay) &&
            !colo_shutdown_requested) {
            int64_t delay_ms;

            delay_ms = s->parameters.x_checkpoint_delay -
                       (current_time - checkpoint_time);
            g_usleep(delay_ms * 1000);
        }
        ret = colo_do_checkpoint_transaction(s, bioc, fb);
        if (ret < 0) {
            goto out;
        }
        checkpoint_time = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    }

out:
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }

    qemu_fclose(fb);
    /*
     * There are only two reasons we can go here, some error happened.
     * Or the user triggered failover.
     */
    if (!failover_request_is_active()) {
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_ERROR, NULL);
    } else {
        qapi_event_send_colo_exit(COLO_MODE_PRIMARY,
                                  COLO_EXIT_REASON_REQUEST, NULL);
    }

    /* Hope this not to be too long to wait here */
    qemu_sem_wait(&s->colo_exit_sem);
    qemu_sem_destroy(&s->colo_exit_sem);
    /*
     * Must be called after failover BH is completed,
     * Or the failover BH may shutdown the wrong fd that
     * re-used by other threads after we release here.
     */
    if (s->rp_state.from_dst_file) {
        qemu_fclose(s->rp_state.from_dst_file);
    }
}

void migrate_start_colo_process(MigrationState *s)
{
    qemu_mutex_unlock_iothread();
    qemu_sem_init(&s->colo_exit_sem, 0);
    migrate_set_state(&s->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);
    colo_process_checkpoint(s);
    qemu_mutex_lock_iothread();
}

static void colo_wait_handle_message(QEMUFile *f, int *checkpoint_request,
                                     Error **errp)
{
    COLOMessage msg;
    Error *local_err = NULL;

    msg = colo_receive_message(f, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    switch (msg) {
    case COLO_MESSAGE_CHECKPOINT_REQUEST:
        *checkpoint_request = 1;
        break;
    case COLO_MESSAGE_GUEST_SHUTDOWN:
        qemu_mutex_lock_iothread();
        vm_stop_force_state(RUN_STATE_COLO);
        qemu_system_shutdown_request_core();
        qemu_mutex_unlock_iothread();
        /*
         * The main thread will be exit and terminate the whole
         * process, do need some cleanup ?
         */
        qemu_thread_exit(0);
    default:
        *checkpoint_request = 0;
        error_setg(errp, "Got unknown COLO message: %d", msg);
        break;
    }
}

static int colo_prepare_before_load(QEMUFile *f)
{
    int ret;

    ret = qemu_loadvm_state_begin(f);
    if (ret < 0) {
        error_report("Load VM state begin error, ret=%d", ret);
    }
    return ret;
}

void *colo_process_incoming_thread(void *opaque)
{
    MigrationIncomingState *mis = opaque;
    QEMUFile *fb = NULL;
    QIOChannelBuffer *bioc = NULL; /* Cache incoming device state */
    uint64_t total_size;
    uint64_t value;
    Error *local_err = NULL;
    int ret;

    qemu_sem_init(&mis->colo_incoming_sem, 0);

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      MIGRATION_STATUS_COLO);

    failover_init_state();

    mis->to_src_file = qemu_file_get_return_path(mis->from_src_file);
    if (!mis->to_src_file) {
        error_report("colo incoming thread: Open QEMUFile to_src_file failed");
        goto out;
    }
    /*
     * Note: We set the fd to unblocked in migration incoming coroutine,
     * But here we are in the colo incoming thread, so it is ok to set the
     * fd back to blocked.
     */
    qemu_file_set_blocking(mis->from_src_file, true);

    ret = colo_init_ram_cache();
    if (ret < 0) {
        error_report("Failed to initialize ram cache");
        goto out;
    }

    bioc = qio_channel_buffer_new(COLO_BUFFER_BASE_SIZE);
    fb = qemu_fopen_channel_input(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    ret = colo_prepare_before_load(mis->from_src_file);
    if (ret < 0) {
        goto out;
    }

    colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_READY,
                      &local_err);
    if (local_err) {
        goto out;
    }

    while (mis->state == MIGRATION_STATUS_COLO) {
        int request;

        colo_wait_handle_message(mis->from_src_file, &request, &local_err);
        if (local_err) {
            goto out;
        }
        assert(request);
        if (failover_request_is_active()) {
            error_report("failover request");
            goto out;
        }

        /* FIXME: This is unnecessary for periodic checkpoint mode */
        colo_send_message(mis->to_src_file, COLO_MESSAGE_CHECKPOINT_REPLY,
                     &local_err);
        if (local_err) {
            goto out;
        }

        colo_receive_check_message(mis->from_src_file,
                           COLO_MESSAGE_VMSTATE_SEND, &local_err);
        if (local_err) {
            goto out;
        }

        ret = qemu_loadvm_state_main(mis->from_src_file, mis);
        if (ret < 0) {
            error_report("Load VM's live state (ram) error");
            goto out;
        }

        value = colo_receive_message_value(mis->from_src_file,
                                 COLO_MESSAGE_VMSTATE_SIZE, &local_err);
        if (local_err) {
            goto out;
        }

        /*
         * Read VM device state data into channel buffer,
         * It's better to re-use the memory allocated.
         * Here we need to handle the channel buffer directly.
         */
        if (value > bioc->capacity) {
            bioc->capacity = value;
            bioc->data = g_realloc(bioc->data, bioc->capacity);
        }
        total_size = qemu_get_buffer(mis->from_src_file, bioc->data, value);
        if (total_size != value) {
            error_report("Got %lu VMState data, less than expected %lu",
                         total_size, value);
            ret = -EINVAL;
            goto out;
        }
        bioc->usage = total_size;
        qio_channel_io_seek(QIO_CHANNEL(bioc), 0, 0, NULL);

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_RECEIVED,
                     &local_err);
        if (local_err) {
            goto out;
        }

        qemu_mutex_lock_iothread();
        qemu_system_reset(VMRESET_SILENT);
        vmstate_loading = true;
        colo_flush_ram_cache();
        ret = qemu_load_device_state(fb);
        if (ret < 0) {
            error_report("COLO: load device state failed");
            qemu_mutex_unlock_iothread();
            goto out;
        }

        vmstate_loading = false;
        qemu_mutex_unlock_iothread();

        if (failover_get_state() == FAILOVER_STATUS_RELAUNCH) {
            failover_set_state(FAILOVER_STATUS_RELAUNCH, FAILOVER_STATUS_NONE);
            failover_request_active(NULL);
            goto out;
        }

        colo_send_message(mis->to_src_file, COLO_MESSAGE_VMSTATE_LOADED,
                     &local_err);
        if (local_err) {
            goto out;
        }
    }

out:
    vmstate_loading = false;
    /* Throw the unreported error message after exited from loop */
    if (local_err) {
        error_report_err(local_err);
    }
    if (!failover_request_is_active()) {
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_ERROR, NULL);
    } else {
        qapi_event_send_colo_exit(COLO_MODE_SECONDARY,
                                  COLO_EXIT_REASON_REQUEST, NULL);
    }

    if (fb) {
        qemu_fclose(fb);
    }
    /*
     * We can ensure BH is hold the global lock, and will join COLO
     * incoming thread, so here it is not necessary to lock here again,
     * Or there will be a deadlock error.
     */
    colo_release_ram_cache();

    /* Hope this not to be too long to loop here */
    qemu_sem_wait(&mis->colo_incoming_sem);
    qemu_sem_destroy(&mis->colo_incoming_sem);
    /* Must be called after failover BH is completed */
    if (mis->to_src_file) {
        qemu_fclose(mis->to_src_file);
    }
    migration_incoming_exit_colo();

    return NULL;
}

bool colo_handle_shutdown(void)
{
    /*
     * If VM is in COLO-FT mode, we need do some significant work before
     * respond to the shutdown request. Besides, Secondary VM will ignore
     * the shutdown request from users.
     */
    if (migration_incoming_in_colo_state()) {
        return true;
    }
    if (migration_in_colo_state()) {
        colo_shutdown_requested = 1;
        return true;
    }
    return false;
}
