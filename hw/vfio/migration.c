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
#include "cpu.h"
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

    if (migration->region.size) {
        vfio_region_exit(&migration->region);
        vfio_region_finalize(&migration->region);
    }
}

static int vfio_migration_region_init(VFIODevice *vbasedev, int index)
{
    VFIOMigration *migration = vbasedev->migration;
    Object *obj = NULL;
    int ret = -EINVAL;

    if (!vbasedev->ops->vfio_get_object) {
        return ret;
    }

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return ret;
    }

    ret = vfio_region_setup(obj, vbasedev, &migration->region, index,
                            "migration");
    if (ret) {
        error_report("%s: Failed to setup VFIO migration region %d: %s",
                     vbasedev->name, index, strerror(-ret));
        goto err;
    }

    if (!migration->region.size) {
        ret = -EINVAL;
        error_report("%s: Invalid region size of VFIO migration region %d: %s",
                     vbasedev->name, index, strerror(-ret));
        goto err;
    }

    return 0;

err:
    vfio_migration_region_exit(vbasedev);
    return ret;
}

static int vfio_migration_set_state(VFIODevice *vbasedev, uint32_t mask,
                                    uint32_t value)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint32_t device_state;
    int ret;

    ret = pread(vbasedev->fd, &device_state, sizeof(device_state),
                region->fd_offset + offsetof(struct vfio_device_migration_info,
                                              device_state));
    if (ret < 0) {
        error_report("%s: Failed to read device state %d %s",
                     vbasedev->name, ret, strerror(errno));
        return ret;
    }

    device_state = (device_state & mask) | value;

    if (!VFIO_DEVICE_STATE_VALID(device_state)) {
        return -EINVAL;
    }

    ret = pwrite(vbasedev->fd, &device_state, sizeof(device_state),
                 region->fd_offset + offsetof(struct vfio_device_migration_info,
                                              device_state));
    if (ret < 0) {
        error_report("%s: Failed to set device state %d %s",
                     vbasedev->name, ret, strerror(errno));

        ret = pread(vbasedev->fd, &device_state, sizeof(device_state),
                region->fd_offset + offsetof(struct vfio_device_migration_info,
                device_state));
        if (ret < 0) {
            error_report("%s: On failure, failed to read device state %d %s",
                    vbasedev->name, ret, strerror(errno));
            return ret;
        }

        if (VFIO_DEVICE_STATE_IS_ERROR(device_state)) {
            error_report("%s: Device is in error state 0x%x",
                         vbasedev->name, device_state);
            return -EFAULT;
        }
    }

    vbasedev->device_state = device_state;
    trace_vfio_migration_set_state(vbasedev->name, device_state);
    return 0;
}

static void *get_data_section_size(VFIORegion *region, uint64_t data_offset,
                                   uint64_t data_size, uint64_t *size)
{
    void *ptr = NULL;
    int i;

    if (!region->mmaps) {
        *size = data_size;
        return ptr;
    }

    /* check if data_offset in within sparse mmap areas */
    for (i = 0; i < region->nr_mmaps; i++) {
        VFIOMmap *map = region->mmaps + i;

        if ((data_offset >= map->offset) &&
            (data_offset < map->offset + map->size)) {
            ptr = map->mmap + data_offset - map->offset;

            if (data_offset + data_size <= map->offset + map->size) {
                *size = data_size;
            } else {
                *size = map->offset + map->size - data_offset;
            }
            break;
        }
    }

    if (!ptr) {
        uint64_t limit = 0;

        /*
         * data_offset is not within sparse mmap areas, find size of non-mapped
         * area. Check through all list since region->mmaps list is not sorted.
         */
        for (i = 0; i < region->nr_mmaps; i++) {
            VFIOMmap *map = region->mmaps + i;

            if ((data_offset < map->offset) &&
                (!limit || limit > map->offset)) {
                limit = map->offset;
            }
        }

        *size = limit ? limit - data_offset : data_size;
    }
    return ptr;
}

static int vfio_save_buffer(QEMUFile *f, VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint64_t data_offset = 0, data_size = 0, size;
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

    trace_vfio_save_buffer(vbasedev->name, data_offset, data_size,
                           migration->pending_bytes);

    qemu_put_be64(f, data_size);
    size = data_size;

    while (size) {
        void *buf = NULL;
        bool buffer_mmaped;
        uint64_t sec_size;

        buf = get_data_section_size(region, data_offset, size, &sec_size);

        buffer_mmaped = (buf != NULL);

        if (!buffer_mmaped) {
            buf = g_try_malloc(sec_size);
            if (!buf) {
                error_report("%s: Error allocating buffer ", __func__);
                return -ENOMEM;
            }

            ret = pread(vbasedev->fd, buf, sec_size,
                        region->fd_offset + data_offset);
            if (ret != sec_size) {
                error_report("%s: Failed to get migration data %d",
                             vbasedev->name, ret);
                g_free(buf);
                return -EINVAL;
            }
        }

        qemu_put_buffer(f, buf, sec_size);

        if (!buffer_mmaped) {
            g_free(buf);
        }
        size -= sec_size;
        data_offset += sec_size;
    }

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return data_size;
}

static int vfio_update_pending(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
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

static int vfio_start_dirty_page_tracking(VFIODevice *vbasedev, bool start)
{
    int ret;
    VFIOContainer *container = vbasedev->group->container;
    struct vfio_iommu_type1_dirty_bitmap dirty = {
        .argsz = sizeof(dirty),
    };

    if (start) {
        if (vbasedev->device_state & VFIO_DEVICE_STATE_SAVING) {
            dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_START;
        } else {
            return -EINVAL;
        }
    } else {
            dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, &dirty);
    if (ret) {
        error_report("Failed to set dirty tracking flag 0x%x errno: %d",
                     dirty.flags, errno);
    }
    return ret;
}

/* ---------------------------------------------------------------------- */

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    trace_vfio_save_setup(vbasedev->name);

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    if (migration->region.mmaps) {
        qemu_mutex_lock_iothread();
        ret = vfio_region_mmap(&migration->region);
        qemu_mutex_unlock_iothread();
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region %d: %s",
                         vbasedev->name, migration->region.nr,
                         strerror(-ret));
            return ret;
        }
    }

    ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_MASK,
                                   VFIO_DEVICE_STATE_SAVING);
    if (ret) {
        error_report("%s: Failed to set state SAVING", vbasedev->name);
        return ret;
    }

    ret = vfio_start_dirty_page_tracking(vbasedev, true);
    if (ret) {
        return ret;
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
    VFIOMigration *migration = vbasedev->migration;

    vfio_start_dirty_page_tracking(vbasedev, false);

    if (migration->region.mmaps) {
        vfio_region_unmap(&migration->region);
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

    if (migration->pending_bytes == 0) {
        ret = vfio_update_pending(vbasedev);
        if (ret) {
            return ret;
        }

        if (migration->pending_bytes == 0) {
            /* indicates data finished, goto complete phase */
            return 1;
        }
    }

    data_size = vfio_save_buffer(f, vbasedev);

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

    return 0;
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_RUNNING,
                                   VFIO_DEVICE_STATE_SAVING);
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

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_SAVING, 0);
    if (ret) {
        error_report("%s: Failed to set state STOPPED", vbasedev->name);
        return ret;
    }

    trace_vfio_save_complete_precopy(vbasedev->name);
    return ret;
}

static int vfio_load_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret = 0;

    if (migration->region.mmaps) {
        ret = vfio_region_mmap(&migration->region);
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region %d: %s",
                         vbasedev->name, migration->region.nr,
                         strerror(-ret));
            return ret;
        }
    }

    ret = vfio_migration_set_state(vbasedev, ~VFIO_DEVICE_STATE_MASK,
                                   VFIO_DEVICE_STATE_RESUMING);
    if (ret) {
        error_report("%s: Failed to set state RESUMING", vbasedev->name);
    }
    return ret;
}

static int vfio_load_cleanup(void *opaque)
{
    vfio_save_cleanup(opaque);
    return 0;
}

static int vfio_load_state(QEMUFile *f, void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    int ret = 0;
    uint64_t data, data_size;

    data = qemu_get_be64(f);
    while (data != VFIO_MIG_FLAG_END_OF_STATE) {

        trace_vfio_load_state(vbasedev->name, data);

        switch (data) {
        case VFIO_MIG_FLAG_DEV_CONFIG_STATE:
        {
            ret = vfio_load_device_config_state(f, opaque);
            if (ret) {
                return ret;
            }
            break;
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
            VFIORegion *region = &migration->region;
            uint64_t data_offset = 0, size;

            data_size = size = qemu_get_be64(f);
            if (data_size == 0) {
                break;
            }

            ret = pread(vbasedev->fd, &data_offset, sizeof(data_offset),
                        region->fd_offset +
                        offsetof(struct vfio_device_migration_info,
                        data_offset));
            if (ret != sizeof(data_offset)) {
                error_report("%s:Failed to get migration buffer data offset %d",
                             vbasedev->name, ret);
                return -EINVAL;
            }

            trace_vfio_load_state_device_data(vbasedev->name, data_offset,
                                              data_size);

            while (size) {
                void *buf = NULL;
                uint64_t sec_size;
                bool buffer_mmaped;

                buf = get_data_section_size(region, data_offset, size,
                                            &sec_size);

                buffer_mmaped = (buf != NULL);

                if (!buffer_mmaped) {
                    buf = g_try_malloc(sec_size);
                    if (!buf) {
                        error_report("%s: Error allocating buffer ", __func__);
                        return -ENOMEM;
                    }
                }

                qemu_get_buffer(f, buf, sec_size);

                if (!buffer_mmaped) {
                    ret = pwrite(vbasedev->fd, buf, sec_size,
                                 region->fd_offset + data_offset);
                    g_free(buf);

                    if (ret != sec_size) {
                        error_report("%s: Failed to set migration buffer %d",
                                vbasedev->name, ret);
                        return -EINVAL;
                    }
                }
                size -= sec_size;
                data_offset += sec_size;
            }

            ret = pwrite(vbasedev->fd, &data_size, sizeof(data_size),
                         region->fd_offset +
                       offsetof(struct vfio_device_migration_info, data_size));
            if (ret != sizeof(data_size)) {
                error_report("%s: Failed to set migration buffer data size %d",
                             vbasedev->name, ret);
                return -EINVAL;
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
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .load_state = vfio_load_state,
};

/* ---------------------------------------------------------------------- */

static void vfio_vmstate_change(void *opaque, int running, RunState state)
{
    VFIODevice *vbasedev = opaque;

    if ((vbasedev->vm_running != running)) {
        int ret;
        uint32_t value = 0, mask = 0;

        if (running) {
            value = VFIO_DEVICE_STATE_RUNNING;
            if (vbasedev->device_state & VFIO_DEVICE_STATE_RESUMING) {
                mask = ~VFIO_DEVICE_STATE_RESUMING;
            }
        } else {
            mask = ~VFIO_DEVICE_STATE_RUNNING;
        }

        ret = vfio_migration_set_state(vbasedev, mask, value);
        if (ret) {
            error_report("%s: Failed to set device state 0x%x",
                         vbasedev->name, value & mask);
        }
        vbasedev->vm_running = running;
        trace_vfio_vmstate_change(vbasedev->name, running, RunState_str(state),
                                  value & mask);
    }
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIODevice *vbasedev = container_of(notifier, VFIODevice, migration_state);
    int ret;

    trace_vfio_migration_state_notifier(vbasedev->name,
                                        MigrationStatus_str(s->state));

    switch (s->state) {
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        ret = vfio_migration_set_state(vbasedev,
                      ~(VFIO_DEVICE_STATE_SAVING | VFIO_DEVICE_STATE_RESUMING),
                      VFIO_DEVICE_STATE_RUNNING);
        if (ret) {
            error_report("%s: Failed to set state RUNNING", vbasedev->name);
        }

        vfio_start_dirty_page_tracking(vbasedev, false);
    }
}

static int vfio_migration_init(VFIODevice *vbasedev,
                               struct vfio_region_info *info)
{
    int ret;
    char id[256] = "";

    vbasedev->migration = g_new0(VFIOMigration, 1);

    ret = vfio_migration_region_init(vbasedev, info->index);
    if (ret) {
        error_report("%s: Failed to initialise migration region",
                     vbasedev->name);
        g_free(vbasedev->migration);
        vbasedev->migration = NULL;
        return ret;
    }

    if (vbasedev->ops->vfio_get_object) {
        Object *obj = vbasedev->ops->vfio_get_object(vbasedev);

        if (obj) {
            DeviceState *dev = DEVICE(obj);
            char *oid = vmstate_if_get_id(VMSTATE_IF(dev));

            if (oid) {
                pstrcpy(id, sizeof(id), oid);
                pstrcat(id, sizeof(id), "/");
                g_free(oid);
            }
        }
    }
    pstrcat(id, sizeof(id), "vfio");

    register_savevm_live(id, VMSTATE_INSTANCE_ID_ANY, 1, &savevm_vfio_handlers,
                         vbasedev);
    vbasedev->vm_state = qemu_add_vm_change_state_handler(vfio_vmstate_change,
                                                          vbasedev);
    vbasedev->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&vbasedev->migration_state);
    return ret;
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
    g_free(info);
    return 0;

add_blocker:
    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");
    g_free(info);

    ret = migrate_add_blocker(vbasedev->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }
    return ret;
}

void vfio_migration_finalize(VFIODevice *vbasedev)
{

    if (vbasedev->migration_state.notify) {
        remove_migration_state_change_notifier(&vbasedev->migration_state);
    }

    if (vbasedev->vm_state) {
        qemu_del_vm_change_state_handler(vbasedev->vm_state);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
        vbasedev->migration_blocker = NULL;
    }

    vfio_migration_region_exit(vbasedev);
    g_free(vbasedev->migration);
}
