/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "sysemu/runstate.h"
#include "hw/vfio/vfio-common.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "pci.h"
#include "trace.h"
#include "hw/hw.h"

/*
 * Flags to be used as unique delimiters for VFIO devices in the migration
 * stream. These flags are composed as:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => Magic ID, represents emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 *
 * The beginning of state information is marked by _DEV_CONFIG_STATE,
 * _DEV_SETUP_STATE, or _DEV_DATA_STATE, respectively. The end of a
 * certain state information is marked by _END_OF_STATE.
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)
#define VFIO_MIG_FLAG_DEV_DATA_STATE    (0xffffffffef100004ULL)

static int64_t bytes_transferred;

static int vfio_save_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_CONFIG_STATE);

    if (vbasedev->ops && vbasedev->ops->vfio_save_config) {
        vbasedev->ops->vfio_save_config(vbasedev, f);
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    trace_vfio_save_device_config_state(vbasedev->name);

    return qemu_file_get_error(f);
}

static int vfio_load_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    uint64_t data;

    if (vbasedev->ops && vbasedev->ops->vfio_load_config) {
        int ret;

        ret = vbasedev->ops->vfio_load_config(vbasedev, f);
        if (ret) {
            error_report("%s: Failed to load device config space",
                         vbasedev->name);
            return ret;
        }
    }

    data = qemu_get_be64(f);
    if (data != VFIO_MIG_FLAG_END_OF_STATE) {
        error_report("%s: Failed loading device config space, "
                     "end flag incorrect 0x%"PRIx64, vbasedev->name, data);
        return -EINVAL;
    }

    trace_vfio_load_device_config_state(vbasedev->name);
    return qemu_file_get_error(f);
}

static void vfio_migration_cleanup(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (migration->ops->cleanup) {
        migration->ops->cleanup(vbasedev);
    }
}

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    trace_vfio_save_setup(vbasedev->name);

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    if (migration->ops->save_setup) {
        ret = migration->ops->save_setup(vbasedev);
        if (ret) {
            error_report("%s: Failed to vfio lm save setup:%s",
                         vbasedev->name, strerror(-ret));
            return ret;
        }
    }

    if (migration->ops->set_state) {
        ret = migration->ops->set_state(vbasedev, VFIO_DEVICE_STATE_MASK,
                                        VFIO_DEVICE_STATE_V1_SAVING);
        if (ret) {
            error_report("%s: Failed to set state SAVING", vbasedev->name);
            return ret;
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return 0;
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    vfio_migration_cleanup(vbasedev);
    trace_vfio_save_cleanup(vbasedev->name);
}

static void vfio_save_pending(QEMUFile *f, void *opaque,
                              uint64_t threshold_size,
                              uint64_t *res_precopy_only,
                              uint64_t *res_compatible,
                              uint64_t *res_postcopy_only)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    if (migration->ops->update_pending) {
        ret = migration->ops->update_pending(vbasedev);
        if (ret) {
            return;
        }
    }

    *res_precopy_only += migration->pending_bytes;

    trace_vfio_save_pending(vbasedev->name, *res_precopy_only,
                            *res_postcopy_only, *res_compatible);
}

static int vfio_save_iterate(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t data_size;
    int ret;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);

    if (migration->pending_bytes == 0) {
        if (migration->ops->update_pending) {
            ret = migration->ops->update_pending(vbasedev);
            if (ret) {
                return ret;
            }
        }

        if (migration->pending_bytes == 0) {
            qemu_put_be64(f, 0);
            qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);
            /* indicates data finished, goto complete phase */
            return 1;
        }
    }

    if (migration->ops->save_buffer) {
        ret = migration->ops->save_buffer(f, vbasedev, &data_size);
        if (ret) {
            error_report("%s: vfio_miragion_save_buffer_local failed %s",
                         vbasedev->name, strerror(errno));
            return ret;
        }
        bytes_transferred += data_size;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    /*
     * Reset pending_bytes as .save_live_pending is not called during savevm or
     * snapshot case, in such case vfio_migration_update_pending_local() at the
     * start of this function updates pending_bytes.
     */
    migration->pending_bytes = 0;
    trace_vfio_save_iterate(vbasedev->name, data_size);
    return 0;
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint64_t data_size;
    int ret;

    if (migration->ops->set_state) {
        ret = migration->ops->set_state(vbasedev,
                                        ~VFIO_DEVICE_STATE_V1_RUNNING,
                                        VFIO_DEVICE_STATE_V1_SAVING);
        if (ret) {
            error_report("%s: Failed to set state STOP and SAVING",
                         vbasedev->name);
            return ret;
        }
    }

    if (migration->ops->update_pending) {
        ret = migration->ops->update_pending(vbasedev);
        if (ret) {
            return ret;
        }
    }

    while (migration->pending_bytes > 0) {
        qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);
        if (migration->ops->save_buffer) {
            ret = migration->ops->save_buffer(f, vbasedev, &data_size);
            if (ret < 0) {
                error_report("%s: Failed to save buffer", vbasedev->name);
                return ret;
            }
            bytes_transferred += data_size;
        }

        if (data_size == 0) {
            break;
        }

        if (migration->ops->update_pending) {
            ret = migration->ops->update_pending(vbasedev);
            if (ret) {
                return ret;
            }
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    if (migration->ops->set_state) {
        ret = migration->ops->set_state(vbasedev, ~VFIO_DEVICE_STATE_V1_SAVING,
                                        0);
        if (ret) {
            error_report("%s: Failed to set state STOPPED", vbasedev->name);
            return ret;
        }
    }

    trace_vfio_save_complete_precopy(vbasedev->name);
    return ret;
}

static void vfio_save_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    ret = vfio_save_device_config_state(f, opaque);
    if (ret) {
        error_report("%s: Failed to save device config space",
                     vbasedev->name);
        qemu_file_set_error(f, ret);
    }
}

static int vfio_load_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret = 0;

    if (migration->ops->load_setup) {
        ret = migration->ops->load_setup(vbasedev);
        if (ret < 0) {
            error_report("%s: Failed to migration load setup", vbasedev->name);
            return ret;
        }
    }

    if (migration->ops->set_state) {
        ret = migration->ops->set_state(vbasedev, ~VFIO_DEVICE_STATE_MASK,
                                        VFIO_DEVICE_STATE_V1_RESUMING);
        if (ret) {
            error_report("%s: Failed to set state RESUMING", vbasedev->name);
            vfio_migration_cleanup(vbasedev);
            return ret;
        }
    }
    return ret;
}

static int vfio_load_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    vfio_migration_cleanup(vbasedev);
    trace_vfio_load_cleanup(vbasedev->name);
    return 0;
}

static int vfio_load_state(QEMUFile *f, void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    int ret = 0;
    uint64_t data;

    data = qemu_get_be64(f);
    while (data != VFIO_MIG_FLAG_END_OF_STATE) {

        trace_vfio_load_state(vbasedev->name, data);

        switch (data) {
        case VFIO_MIG_FLAG_DEV_CONFIG_STATE:
        {
            return vfio_load_device_config_state(f, opaque);
        }
        case VFIO_MIG_FLAG_DEV_SETUP_STATE:
        {
            data = qemu_get_be64(f);
            if (data == VFIO_MIG_FLAG_END_OF_STATE) {
                return ret;
            } else {
                error_report("%s: SETUP STATE: EOS not found 0x%"PRIx64,
                             vbasedev->name, data);
                return -EINVAL;
            }
            break;
        }
        case VFIO_MIG_FLAG_DEV_DATA_STATE:
        {
            uint64_t data_size = qemu_get_be64(f);
            VFIOMigration *migration = vbasedev->migration;

            if (data_size) {
                if (migration->ops->load_buffer) {
                    ret = migration->ops->load_buffer(f, vbasedev, data_size);
                    if (ret < 0) {
                        return ret;
                    }
                }
            }
            break;
        }
        default:
            error_report("%s: Unknown tag 0x%"PRIx64, vbasedev->name, data);
            return -EINVAL;
        }

        data = qemu_get_be64(f);
        ret = qemu_file_get_error(f);
        if (ret) {
            return ret;
        }
    }
    return ret;
}

static SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
    .save_live_pending = vfio_save_pending,
    .save_live_iterate = vfio_save_iterate,
    .save_live_complete_precopy = vfio_save_complete_precopy,
    .save_state = vfio_save_state,
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .load_state = vfio_load_state,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change(void *opaque, bool running, RunState state)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint32_t value, mask;
    int ret;

    if (migration->vm_running == running) {
        return;
    }

    if (running) {
        /*
         * Here device state can have one of _SAVING, _RESUMING or _STOP bit.
         * Transition from _SAVING to _RUNNING can happen if there is migration
         * failure, in that case clear _SAVING bit.
         * Transition from _RESUMING to _RUNNING occurs during resuming
         * phase, in that case clear _RESUMING bit.
         * In both the above cases, set _RUNNING bit.
         */
        mask = ~VFIO_DEVICE_STATE_MASK;
        value = VFIO_DEVICE_STATE_V1_RUNNING;
    } else {
        /*
         * Here device state could be either _RUNNING or _SAVING|_RUNNING. Reset
         * _RUNNING bit
         */
        mask = ~VFIO_DEVICE_STATE_V1_RUNNING;

        /*
         * When VM state transition to stop for savevm command, device should
         * start saving data.
         */
        if (state == RUN_STATE_SAVE_VM) {
            value = VFIO_DEVICE_STATE_V1_SAVING;
        } else {
            value = 0;
        }
    }

    if (migration->ops->set_state) {
        ret = migration->ops->set_state(vbasedev, mask, value);
        if (ret) {
            /*
             * Migration should be aborted in this case, but vm_state_notify()
             * currently does not support reporting failures.
             */
            error_report("%s: Failed to set device state 0x%x", vbasedev->name,
                         (migration->device_state & mask) | value);
            qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
        }
    }
    migration->vm_running = running;
    trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
            (migration->device_state & mask) | value);
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIOMigration *migration = container_of(notifier, VFIOMigration,
                                            migration_state);
    VFIODevice *vbasedev = migration->vbasedev;
    int ret;

    trace_vfio_migration_state_notifier(vbasedev->name,
                                        MigrationStatus_str(s->state));

    switch (s->state) {
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        bytes_transferred = 0;
        if (migration->ops->set_state) {
            ret = migration->ops->set_state(vbasedev,
                                            ~(VFIO_DEVICE_STATE_V1_SAVING |
                                            VFIO_DEVICE_STATE_V1_RESUMING),
                                            VFIO_DEVICE_STATE_V1_RUNNING);
            if (ret) {
                error_report("%s: Failed to set state RUNNING", vbasedev->name);
            }
        }
    }
}

static void vfio_migration_exit(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (migration->ops->exit) {
        migration->ops->exit(vbasedev);
    }
    g_free(vbasedev->migration);
    vbasedev->migration = NULL;
}

static int vfio_migration_check(VFIODevice *vbasedev)
{
    VFIOContainer *container = vbasedev->group->container;

    if (!vbasedev->enable_migration || !container->dirty_pages_supported) {
        return -EINVAL;
    }

    if (!vbasedev->ops->vfio_get_object) {
        return -EINVAL;
    }

    return 0;
}

static int vfio_migration_register_handlers(VFIODevice *vbasedev)
{
    Object *obj;
    char id[256] = "";
    g_autofree char *path = NULL, *oid = NULL;
    VFIOMigration *migration = vbasedev->migration;

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return -EINVAL;
    }

    oid = vmstate_if_get_id(VMSTATE_IF(DEVICE(obj)));
    if (oid) {
        path = g_strdup_printf("%s/vfio", oid);
    } else {
        path = g_strdup("vfio");
    }
    strpadcpy(id, sizeof(id), path, '\0');

    register_savevm_live(id, VMSTATE_INSTANCE_ID_ANY, 1, &savevm_vfio_handlers,
                         vbasedev);

    migration->vm_state = qdev_add_vm_change_state_handler(vbasedev->dev,
                                                           vfio_vmstate_change,
                                                           vbasedev);
    migration->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&migration->migration_state);

    return 0;
}

/* ---------------------------------------------------------------------- */

int64_t vfio_mig_bytes_transferred(void)
{
    return bytes_transferred;
}

int vfio_migration_probe(VFIODevice *vbasedev, Error **errp)
{
    int ret = -ENOTSUP;

    ret = vfio_migration_check(vbasedev);
    if (ret) {
        goto add_blocker;
    }

    vbasedev->migration = g_new0(VFIOMigration, 1);
    vbasedev->migration->vbasedev = vbasedev;

    ret = vfio_migration_probe_local(vbasedev);
    if (ret) {
        goto add_blocker;
    }

    ret = vfio_migration_register_handlers(vbasedev);
    if (ret) {
        goto add_blocker;
    }

    return 0;

add_blocker:
    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");

    ret = migrate_add_blocker(vbasedev->migration_blocker, errp);
    if (ret < 0) {
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
    g_free(vbasedev->migration);
    vbasedev->migration = NULL;
    return ret;
}

void vfio_migration_finalize(VFIODevice *vbasedev)
{
    if (vbasedev->migration) {
        VFIOMigration *migration = vbasedev->migration;

        remove_migration_state_change_notifier(&migration->migration_state);
        qemu_del_vm_change_state_handler(migration->vm_state);
        unregister_savevm(VMSTATE_IF(vbasedev->dev), "vfio", vbasedev);
        vfio_migration_exit(vbasedev);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
}
