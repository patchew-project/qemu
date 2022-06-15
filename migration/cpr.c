/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/cpr.h"
#include "migration/global_state.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-cpr.h"
#include "qemu-file-channel.h"
#include "qemu-file.h"
#include "savevm.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"

static int cpr_enabled_modes;

void cpr_init(int modes)
{
    cpr_enabled_modes = modes;
}

bool cpr_enabled(CprMode mode)
{
    return !!(cpr_enabled_modes & BIT(mode));
}

static GSList *cpr_blockers[CPR_MODE__MAX];

/*
 * Add blocker for each mode in varargs list, or for all modes if CPR_MODE_ALL
 * is specified.  Caller terminates the list with 0 or CPR_MODE_ALL.  This
 * function takes ownership of *reasonp, and frees it on error, or in
 * cpr_del_blocker.  errp is set in a later patch.
 */
int cpr_add_blocker(Error **reasonp, Error **errp, CprMode mode, ...)
{
    int modes = 0;
    va_list ap;
    ERRP_GUARD();

    va_start(ap, mode);
    while (mode != CPR_MODE_NONE && mode != CPR_MODE_ALL) {
        assert(mode > CPR_MODE_NONE && mode < CPR_MODE__MAX);
        modes |= BIT(mode);
        mode = va_arg(ap, CprMode);
    }
    va_end(ap);
    if (mode == CPR_MODE_ALL) {
        modes = BIT(CPR_MODE__MAX) - 1;
    }

    for (mode = 0; mode < CPR_MODE__MAX; mode++) {
        if (modes & BIT(mode)) {
            cpr_blockers[mode] = g_slist_prepend(cpr_blockers[mode], *reasonp);
        }
    }
    return 0;
}

/*
 * Delete the blocker from all modes it is associated with.
 */
void cpr_del_blocker(Error **reasonp)
{
    CprMode mode;

    if (*reasonp) {
        for (mode = 0; mode < CPR_MODE__MAX; mode++) {
            cpr_blockers[mode] = g_slist_remove(cpr_blockers[mode], *reasonp);
        }
        error_free(*reasonp);
        *reasonp = NULL;
    }
}

/*
 * Add a blocker which will not be deleted.  Simpler for some callers.
 */
int cpr_add_blocker_str(const char *msg, Error **errp, CprMode mode, ...)
{
    int ret;
    va_list ap;
    Error *reason = NULL;

    error_setg(&reason, "%s", msg);
    va_start(ap, mode);
    ret = cpr_add_blocker(&reason, errp, mode, ap);
    va_end(ap);
    return ret;
}

static bool cpr_is_blocked(Error **errp, CprMode mode)
{
    if (cpr_blockers[mode]) {
        error_propagate(errp, error_copy(cpr_blockers[mode]->data));
        return true;
    }

    return false;
}

void qmp_cpr_save(const char *filename, CprMode mode, Error **errp)
{
    int ret;
    QEMUFile *f;
    int saved_vm_running = runstate_is_running();

    if (!(cpr_enabled_modes & BIT(mode))) {
        error_setg(errp, "cpr mode is not enabled.  Use -cpr-enable.");
        return;
    }

    if (cpr_is_blocked(errp, mode)) {
        return;
    }

    if (global_state_store()) {
        error_setg(errp, "Error saving global state");
        return;
    }

    f = qemu_fopen_file(filename, O_CREAT | O_WRONLY | O_TRUNC, 0600,
                        "cpr-save", errp);
    if (!f) {
        return;
    }

    if (runstate_check(RUN_STATE_SUSPENDED)) {
        /* Update timers_state before saving.  Suspend did not so do. */
        cpu_disable_ticks();
    }
    vm_stop(RUN_STATE_SAVE_VM);

    cpr_set_mode(mode);
    ret = qemu_save_device_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, "Error %d while saving VM state", ret);
        goto err;
    }

    return;

err:
    if (saved_vm_running) {
        vm_start();
    }
    cpr_set_mode(CPR_MODE_NONE);
}

void qmp_cpr_load(const char *filename, CprMode mode, Error **errp)
{
    QEMUFile *f;
    int ret;
    RunState state;

    if (!(cpr_enabled_modes & BIT(mode))) {
        error_setg(errp, "cpr mode is not enabled.  Use -cpr-enable.");
        return;
    }

    if (runstate_is_running()) {
        error_setg(errp, "cpr-load called for a running VM");
        return;
    }

    f = qemu_fopen_file(filename, O_RDONLY, 0, "cpr-load", errp);
    if (!f) {
        return;
    }

    if (qemu_get_be32(f) != QEMU_VM_FILE_MAGIC ||
        qemu_get_be32(f) != QEMU_VM_FILE_VERSION) {
        error_setg(errp, "%s is not a vmstate file", filename);
        qemu_fclose(f);
        return;
    }

    cpr_set_mode(mode);
    ret = qemu_load_device_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        error_setg(errp, "Error %d while loading VM state", ret);
        goto out;
    }

    state = global_state_get_runstate();
    if (state == RUN_STATE_RUNNING) {
        vm_start();
    } else {
        runstate_set(state);
        if (runstate_check(RUN_STATE_SUSPENDED)) {
            /* Force vm_start to be called later. */
            qemu_system_start_on_wakeup_request();
        }
    }

out:
    cpr_set_mode(CPR_MODE_NONE);
}
