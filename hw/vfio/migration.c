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

    /* Migration support added for PCI device only */
    if (vbasedev->type == VFIO_DEVICE_TYPE_PCI) {
        obj = vfio_pci_get_object(vbasedev);
    }

    if (!obj) {
        return ret;
    }

    ret = vfio_region_setup(obj, vbasedev, &migration->region.buffer,
                            migration->region.index, "migration");
    if (ret) {
        error_report("Failed to setup VFIO migration region %d: %s",
                      migration->region.index, strerror(-ret));
        goto err;
    }

    if (!migration->region.buffer.size) {
        ret = -EINVAL;
        error_report("Invalid region size of VFIO migration region %d: %s",
                     migration->region.index, strerror(-ret));
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
    int ret = 0;

    ret = pwrite(vbasedev->fd, &state, sizeof(state),
                 region->fd_offset + offsetof(struct vfio_device_migration_info,
                                              device_state));
    if (ret < 0) {
        error_report("Failed to set migration state %d %s",
                     ret, strerror(errno));
        return ret;
    }

    vbasedev->device_state = state;
    return 0;
}

static void vfio_vmstate_change(void *opaque, int running, RunState state)
{
    VFIODevice *vbasedev = opaque;

    if ((vbasedev->vm_running != running) && running) {
        int ret;

        ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RUNNING);
        if (ret) {
            error_report("Failed to set state RUNNING");
        }
    }

    vbasedev->vm_running = running;
}

static void vfio_migration_state_notifier(Notifier *notifier, void *data)
{
    MigrationState *s = data;
    VFIODevice *vbasedev = container_of(notifier, VFIODevice, migration_state);
    int ret;

    switch (s->state) {
    case MIGRATION_STATUS_ACTIVE:
        if (vbasedev->device_state & VFIO_DEVICE_STATE_RUNNING) {
            if (vbasedev->vm_running) {
                ret = vfio_migration_set_state(vbasedev,
                          VFIO_DEVICE_STATE_RUNNING | VFIO_DEVICE_STATE_SAVING);
                if (ret) {
                    error_report("Failed to set state RUNNING and SAVING");
                }
            } else {
                ret = vfio_migration_set_state(vbasedev,
                                               VFIO_DEVICE_STATE_SAVING);
                if (ret) {
                    error_report("Failed to set state STOP and SAVING");
                }
            }
        } else {
            ret = vfio_migration_set_state(vbasedev,
                                           VFIO_DEVICE_STATE_RESUMING);
            if (ret) {
                error_report("Failed to set state RESUMING");
            }
        }
        return;

    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
    case MIGRATION_STATUS_FAILED:
        ret = vfio_migration_set_state(vbasedev, VFIO_DEVICE_STATE_RUNNING);
        if (ret) {
            error_report("Failed to set state RUNNING");
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
        error_report("Failed to initialise migration region");
        return ret;
    }

    qemu_mutex_init(&vbasedev->migration->lock);

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
    int ret;

    ret = vfio_get_dev_region_info(vbasedev, VFIO_REGION_TYPE_MIGRATION,
                                   VFIO_REGION_SUBTYPE_MIGRATION, &info);
    if (ret) {
        Error *local_err = NULL;

        error_setg(&vbasedev->migration_blocker,
                   "VFIO device doesn't support migration");
        ret = migrate_add_blocker(vbasedev->migration_blocker, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_free(vbasedev->migration_blocker);
            return ret;
        }
    } else {
        return vfio_migration_init(vbasedev, info);
    }

    return 0;
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
