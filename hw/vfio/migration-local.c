/*
 * QEMU VFIO Migration Support
 *
 * Copyright NVIDIA, Inc. 2020
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * See the COPYING file in the top-level directory.
 *
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
#include "ui/console.h"

static inline int vfio_mig_access(VFIODevice *vbasedev, void *val, int count,
                                  off_t off, bool iswrite)
{
    int ret;

    ret = iswrite ? pwrite(vbasedev->fd, val, count, off) :
                    pread(vbasedev->fd, val, count, off);
    if (ret < count) {
        error_report("vfio_mig_%s %d byte %s: failed at offset 0x%"
                     HWADDR_PRIx", err: %s", iswrite ? "write" : "read", count,
                     vbasedev->name, off, strerror(errno));
        return (ret < 0) ? ret : -EINVAL;
    }
    return 0;
}

static int vfio_mig_rw(VFIODevice *vbasedev, __u8 *buf, size_t count,
                       off_t off, bool iswrite)
{
    int ret, done = 0;
    __u8 *tbuf = buf;

    while (count) {
        int bytes = 0;

        if (count >= 8 && !(off % 8)) {
            bytes = 8;
        } else if (count >= 4 && !(off % 4)) {
            bytes = 4;
        } else if (count >= 2 && !(off % 2)) {
            bytes = 2;
        } else {
            bytes = 1;
        }

        ret = vfio_mig_access(vbasedev, tbuf, bytes, off, iswrite);
        if (ret) {
            return ret;
        }

        count -= bytes;
        done += bytes;
        off += bytes;
        tbuf += bytes;
    }
    return done;
}

#define vfio_mig_read(f, v, c, o)       vfio_mig_rw(f, (__u8 *)v, c, o, false)
#define vfio_mig_write(f, v, c, o)      vfio_mig_rw(f, (__u8 *)v, c, o, true)

#define VFIO_MIG_STRUCT_OFFSET(f)       \
                                 offsetof(struct vfio_device_migration_info, f)
/*
 * Change the device_state register for device @vbasedev. Bits set in @mask
 * are preserved, bits set in @value are set, and bits not set in either @mask
 * or @value are cleared in device_state. If the register cannot be accessed,
 * the resulting state would be invalid, or the device enters an error state,
 * an error is returned.
 */

static int vfio_migration_set_state_local(VFIODevice *vbasedev, uint32_t mask,
                                          uint32_t value)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    off_t dev_state_off = region->fd_offset +
                          VFIO_MIG_STRUCT_OFFSET(device_state);
    uint32_t device_state;
    int ret;

    ret = vfio_mig_read(vbasedev, &device_state, sizeof(device_state),
                        dev_state_off);
    if (ret < 0) {
        return ret;
    }

    device_state = (device_state & mask) | value;

    if (!VFIO_DEVICE_STATE_VALID(device_state)) {
        return -EINVAL;
    }

    ret = vfio_mig_write(vbasedev, &device_state, sizeof(device_state),
                         dev_state_off);
    if (ret < 0) {
        int rret;

        rret = vfio_mig_read(vbasedev, &device_state, sizeof(device_state),
                             dev_state_off);

        if ((rret < 0) || (VFIO_DEVICE_STATE_IS_ERROR(device_state))) {
            hw_error("%s: Device in error state 0x%x", vbasedev->name,
                     device_state);
            return rret ? rret : -EIO;
        }
        return ret;
    }

    migration->device_state = device_state;
    trace_vfio_migration_set_state(vbasedev->name, device_state);
    return 0;
}

static void *get_data_section_size(VFIORegion *region, uint64_t data_offset,
                                   uint64_t data_size, uint64_t *size)
{
    void *ptr = NULL;
    uint64_t limit = 0;
    int i;

    if (!region->mmaps) {
        if (size) {
            *size = MIN(data_size, region->size - data_offset);
        }
        return ptr;
    }

    for (i = 0; i < region->nr_mmaps; i++) {
        VFIOMmap *map = region->mmaps + i;

        if ((data_offset >= map->offset) &&
            (data_offset < map->offset + map->size)) {

            /* check if data_offset is within sparse mmap areas */
            ptr = map->mmap + data_offset - map->offset;
            if (size) {
                *size = MIN(data_size, map->offset + map->size - data_offset);
            }
            break;
        } else if ((data_offset < map->offset) &&
                   (!limit || limit > map->offset)) {
            /*
             * data_offset is not within sparse mmap areas, find size of
             * non-mapped area. Check through all list since region->mmaps list
             * is not sorted.
             */
            limit = map->offset;
        }
    }

    if (!ptr && size) {
        *size = limit ? MIN(data_size, limit - data_offset) : data_size;
    }
    return ptr;
}

static int vfio_migration_save_buffer_local(QEMUFile *f, VFIODevice *vbasedev,
                                            uint64_t *size)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint64_t data_offset = 0, data_size = 0, sz;
    int ret;

    ret = vfio_mig_read(vbasedev, &data_offset, sizeof(data_offset),
                      region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_offset));
    if (ret < 0) {
        return ret;
    }

    ret = vfio_mig_read(vbasedev, &data_size, sizeof(data_size),
                        region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_size));
    if (ret < 0) {
        return ret;
    }

    trace_vfio_save_buffer_local(vbasedev->name, data_offset, data_size,
                                 migration->pending_bytes);

    qemu_put_be64(f, data_size);
    sz = data_size;

    while (sz) {
        void *buf;
        uint64_t sec_size;
        bool buf_allocated = false;

        buf = get_data_section_size(region, data_offset, sz, &sec_size);

        if (!buf) {
            buf = g_try_malloc(sec_size);
            if (!buf) {
                error_report("%s: Error allocating buffer ", __func__);
                return -ENOMEM;
            }
            buf_allocated = true;

            ret = vfio_mig_read(vbasedev, buf, sec_size,
                                region->fd_offset + data_offset);
            if (ret < 0) {
                g_free(buf);
                return ret;
            }
        }

        qemu_put_buffer(f, buf, sec_size);

        if (buf_allocated) {
            g_free(buf);
        }
        sz -= sec_size;
        data_offset += sec_size;
    }

    ret = qemu_file_get_error(f);

    if (!ret && size) {
        *size = data_size;
    }

    return ret;
}

static int vfio_migration_load_buffer_local(QEMUFile *f, VFIODevice *vbasedev,
                                            uint64_t data_size)
{
    VFIORegion *region = &vbasedev->migration->region;
    uint64_t data_offset = 0, size, report_size;
    int ret;

    do {
        ret = vfio_mig_read(vbasedev, &data_offset, sizeof(data_offset),
                      region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_offset));
        if (ret < 0) {
            return ret;
        }

        if (data_offset + data_size > region->size) {
            /*
             * If data_size is greater than the data section of migration region
             * then iterate the write buffer operation. This case can occur if
             * size of migration region at destination is smaller than size of
             * migration region at source.
             */
            report_size = size = region->size - data_offset;
            data_size -= size;
        } else {
            report_size = size = data_size;
            data_size = 0;
        }

        trace_vfio_load_state_device_data_local(vbasedev->name, data_offset,
                                                size);

        while (size) {
            void *buf;
            uint64_t sec_size;
            bool buf_alloc = false;

            buf = get_data_section_size(region, data_offset, size, &sec_size);

            if (!buf) {
                buf = g_try_malloc(sec_size);
                if (!buf) {
                    error_report("%s: Error allocating buffer ", __func__);
                    return -ENOMEM;
                }
                buf_alloc = true;
            }

            qemu_get_buffer(f, buf, sec_size);

            if (buf_alloc) {
                ret = vfio_mig_write(vbasedev, buf, sec_size,
                        region->fd_offset + data_offset);
                g_free(buf);

                if (ret < 0) {
                    return ret;
                }
            }
            size -= sec_size;
            data_offset += sec_size;
        }

        ret = vfio_mig_write(vbasedev, &report_size, sizeof(report_size),
                        region->fd_offset + VFIO_MIG_STRUCT_OFFSET(data_size));
        if (ret < 0) {
            return ret;
        }
    } while (data_size);

    return 0;
}

static int vfio_migration_update_pending_local(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIORegion *region = &migration->region;
    uint64_t pending_bytes = 0;
    int ret;

    ret = vfio_mig_read(vbasedev, &pending_bytes, sizeof(pending_bytes),
                    region->fd_offset + VFIO_MIG_STRUCT_OFFSET(pending_bytes));
    if (ret < 0) {
        migration->pending_bytes = 0;
        return ret;
    }

    migration->pending_bytes = pending_bytes;
    trace_vfio_update_pending(vbasedev->name, pending_bytes);
    return 0;
}

static void vfio_migration_cleanup_local(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (migration->region.mmaps) {
        vfio_region_unmap(&migration->region);
    }
}

static int vfio_migration_save_setup_local(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    int ret = -1;

    if (migration->region.mmaps) {
        /*
         * Calling vfio_region_mmap() from migration thread. Memory API called
         * from this function require locking the iothread when called from
         * outside the main loop thread.
         */
        qemu_mutex_lock_iothread();
        ret = vfio_region_mmap(&migration->region);
        qemu_mutex_unlock_iothread();
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region: %s",
                         vbasedev->name, strerror(-ret));
            error_report("%s: Falling back to slow path", vbasedev->name);
        }
    }
    return ret;
}

static int vfio_migration_load_setup_local(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    int ret = -1;

    if (migration->region.mmaps) {
        ret = vfio_region_mmap(&migration->region);
        if (ret) {
            error_report("%s: Failed to mmap VFIO migration region %d: %s",
                         vbasedev->name, migration->region.nr,
                         strerror(-ret));
            error_report("%s: Falling back to slow path", vbasedev->name);
        }
    }
    return ret;
}

static void vfio_migration_exit_local(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    vfio_region_exit(&migration->region);
    vfio_region_finalize(&migration->region);
}

static VFIOMigrationOps vfio_local_method = {
    .save_setup = vfio_migration_save_setup_local,
    .load_setup = vfio_migration_load_setup_local,
    .update_pending = vfio_migration_update_pending_local,
    .save_buffer = vfio_migration_save_buffer_local,
    .load_buffer = vfio_migration_load_buffer_local,
    .set_state = vfio_migration_set_state_local,
    .cleanup = vfio_migration_cleanup_local,
    .exit = vfio_migration_exit_local,
};

int vfio_migration_probe_local(VFIODevice *vbasedev)
{
    int ret;
    Object *obj;
    struct vfio_region_info *info = NULL;
    VFIOMigration *migration = vbasedev->migration;

    obj = vbasedev->ops->vfio_get_object(vbasedev);
    if (!obj) {
        return -EINVAL;
    }

    ret = vfio_get_dev_region_info(vbasedev,
                                   VFIO_REGION_TYPE_MIGRATION_DEPRECATED,
                                   VFIO_REGION_SUBTYPE_MIGRATION_DEPRECATED,
                                   &info);
    if (ret) {
        return -EINVAL;
    }

    ret = vfio_region_setup(obj, vbasedev, &vbasedev->migration->region,
                            info->index, "migration");
    if (ret) {
        error_report("%s: Failed to setup VFIO migration region %d: %s",
                     vbasedev->name, info->index, strerror(-ret));
        goto err;
    }

    if (!vbasedev->migration->region.size) {
        error_report("%s: Invalid zero-sized VFIO migration region %d",
                     vbasedev->name, info->index);
        ret = -EINVAL;
        goto err;
    }

    trace_vfio_migration_probe_local(vbasedev->name, info->index);
    migration->ops = &vfio_local_method;
    g_free(info);
    return 0;

err:
    vfio_migration_exit_local(vbasedev);
    g_free(info);
    return ret;
}
