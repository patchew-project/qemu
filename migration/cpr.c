/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "migration.h"
#include "migration/snapshot.h"
#include "chardev/char.h"
#include "migration/misc.h"
#include "migration/cpr.h"
#include "migration/global_state.h"
#include "qemu-file-channel.h"
#include "qemu-file.h"
#include "savevm.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/runstate.h"
#include "sysemu/runstate-action.h"
#include "sysemu/sysemu.h"
#include "sysemu/replay.h"
#include "sysemu/xen.h"
#include "hw/vfio/vfio-common.h"
#include "hw/virtio/vhost.h"
#include "qemu/env.h"

static int cpr_is_active;

bool cpr_active(void)
{
    return cpr_is_active;
}

QEMUFile *qf_file_open(const char *path, int flags, int mode,
                              const char *name, Error **errp)
{
    QIOChannelFile *fioc;
    QIOChannel *ioc;
    QEMUFile *f;

    if (flags & O_RDWR) {
        error_setg(errp, "qf_file_open %s: O_RDWR not supported", path);
        return 0;
    }

    fioc = qio_channel_file_new_path(path, flags, mode, errp);
    if (!fioc) {
        return 0;
    }

    ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    f = (flags & O_WRONLY) ? qemu_fopen_channel_output(ioc) :
                             qemu_fopen_channel_input(ioc);
    object_unref(OBJECT(fioc));
    return f;
}

static int preserve_fd(const char *name, const char *val, void *handle)
{
    qemu_clr_cloexec(atoi(val));
    return 0;
}

void cprsave(const char *file, CprMode mode, Error **errp)
{
    int ret = 0;
    QEMUFile *f;
    int saved_vm_running = runstate_is_running();
    bool restart = (mode == CPR_MODE_RESTART);
    bool reboot = (mode == CPR_MODE_REBOOT);

    if (reboot && qemu_ram_volatile(errp)) {
        return;
    }

    if (restart && xen_enabled()) {
        error_setg(errp, "xen does not support cprsave restart");
        return;
    }

    if (migrate_colo_enabled()) {
        error_setg(errp, "error: cprsave does not support x-colo");
        return;
    }

    if (replay_mode != REPLAY_MODE_NONE) {
        error_setg(errp, "error: cprsave does not support replay");
        return;
    }

    f = qf_file_open(file, O_CREAT | O_WRONLY | O_TRUNC, 0600, "cprsave", errp);
    if (!f) {
        return;
    }

    ret = global_state_store();
    if (ret) {
        error_setg(errp, "Error saving global state");
        qemu_fclose(f);
        return;
    }
    if (runstate_check(RUN_STATE_SUSPENDED)) {
        /* Update timers_state before saving.  Suspend did not so do. */
        cpu_disable_ticks();
    }
    vm_stop(RUN_STATE_SAVE_VM);

    cpr_is_active = true;
    ret = qemu_save_device_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, QERR_IO_ERROR);
        goto err;
    }

    if (ret < 0) {
        if (!*errp) {
            error_setg(errp, "Error %d while saving VM state", ret);
        }
        goto err;
    }

    if (reboot) {
        shutdown_action = SHUTDOWN_ACTION_POWEROFF;
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    } else if (restart) {
        if (!qemu_chr_cpr_capable(errp)) {
            goto err;
        }
        if (vfio_cprsave(errp)) {
            goto err;
        }
        walkenv(FD_PREFIX, preserve_fd, 0);
        vhost_dev_reset_all();
        setenv("QEMU_START_FREEZE", "", 1);
        qemu_system_exec_request();
    }
    goto done;

err:
    if (saved_vm_running) {
        vm_start();
    }
done:
    cpr_is_active = false;
    return;
}

void cprload(const char *file, Error **errp)
{
    QEMUFile *f;
    int ret;
    RunState state;

    if (runstate_is_running()) {
        error_setg(errp, "cprload called for a running VM");
        return;
    }

    f = qf_file_open(file, O_RDONLY, 0, "cprload", errp);
    if (!f) {
        return;
    }

    if (qemu_get_be32(f) != QEMU_VM_FILE_MAGIC ||
        qemu_get_be32(f) != QEMU_VM_FILE_VERSION) {
        error_setg(errp, "error: %s is not a vmstate file", file);
        return;
    }

    ret = qemu_load_device_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, "Error %d while loading VM state", ret);
        return;
    }

    if (vfio_cprload(errp)) {
        return;
    }

    state = global_state_get_runstate();
    if (state == RUN_STATE_RUNNING) {
        vm_start();
    } else {
        runstate_set(state);
        if (runstate_check(RUN_STATE_SUSPENDED)) {
            qemu_system_start_on_wake_request();
        }
    }
}
