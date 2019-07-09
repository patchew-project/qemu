/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "cpu.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "migration/blocker.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "pci.h"
#include "trace.h"

/*
 * Flags used as delimiter:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)
#define VFIO_MIG_FLAG_DEV_DATA_STATE    (0xffffffffef100004ULL)

static void vfio_migration_region_exit(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (!migration) {
        return;
    }

    if (migration->region.buffer.size) {
        vfio_region_exit(&migration->region.buffer);
        vfio_region_finalize(&migration->region.buffer);
    }
}

static int vfio_migration_region_init(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    Object *obj = NULL;
    int ret = -EINVAL;

    if (!migration) {
        return ret;
    }

    if (!vbasedev->ops || !vbasedev->ops->vfio_get_object) {
        return ret;
    }

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return ret;
    }

    ret = vfio_region_setup(obj, vbasedev, &migration->region.buffer,
                            migration->region.index, "migration");
    if (ret) {
        error_report("%s: Failed to setup VFIO migration region %d: %s",
                     vbasedev->name, migration->region.index, strerror(-ret));
        goto err;
    }

    if (!migration->region.buffer.size) {
        ret = -EINVAL;
        error_report("%s: Invalid region size of VFIO migration region %d: %s",
                     vbasedev->name, migration->region.index, strerror(-ret));
        goto err;
    }

    return 0;

err:
    vfio_migration_region_exit(vbasedev);
    return ret;
}

static int vfio_migration_set_state(VFIODevice *vbasedev, uint32_t state)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region.buffer;
    uint32_t device_state;
    int ret = 0;

    device_state = (state & VFIO_DEVICE_STATE_MASK) |
                   (vbasedev->device_state & ~VFIO_DEVICE_STATE_MASK);

    if ((device_state & VFIO_DEVICE_STATE_MASK) == VFIO_DEVICE_STATE_INVALID) {
        return -EINVAL;
    }

    ret = pwrite(vbasedev->fd, &device_state, sizeof(device_state),
                 region->fd_offset + offsetof(struct vfio_device_migration_info,
                                              device_state));
    if (ret < 0) {
        error_report("%s: Failed to set device state %d %s",
                     vbasedev->name, ret, strerror(errno));
        return ret;
    }

    vbasedev->device_state = device_state;
    trace_vfio_migration_set_state(vbasedev->name, device_state);
    return 0;
}

static void *find_data_region(VFIORegion *region,
                              uint64_t data_offset,
                              uint64_t data_size)
{
    void *ptr = NULL;
    int i;

    for (i = 0; i < region->nr_mmaps; i++) {
        if ((data_offset >= region->mmaps[i].offset) &&
            (data_offset < region->mmaps[i].offset + region->mmaps[i].size) &&
            (data_size <= region->mmaps[i].size)) {
            ptr = region->mmaps[i].mmap + (data_offset -
                                           region->mmaps[i].offset);
            break;
        }
    }
    return ptr;
}

static int vfio_save_buffer(QEMUFile *f, VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region.buffer;
    uint64_t data_offset = 0, data_size = 0;
    int ret;

    ret = pread(vbasedev->fd, &data_offset, sizeof(data_offset),
                region->fd_offset + offsetof(struct vfio_device_migration_info,
                                             data_offset));
    if (ret != sizeof(data_offset)) {
        error_report("%s: Failed to get migration buffer data offset %d",
                     vbasedev->name, ret);
        return -EINVAL;
    }

    ret = pread(vbasedev->fd, &data_size, sizeof(data_size),
                region->fd_offset + offsetof(struct vfio_device_migration_info,
                                             data_size));
    if (ret != sizeof(data_size)) {
        error_report("%s: Failed to get migration buffer data size %d",
                     vbasedev->name, ret);
        return -EINVAL;
    }

    if (data_size > 0) {
        void *buf = NULL;
        bool buffer_mmaped;

        if (region->mmaps) {
            buf = find_data_region(region, data_offset, data_size);
        }

        buffer_mmaped = (buf != NULL) ? true : false;

        if (!buffer_mmaped) {
            buf = g_try_malloc0(data_size);
            if (!buf) {
                error_report("%s: Error allocating buffer ", __func__);
                return -ENOMEM;
            }

            ret = pread(vbasedev->fd, buf, data_size,
                        region->fd_offset + data_offset);
            if (ret != data_size) {
                error_report("%s: Failed to get migration data %d",
                             vbasedev->name, ret);
                g_free(buf);
                return -EINVAL;
            }
        }

        qemu_put_be64(f, data_size);
        qemu_put_buffer(f, buf, data_size);

        if (!buffer_mmaped) {
            g_free(buf);
        }
        migration->pending_bytes -= data_size;
    } else {
        qemu_put_be64(f, data_size);
    }

    trace_vfio_save_buffer(vbasedev->name, data_offset, data_size,
                           migration->pending_bytes);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return data_size;
}

static int vfio_update_pending(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region.buffer;
    uint64_t pending_bytes = 0;
    int ret;

    ret = pread(vbasedev->fd, &pending_bytes, sizeof(pending_bytes),
                region->fd_offset + offsetof(struct vfio_device_migration_info,
                                             pending_bytes));
    if ((ret < 0) || (ret != sizeof(pending_bytes))) {
        error_report("%s: Failed to get pending bytes %d",
                     vbasedev->name, ret);
        migration->pending_bytes = 0;
        return (ret < 0) ? ret : -EINVAL;
    }

    migration->pending_bytes = pending_bytes;
    trace_vfio_update_pending(vbasedev->name, pending_bytes);
    return 0;
}

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

/* ---------------------------------------------------------------------- */

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    if (migration->region.buffer.mmaps) {
        qemu_mutex_lock_iothread();
        ret = vfio_region_mmap(&migration->region.buffer);
        qemu_mutex_unlock_iothread();
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region %d: %s",
                         vbasedev->name, migration->region.index,
                         strerror(-ret));
            return ret;
        }
    }

    if (vbasedev->vm_running) {
        ret = vfio_migration_set_state(vbasedev,
                         VFIO_DEVICE_STATE_RUNNING | VFIO_DEVICE_STATE_SAVING);
        if (ret) {
            error_report("%s: Failed to set state RUNNING and SAVING",
                         vbasedev->name);
            return ret;
        }
    } else {
        ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_SAVING);
        if (ret) {
            error_report("%s: Failed to set state STOP and SAVING",
                         vbasedev->name);
            return ret;
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    trace_vfio_save_setup(vbasedev->name);
    return 0;
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    if (migration->region.buffer.mmaps) {
        vfio_region_unmap(&migration->region.buffer);
    }
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

    ret = vfio_update_pending(vbasedev);
    if (ret) {
        return;
    }

    *res_precopy_only += migration->pending_bytes;

    trace_vfio_save_pending(vbasedev->name, *res_precopy_only,
                            *res_postcopy_only, *res_compatible);
}

static int vfio_save_iterate(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret, data_size;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);

    qemu_mutex_lock(&migration->lock);
    data_size = vfio_save_buffer(f, vbasedev);
    qemu_mutex_unlock(&migration->lock);

    if (data_size < 0) {
        error_report("%s: vfio_save_buffer failed %s", vbasedev->name,
                     strerror(errno));
        return data_size;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    trace_vfio_save_iterate(vbasedev->name, data_size);
    if (data_size == 0) {
        /* indicates data finished, goto complete phase */
        return 1;
    }

    return 0;
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_SAVING);
    if (ret) {
        error_report("%s: Failed to set state STOP and SAVING",
                     vbasedev->name);
        return ret;
    }

    ret = vfio_save_device_config_state(f, opaque);
    if (ret) {
        return ret;
    }

    ret = vfio_update_pending(vbasedev);
    if (ret) {
        return ret;
    }

    while (migration->pending_bytes > 0) {
        qemu_put_be64(f, VFIO_MIG_FLAG_DEV_DATA_STATE);
        ret = vfio_save_buffer(f, vbasedev);
        if (ret < 0) {
            error_report("%s: Failed to save buffer", vbasedev->name);
            return ret;
        } else if (ret == 0) {
            break;
        }

        ret = vfio_update_pending(vbasedev);
        if (ret) {
            return ret;
        }
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_MASK);
    if (ret) {
        error_report("%s: Failed to set state STOPPED", vbasedev->name);
        return ret;
    }

    trace_vfio_save_complete_precopy(vbasedev->name);
    return ret;
}

static SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_cleanup = vfio_save_cleanup,
    .save_live_pending = vfio_save_pending,
    .save_live_iterate = vfio_save_iterate,
    .save_live_complete_precopy = vfio_save_complete_precopy,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change(void *opaque, int running, RunState state)
{
    VFIODevice *vbasedev = opaque;

    if ((vbasedev->vm_running != running)) {
        int ret;
        uint32_t dev_state;

        if (running) {
            dev_state = VFIO_DEVICE_STATE_RUNNING;
        } else {
            dev_state = (vbasedev->device_state & VFIO_DEVICE_STATE_MASK) &
                     ~VFIO_DEVICE_STATE_RUNNING;
        }

        ret = vfio_migration_set_state(vbasedev, dev_state);
        if (ret) {
            error_report("%s: Failed to set device state 0x%x",
                         vbasedev->name, dev_state);
        }
        vbasedev->vm_running = running;
        trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
                                  dev_state);
    }
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIODevice *vbasedev = container_of(notifier, VFIODevice, migration_state);
    int ret;

    trace_vfio_migration_state_notifier(vbasedev->name, s->state);

    switch (s->state) {
    case MIGRATION_STATUS_ACTIVE:
        if (vbasedev->device_state & VFIO_DEVICE_STATE_RUNNING) {
            if (vbasedev->vm_running) {
                ret = vfio_migration_set_state(vbasedev,
                          VFIO_DEVICE_STATE_RUNNING | VFIO_DEVICE_STATE_SAVING);
                if (ret) {
                    error_report("%s: Failed to set state RUNNING and SAVING",
                                  vbasedev->name);
                }
            } else {
                ret = vfio_migration_set_state(vbasedev,
                                               VFIO_DEVICE_STATE_SAVING);
                if (ret) {
                    error_report("%s: Failed to set state STOP and SAVING",
                                 vbasedev->name);
                }
            }
        } else {
            ret = vfio_migration_set_state(vbasedev,
                                           VFIO_DEVICE_STATE_RESUMING);
            if (ret) {
                error_report("%s: Failed to set state RESUMING",
                             vbasedev->name);
            }
        }
        return;

    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RUNNING);
        if (ret) {
            error_report("%s: Failed to set state RUNNING", vbasedev->name);
        }
        return;
    }
}

static int vfio_migration_init(VFIODevice *vbasedev,
                               struct vfio_region_info *info)
{
    int ret;

    vbasedev->migration = g_new0(VFIOMigration, 1);
    vbasedev->migration->region.index = info->index;

    ret = vfio_migration_region_init(vbasedev);
    if (ret) {
        error_report("%s: Failed to initialise migration region",
                     vbasedev->name);
        return ret;
    }

    qemu_mutex_init(&vbasedev->migration->lock);
    register_savevm_live(vbasedev->dev, "vfio", -1, 1, &savevm_vfio_handlers,
                         vbasedev);
    vbasedev->vm_state = qemu_add_vm_change_state_handler(vfio_vmstate_change,
                                                          vbasedev);

    vbasedev->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&vbasedev->migration_state);

    return 0;
}

/* ---------------------------------------------------------------------- */

int vfio_migration_probe(VFIODevice *vbasedev, Error **errp)
{
    struct vfio_region_info *info;
    Error *local_err = NULL;
    int ret;

    ret = vfio_get_dev_region_info(vbasedev, VFIO_REGION_TYPE_MIGRATION,
                                   VFIO_REGION_SUBTYPE_MIGRATION, &info);
    if (ret) {
        goto add_blocker;
    }

    ret = vfio_migration_init(vbasedev, info);
    if (ret) {
        goto add_blocker;
    }

    trace_vfio_migration_probe(vbasedev->name, info->index);
    return 0;

add_blocker:
    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");
    ret = migrate_add_blocker(vbasedev->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(vbasedev->migration_blocker);
    }
    return ret;
}

void vfio_migration_finalize(VFIODevice *vbasedev)
{
    if (!vbasedev->migration) {
        return;
    }

    if (vbasedev->migration_state.notify) {
        remove_migration_state_change_notifier(&vbasedev->migration_state);
    }

    if (vbasedev->vm_state) {
        qemu_del_vm_change_state_handler(vbasedev->vm_state);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
    }

    qemu_mutex_destroy(&vbasedev->migration->lock);
    vfio_migration_region_exit(vbasedev);
    g_free(vbasedev->migration);
}
