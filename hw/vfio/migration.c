/*
 * Migration support for VFIO devices
 *
 * Copyright NVIDIA, Inc. 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

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

/*
 * Flags used as delimiter:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)

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
    g_free(vbasedev->migration);
}

static int vfio_migration_region_init(VFIODevice *vbasedev)
{
    VFIOMigration *migration;
    Object *obj = NULL;
    int ret;
    struct vfio_device_migration_info migration_info = {
        .argsz = sizeof(migration_info),
        .flags = VFIO_MIGRATION_GET_REGION,
    };

    /* Migration support added for PCI device only */
    if (vbasedev->type == VFIO_DEVICE_TYPE_PCI) {
        VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

        obj = OBJECT(vdev);
    } else
        return -EINVAL;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, &migration_info);
    if (ret < 0) {
        error_report("Failed to migration region %s",
                     strerror(errno));
        return ret;
    }

    if (!migration_info.size || !migration_info.region_index) {
        error_report("Incorrect migration region params index: %d,size: 0x%llx",
                     migration_info.region_index, migration_info.size);
        return -EINVAL;
    }

    vbasedev->migration = g_new0(VFIOMigration, 1);
    migration = vbasedev->migration;

    migration->region.index = migration_info.region_index;

    ret = vfio_region_setup(obj, vbasedev,
                            &migration->region.buffer,
                            migration_info.region_index,
                            "migration");
    if (ret != 0) {
        error_report("%s: vfio_region_setup(%d): %s",
                __func__, migration_info.region_index, strerror(-ret));
        goto err;
    }

    if (migration->region.buffer.mmaps == NULL) {
        ret = -EINVAL;
        error_report("%s: Migration region (%d) not mappable : %s",
                __func__, migration_info.region_index, strerror(-ret));
        goto err;
    }

    ret = vfio_region_mmap(&migration->region.buffer);
    if (ret != 0) {
        error_report("%s: vfio_region_mmap(%d): %s", __func__,
                migration_info.region_index, strerror(-ret));
        goto err;
    }
    assert(migration->region.buffer.mmaps[0].mmap != NULL);

    return 0;

err:
    vfio_migration_region_exit(vbasedev);
    return ret;
}

static int vfio_migration_set_state(VFIODevice *vbasedev, uint32_t state)
{
    int ret = 0;
    struct vfio_device_migration_info migration_info = {
        .argsz = sizeof(migration_info),
        .flags = VFIO_MIGRATION_SET_STATE,
        .device_state = state,
    };

    if (vbasedev->device_state == state) {
        return ret;
    }

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, &migration_info);
    if (ret < 0) {
        error_report("Failed to set migration state %d %s",
                     ret, strerror(errno));
        return ret;
    }

    vbasedev->device_state = state;
    return ret;
}

void vfio_get_dirty_page_list(VFIODevice *vbasedev,
                              uint64_t start_addr,
                              uint64_t pfn_count)
{
    uint64_t count = 0;
    int ret;
    struct vfio_device_migration_info *migration_info;
    uint64_t bitmap_size;

    bitmap_size = (BITS_TO_LONGS(pfn_count) + 1) * sizeof(unsigned long);

    migration_info = g_malloc0(sizeof(*migration_info) +  bitmap_size);
    if (!migration_info) {
        error_report("Failed to allocated migration_info %s",
                     strerror(errno));
        return;
    }

    memset(migration_info, 0, sizeof(*migration_info) +  bitmap_size);
    migration_info->flags = VFIO_MIGRATION_GET_DIRTY_PFNS,
    migration_info->start_addr = start_addr;
    migration_info->pfn_count = pfn_count;
    migration_info->argsz = sizeof(*migration_info) + bitmap_size;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, migration_info);
    if (ret < 0) {
        error_report("Failed to get dirty pages bitmap %d %s",
                ret, strerror(errno));
        g_free(migration_info);
        return;
    }

    if (migration_info->pfn_count) {
        cpu_physical_memory_set_dirty_lebitmap(
                (unsigned long *)&migration_info->dirty_bitmap,
                migration_info->start_addr, migration_info->pfn_count);
        count +=  migration_info->pfn_count;
    }
    g_free(migration_info);
}

static int vfio_save_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_CONFIG_STATE);

    if (vbasedev->type == VFIO_DEVICE_TYPE_PCI) {
        VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
        PCIDevice *pdev = &vdev->pdev;
        uint32_t msi_flags, msi_addr_lo, msi_addr_hi = 0, msi_data;
        bool msi_64bit;
        int i;

        for (i = 0; i < PCI_ROM_SLOT; i++) {
            uint32_t bar;

            bar = pci_default_read_config(pdev, PCI_BASE_ADDRESS_0 + i * 4, 4);
            qemu_put_be32(f, bar);
        }

        msi_flags = pci_default_read_config(pdev,
                                            pdev->msi_cap + PCI_MSI_FLAGS, 2);
        msi_64bit = (msi_flags & PCI_MSI_FLAGS_64BIT);

        msi_addr_lo = pci_default_read_config(pdev,
                                         pdev->msi_cap + PCI_MSI_ADDRESS_LO, 4);
        qemu_put_be32(f, msi_addr_lo);

        if (msi_64bit) {
            msi_addr_hi = pci_default_read_config(pdev,
                                            pdev->msi_cap + PCI_MSI_ADDRESS_HI,
                                            4);
        }
        qemu_put_be32(f, msi_addr_hi);

        msi_data = pci_default_read_config(pdev,
                pdev->msi_cap + (msi_64bit ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32),
                2);
        qemu_put_be32(f, msi_data);
    }
    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    return qemu_file_get_error(f);
}

static int vfio_load_device_config_state(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;

    if (vbasedev->type == VFIO_DEVICE_TYPE_PCI) {
        VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
        PCIDevice *pdev = &vdev->pdev;
        uint32_t pci_cmd;
        uint32_t msi_flags, msi_addr_lo, msi_addr_hi = 0, msi_data;
        bool msi_64bit;
        int i;

        /* retore pci bar configuration */
        pci_cmd = pci_default_read_config(pdev, PCI_COMMAND, 2);
        vfio_pci_write_config(pdev, PCI_COMMAND,
                         pci_cmd & (!(PCI_COMMAND_IO | PCI_COMMAND_MEMORY)), 2);
        for (i = 0; i < PCI_ROM_SLOT; i++) {
            uint32_t bar = qemu_get_be32(f);

            vfio_pci_write_config(pdev, PCI_BASE_ADDRESS_0 + i * 4, bar, 4);
        }
        vfio_pci_write_config(pdev, PCI_COMMAND,
                              pci_cmd | PCI_COMMAND_IO | PCI_COMMAND_MEMORY, 2);

        /* restore msi configuration */
        msi_flags = pci_default_read_config(pdev,
                                            pdev->msi_cap + PCI_MSI_FLAGS,
                                            2);
        msi_64bit = (msi_flags & PCI_MSI_FLAGS_64BIT);

        vfio_pci_write_config(&vdev->pdev,
                              pdev->msi_cap + PCI_MSI_FLAGS,
                              msi_flags & (!PCI_MSI_FLAGS_ENABLE),
                              2);

        msi_addr_lo = qemu_get_be32(f);
        vfio_pci_write_config(pdev,
                              pdev->msi_cap + PCI_MSI_ADDRESS_LO,
                              msi_addr_lo,
                              4);

        msi_addr_hi = qemu_get_be32(f);
        if (msi_64bit) {
            vfio_pci_write_config(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_HI,
                                  msi_addr_hi, 4);
        }
        msi_data = qemu_get_be32(f);
        vfio_pci_write_config(pdev,
                              pdev->msi_cap + (msi_64bit ? PCI_MSI_DATA_64 :
                                                           PCI_MSI_DATA_32),
                              msi_data,
                              2);

        vfio_pci_write_config(&vdev->pdev,
                              pdev->msi_cap + PCI_MSI_FLAGS,
                              msi_flags | PCI_MSI_FLAGS_ENABLE,
                              2);
    }

    if (qemu_get_be64(f) != VFIO_MIG_FLAG_END_OF_STATE) {
        error_report("%s Wrong end of block ", __func__);
        return -EINVAL;
    }

    return qemu_file_get_error(f);
}

/* ---------------------------------------------------------------------- */

static bool vfio_is_active_iterate(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    if (vbasedev->vm_running && vbasedev->migration &&
        (vbasedev->migration->pending_precopy_only != 0))
        return true;

    if (!vbasedev->vm_running && vbasedev->migration &&
        (vbasedev->migration->pending_postcopy != 0))
        return true;

    return false;
}

static int vfio_save_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    qemu_put_be64(f, VFIO_MIG_FLAG_DEV_SETUP_STATE);

    qemu_mutex_lock_iothread();
    ret = vfio_migration_region_init(vbasedev);
    qemu_mutex_unlock_iothread();
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

static int vfio_save_buffer(QEMUFile *f, VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    uint8_t *buf = (uint8_t *)migration->region.buffer.mmaps[0].mmap;
    int ret;
    struct vfio_device_migration_info migration_info = {
        .argsz = sizeof(migration_info),
        .flags = VFIO_MIGRATION_GET_BUFFER,
    };

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, &migration_info);
    if (ret < 0) {
        error_report("Failed to get migration buffer information %s",
                     strerror(errno));
        return ret;
    }

    qemu_put_be64(f, migration_info.bytes_written);

    if (migration_info.bytes_written) {
        qemu_put_buffer(f, buf, migration_info.bytes_written);
    }

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return migration_info.bytes_written;
}

static int vfio_save_iterate(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    ret = vfio_save_buffer(f, vbasedev);
    if (ret < 0) {
        error_report("vfio_save_buffer failed %s",
                     strerror(errno));
        return ret;
    }

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    return ret;
}

static void vfio_update_pending(VFIODevice *vbasedev, uint64_t threshold_size)
{
    struct vfio_device_migration_info migration_info;
    VFIOMigration *migration = vbasedev->migration;
    int ret;

    migration_info.argsz = sizeof(migration_info);
    migration_info.flags = VFIO_MIGRATION_GET_PENDING;
    migration_info.threshold_size = threshold_size;

    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, &migration_info);
    if (ret < 0) {
        error_report("Failed to get pending bytes %s",
                     strerror(errno));
        return;
    }

    migration->pending_precopy_only = migration_info.pending_precopy_only;
    migration->pending_compatible = migration_info.pending_compatible;
    migration->pending_postcopy = migration_info.pending_postcopy_only;

    return;
}

static void vfio_save_pending(QEMUFile *f, void *opaque,
                              uint64_t threshold_size,
                              uint64_t *res_precopy_only,
                              uint64_t *res_compatible,
                              uint64_t *res_postcopy_only)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;

    vfio_update_pending(vbasedev, threshold_size);

    *res_precopy_only += migration->pending_precopy_only;
    *res_compatible += migration->pending_compatible;
    *res_postcopy_only += migration->pending_postcopy;
}

static int vfio_save_complete_precopy(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    MigrationState *ms = migrate_get_current();
    int ret;

    if (vbasedev->vm_running) {
        vbasedev->vm_running = 0;
    }

    ret = vfio_migration_set_state(vbasedev,
                                 VFIO_DEVICE_STATE_MIGRATION_STOPNCOPY_ACTIVE);
    if (ret) {
        error_report("Failed to set state STOPNCOPY_ACTIVE");
        return ret;
    }

    ret = vfio_save_device_config_state(f, opaque);
    if (ret) {
        return ret;
    }

    do {
        vfio_update_pending(vbasedev, ms->threshold_size);

        if (vfio_is_active_iterate(opaque)) {
            ret = vfio_save_buffer(f, vbasedev);
            if (ret < 0) {
                error_report("Failed to save buffer");
                break;
            } else if (ret == 0) {
                break;
            }
        }
    } while ((migration->pending_compatible + migration->pending_postcopy) > 0);

    qemu_put_be64(f, VFIO_MIG_FLAG_END_OF_STATE);

    ret = qemu_file_get_error(f);
    if (ret) {
        return ret;
    }

    ret = vfio_migration_set_state(vbasedev,
                                   VFIO_DEVICE_STATE_MIGRATION_SAVE_COMPLETED);
    if (ret) {
        error_report("Failed to set state SAVE_COMPLETED");
        return ret;
    }
    return ret;
}

static void vfio_save_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;

    vfio_migration_region_exit(vbasedev);
}

static int vfio_load_state(QEMUFile *f, void *opaque, int version_id)
{
    VFIODevice *vbasedev = opaque;
    VFIOMigration *migration = vbasedev->migration;
    uint8_t *buf = (uint8_t *)migration->region.buffer.mmaps[0].mmap;
    int ret;
    uint64_t data;

    data = qemu_get_be64(f);
    while (data != VFIO_MIG_FLAG_END_OF_STATE) {
        if (data == VFIO_MIG_FLAG_DEV_CONFIG_STATE) {
            ret = vfio_load_device_config_state(f, opaque);
            if (ret) {
                return ret;
            }
        } else if (data == VFIO_MIG_FLAG_DEV_SETUP_STATE) {
            data = qemu_get_be64(f);
            if (data == VFIO_MIG_FLAG_END_OF_STATE) {
                return 0;
            } else {
                error_report("SETUP STATE: EOS not found 0x%lx", data);
                return -EINVAL;
            }
        } else if (data != 0) {
            struct vfio_device_migration_info migration_info = {
                .argsz = sizeof(migration_info),
                .flags = VFIO_MIGRATION_SET_BUFFER,
            };

            qemu_get_buffer(f, buf, data);
            migration_info.bytes_written = data;

            ret = ioctl(vbasedev->fd,
                        VFIO_DEVICE_MIGRATION_INFO,
                        &migration_info);
            if (ret < 0) {
                error_report("Failed to set migration buffer information %s",
                              strerror(errno));
                return ret;
            }
        }

        ret = qemu_file_get_error(f);
        if (ret) {
            return ret;
        }
        data = qemu_get_be64(f);
    }

    return 0;
}

static int vfio_load_setup(QEMUFile *f, void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret;

    ret = vfio_migration_set_state(vbasedev,
                                    VFIO_DEVICE_STATE_MIGRATION_RESUME);
    if (ret) {
        error_report("Failed to set state RESUME");
    }

    ret = vfio_migration_region_init(vbasedev);
    if (ret) {
        error_report("Failed to initialise migration region");
        return ret;
    }

    return 0;
}

static int vfio_load_cleanup(void *opaque)
{
    VFIODevice *vbasedev = opaque;
    int ret = 0;

    ret = vfio_migration_set_state(vbasedev,
                                 VFIO_DEVICE_STATE_MIGRATION_RESUME_COMPLETED);
    if (ret) {
        error_report("Failed to set state RESUME_COMPLETED");
    }

    vfio_migration_region_exit(vbasedev);
    return ret;
}

static SaveVMHandlers savevm_vfio_handlers = {
    .save_setup = vfio_save_setup,
    .save_live_iterate = vfio_save_iterate,
    .save_live_complete_precopy = vfio_save_complete_precopy,
    .save_live_pending = vfio_save_pending,
    .save_cleanup = vfio_save_cleanup,
    .load_state = vfio_load_state,
    .load_setup = vfio_load_setup,
    .load_cleanup = vfio_load_cleanup,
    .is_active_iterate = vfio_is_active_iterate,
};

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
    case MIGRATION_STATUS_SETUP:
        ret = vfio_migration_set_state(vbasedev,
                                       VFIO_DEVICE_STATE_MIGRATION_SETUP);
        if (ret) {
            error_report("Failed to set state SETUP");
        }
        return;

    case MIGRATION_STATUS_ACTIVE:
        if (vbasedev->device_state == VFIO_DEVICE_STATE_MIGRATION_SETUP) {
            if (vbasedev->vm_running) {
                ret = vfio_migration_set_state(vbasedev,
                                    VFIO_DEVICE_STATE_MIGRATION_PRECOPY_ACTIVE);
                if (ret) {
                    error_report("Failed to set state PRECOPY_ACTIVE");
                }
            } else {
                ret = vfio_migration_set_state(vbasedev,
                                 VFIO_DEVICE_STATE_MIGRATION_STOPNCOPY_ACTIVE);
                if (ret) {
                    error_report("Failed to set state STOPNCOPY_ACTIVE");
                }
            }
        } else {
            ret = vfio_migration_set_state(vbasedev,
                                           VFIO_DEVICE_STATE_MIGRATION_RESUME);
            if (ret) {
                error_report("Failed to set state RESUME");
            }
        }
        return;

    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_CANCELLED:
        ret = vfio_migration_set_state(vbasedev,
                                       VFIO_DEVICE_STATE_MIGRATION_CANCELLED);
        if (ret) {
            error_report("Failed to set state CANCELLED");
        }
        return;

    case MIGRATION_STATUS_FAILED:
        ret = vfio_migration_set_state(vbasedev,
                                       VFIO_DEVICE_STATE_MIGRATION_FAILED);
        if (ret) {
            error_report("Failed to set state FAILED");
        }
        return;
    }
}

static int vfio_migration_init(VFIODevice *vbasedev)
{
    register_savevm_live(NULL, "vfio", -1, 1, &savevm_vfio_handlers, vbasedev);
    vbasedev->vm_state = qemu_add_vm_change_state_handler(vfio_vmstate_change,
                                                          vbasedev);

    vbasedev->migration_state.notify = vfio_migration_state_notifier;
    add_migration_state_change_notifier(&vbasedev->migration_state);

    return 0;
}


/* ---------------------------------------------------------------------- */

int vfio_migration_probe(VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_migration_info  probe;
    Error *local_err = NULL;
    int ret;

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_MIGRATION_PROBE;
    ret = ioctl(vbasedev->fd, VFIO_DEVICE_MIGRATION_INFO, &probe);

    if (ret == 0) {
        return vfio_migration_init(vbasedev);
    }

    error_setg(&vbasedev->migration_blocker,
               "VFIO device doesn't support migration");
    ret = migrate_add_blocker(vbasedev->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(vbasedev->migration_blocker);
        return ret;
    }

    return 0;
}

void vfio_migration_finalize(VFIODevice *vbasedev)
{
    if (vbasedev->vm_state) {
        qemu_del_vm_change_state_handler(vbasedev->vm_state);
        remove_migration_state_change_notifier(&vbasedev->migration_state);
    }

    if (vbasedev->migration_blocker) {
        migrate_del_blocker(vbasedev->migration_blocker);
        error_free(vbasedev->migration_blocker);
    }
}
