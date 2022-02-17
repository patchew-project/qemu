/**
 * QEMU vfio-user-server server object
 *
 * Copyright © 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

/**
 * Usage: add options:
 *     -machine x-remote,vfio-user=on
 *     -device <PCI-device>,id=<pci-dev-id>
 *     -object x-vfio-user-server,id=<id>,type=unix,path=<socket-path>,
 *             device=<pci-dev-id>
 *
 * Note that x-vfio-user-server object must be used with x-remote machine only.
 * This server could only support PCI devices for now.
 *
 * type - SocketAddress type - presently "unix" alone is supported. Required
 *        option
 *
 * path - named unix socket, it will be created by the server. It is
 *        a required option
 *
 * device - id of a device on the server, a required option. PCI devices
 *          alone are supported presently.
 *
 * notes - x-vfio-user-server could block IO and monitor during the
 *         initialization phase.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "hw/boards.h"
#include "hw/remote/machine.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-events-misc.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "libvfio-user.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "qemu/timer.h"
#include "exec/memory.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/remote/vfio-user-obj.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/vmstate.h"
#include "migration/global_state.h"
#include "block/block.h"
#include "sysemu/block-backend.h"
#include "net/net.h"

#define TYPE_VFU_OBJECT "x-vfio-user-server"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

/**
 * VFU_OBJECT_ERROR - reports an error message. If auto_shutdown
 * is set, it aborts the machine on error. Otherwise, it logs an
 * error message without aborting.
 */
#define VFU_OBJECT_ERROR(o, fmt, ...)                         \
    {                                                         \
        VfuObjectClass *oc = VFU_OBJECT_GET_CLASS(OBJECT(o)); \
                                                              \
        if (oc->auto_shutdown) {                              \
            error_setg(&error_abort, (fmt), ## __VA_ARGS__);  \
        } else {                                              \
            error_report((fmt), ## __VA_ARGS__);              \
        }                                                     \
    }                                                         \

struct VfuObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;

    /*
     * Can be set to shutdown automatically when all server object
     * instances are destroyed
     */
    bool auto_shutdown;
};

struct VfuObject {
    /* private */
    Object parent;

    SocketAddress *socket;

    char *device;

    Error *err;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;

    Error *unplug_blocker;

    int vfu_poll_fd;

    /*
     * vfu_mig_buf holds the migration data. In the remote server, this
     * buffer replaces the role of an IO channel which links the source
     * and the destination.
     *
     * Whenever the client QEMU process initiates migration, the remote
     * server gets notified via libvfio-user callbacks. The remote server
     * sets up a QEMUFile object using this buffer as backend. The remote
     * server passes this object to its migration subsystem, which slurps
     * the VMSD of the device ('devid' above) referenced by this object
     * and stores the VMSD in this buffer.
     *
     * The client subsequetly asks the remote server for any data that
     * needs to be moved over to the destination via libvfio-user
     * library's vfu_migration_callbacks_t callbacks. The remote hands
     * over this buffer as data at this time.
     *
     * A reverse of this process happens at the destination.
     */
    uint8_t *vfu_mig_buf;

    uint64_t vfu_mig_buf_size;

    uint64_t vfu_mig_buf_pending;

    uint64_t vfu_mig_data_written;

    uint64_t vfu_mig_section_offset;

    QEMUFile *vfu_mig_file;

    vfu_migr_state_t vfu_state;
};

static GHashTable *vfu_object_bdf_to_ctx_table;

#define INT2VOIDP(i) (void *)(uintptr_t)(i)

#define KB(x)    ((size_t) (x) << 10)

#define VFU_OBJECT_MIG_WINDOW KB(64)

static void vfu_object_init_ctx(VfuObject *o, Error **errp);

static void vfu_object_set_socket(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        error_setg(errp, "vfu: Unable to set socket property - server busy");
        return;
    }

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

    visit_type_SocketAddress(v, name, &o->socket, errp);

    if (o->socket->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(errp, "vfu: Unsupported socket type - %s",
                   SocketAddressType_str(o->socket->type));
        qapi_free_SocketAddress(o->socket);
        o->socket = NULL;
        return;
    }

    trace_vfu_prop("socket", o->socket->u.q_unix.path);

    vfu_object_init_ctx(o, errp);
}

static void vfu_object_set_device(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        error_setg(errp, "vfu: Unable to set device property - server busy");
        return;
    }

    g_free(o->device);

    o->device = g_strdup(str);

    trace_vfu_prop("device", str);

    vfu_object_init_ctx(o, errp);
}

/**
 * Migration helper functions
 *
 * vfu_mig_buf_read & vfu_mig_buf_write are used by QEMU's migration
 * subsystem - qemu_remote_loadvm & qemu_remote_savevm. loadvm/savevm
 * call these functions via QEMUFileOps to load/save the VMSD of a
 * device into vfu_mig_buf
 *
 */
static ssize_t vfu_mig_buf_read(void *opaque, uint8_t *buf, int64_t pos,
                                size_t size, Error **errp)
{
    VfuObject *o = opaque;

    if (pos > o->vfu_mig_buf_size) {
        size = 0;
    } else if ((pos + size) > o->vfu_mig_buf_size) {
        size = o->vfu_mig_buf_size - pos;
    }

    memcpy(buf, (o->vfu_mig_buf + pos), size);

    return size;
}

static ssize_t vfu_mig_buf_write(void *opaque, struct iovec *iov, int iovcnt,
                                 int64_t pos, Error **errp)
{
    ERRP_GUARD();
    VfuObject *o = opaque;
    uint64_t end = pos + iov_size(iov, iovcnt);
    int i;

    if (o->vfu_mig_buf_pending) {
        error_setg(errp, "Migration is ongoing");
        return 0;
    }

    if (end > o->vfu_mig_buf_size) {
        o->vfu_mig_buf = g_realloc(o->vfu_mig_buf, end);
    }

    for (i = 0; i < iovcnt; i++) {
        memcpy((o->vfu_mig_buf + o->vfu_mig_buf_size), iov[i].iov_base,
               iov[i].iov_len);
        o->vfu_mig_buf_size += iov[i].iov_len;
    }

    return iov_size(iov, iovcnt);
}

static int vfu_mig_buf_shutdown(void *opaque, bool rd, bool wr, Error **errp)
{
    VfuObject *o = opaque;

    o->vfu_mig_buf_size = 0;

    g_free(o->vfu_mig_buf);

    o->vfu_mig_buf = NULL;

    o->vfu_mig_buf_pending = 0;

    o->vfu_mig_data_written = 0;

    o->vfu_mig_section_offset = 0;

    return 0;
}

static const QEMUFileOps vfu_mig_fops_save = {
    .writev_buffer  = vfu_mig_buf_write,
    .shut_down      = vfu_mig_buf_shutdown,
};

static const QEMUFileOps vfu_mig_fops_load = {
    .get_buffer     = vfu_mig_buf_read,
    .shut_down      = vfu_mig_buf_shutdown,
};

static BlockDriverState *vfu_object_find_bs_by_dev(DeviceState *dev)
{
    BlockBackend *blk = blk_by_dev(dev);

    if (!blk) {
        return NULL;
    }

    return blk_bs(blk);
}

static int vfu_object_bdrv_invalidate_cache_by_dev(DeviceState *dev)
{
    BlockDriverState *bs = NULL;
    Error *local_err = NULL;

    bs = vfu_object_find_bs_by_dev(dev);
    if (!bs) {
        return 0;
    }

    bdrv_invalidate_cache(bs, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    return 0;
}

static int vfu_object_bdrv_inactivate_by_dev(DeviceState *dev)
{
    BlockDriverState *bs = NULL;

    bs = vfu_object_find_bs_by_dev(dev);
    if (!bs) {
        return 0;
    }

    return bdrv_inactivate(bs);
}

static void vfu_object_start_stop_netdev(DeviceState *dev, bool start)
{
    NetClientState *nc = NULL;
    Error *local_err = NULL;
    char *netdev = NULL;

    netdev = object_property_get_str(OBJECT(dev), "netdev", &local_err);
    if (local_err) {
        /**
         * object_property_get_str() sets Error if netdev property is
         * not found, not necessarily an error in the context of
         * this function
         */
        error_free(local_err);
        return;
    }

    if (!netdev) {
        return;
    }

    nc = qemu_find_netdev(netdev);

    if (!nc) {
        return;
    }

    if (!start) {
        qemu_flush_or_purge_queued_packets(nc, true);

        if (nc->info && nc->info->cleanup) {
            nc->info->cleanup(nc);
        }
    } else if (nc->peer) {
        qemu_flush_or_purge_queued_packets(nc->peer, false);
    }
}

static int vfu_object_start_devs(DeviceState *dev, void *opaque)
{
    int ret = vfu_object_bdrv_invalidate_cache_by_dev(dev);

    if (ret) {
        return ret;
    }

    vfu_object_start_stop_netdev(dev, true);

    return ret;
}

static int vfu_object_stop_devs(DeviceState *dev, void *opaque)
{
    int ret = vfu_object_bdrv_inactivate_by_dev(dev);

    if (ret) {
        return ret;
    }

    vfu_object_start_stop_netdev(dev, false);

    return ret;
}

/**
 * handlers for vfu_migration_callbacks_t
 *
 * The libvfio-user library accesses these handlers to drive the migration
 * at the remote end, and also to transport the data stored in vfu_mig_buf
 *
 */
static void vfu_mig_state_stop_and_copy(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    int ret;

    if (!o->vfu_mig_file) {
        o->vfu_mig_file = qemu_fopen_ops(o, &vfu_mig_fops_save, false);
    }

    ret = qemu_remote_savevm(o->vfu_mig_file, DEVICE(o->pci_dev));
    if (ret) {
        qemu_file_shutdown(o->vfu_mig_file);
        o->vfu_mig_file = NULL;
        return;
    }

    qemu_fflush(o->vfu_mig_file);
}

static void vfu_mig_state_running(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    int ret;

    if (o->vfu_state != VFU_MIGR_STATE_RESUME) {
        goto run_ctx;
    }

    if (!o->vfu_mig_file) {
        o->vfu_mig_file = qemu_fopen_ops(o, &vfu_mig_fops_load, false);
    }

    ret = qemu_remote_loadvm(o->vfu_mig_file);
    if (ret) {
        VFU_OBJECT_ERROR(o, "vfu: failed to restore device state");
        return;
    }

    qemu_file_shutdown(o->vfu_mig_file);
    o->vfu_mig_file = NULL;

run_ctx:
    ret = qdev_walk_children(DEVICE(o->pci_dev), NULL, NULL,
                             vfu_object_start_devs,
                             NULL, NULL);
    if (ret) {
        VFU_OBJECT_ERROR(o, "vfu: failed to setup backends for %s",
                         o->device);
        return;
    }
}

static void vfu_mig_state_stop(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    int ret;

    ret = qdev_walk_children(DEVICE(o->pci_dev), NULL, NULL,
                             vfu_object_stop_devs,
                             NULL, NULL);
    if (ret) {
        VFU_OBJECT_ERROR(o, "vfu: failed to inactivate backends for %s",
                         o->device);
    }
}

static int vfu_mig_transition(vfu_ctx_t *vfu_ctx, vfu_migr_state_t state)
{
    VfuObject *o = vfu_get_private(vfu_ctx);

    if (o->vfu_state == state) {
        return 0;
    }

    switch (state) {
    case VFU_MIGR_STATE_RESUME:
        break;
    case VFU_MIGR_STATE_STOP_AND_COPY:
        vfu_mig_state_stop_and_copy(vfu_ctx);
        break;
    case VFU_MIGR_STATE_STOP:
        vfu_mig_state_stop(vfu_ctx);
        break;
    case VFU_MIGR_STATE_PRE_COPY:
        break;
    case VFU_MIGR_STATE_RUNNING:
        vfu_mig_state_running(vfu_ctx);
        break;
    default:
        warn_report("vfu: Unknown migration state %d", state);
    }

    o->vfu_state = state;

    return 0;
}

static uint64_t vfu_mig_get_pending_bytes(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    static bool mig_ongoing;

    if (!mig_ongoing && !o->vfu_mig_buf_pending) {
        o->vfu_mig_buf_pending = o->vfu_mig_buf_size;
        mig_ongoing = true;
    }

    if (mig_ongoing && !o->vfu_mig_buf_pending) {
        mig_ongoing = false;
    }

    return o->vfu_mig_buf_pending;
}

static int vfu_mig_prepare_data(vfu_ctx_t *vfu_ctx, uint64_t *offset,
                                uint64_t *size)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint64_t data_size = o->vfu_mig_buf_pending;

    if (data_size > VFU_OBJECT_MIG_WINDOW) {
        data_size = VFU_OBJECT_MIG_WINDOW;
    }

    o->vfu_mig_section_offset = o->vfu_mig_buf_size - o->vfu_mig_buf_pending;

    o->vfu_mig_buf_pending -= data_size;

    if (offset) {
        *offset = 0;
    }

    if (size) {
        *size = data_size;
    }

    return 0;
}

static ssize_t vfu_mig_read_data(vfu_ctx_t *vfu_ctx, void *buf,
                                 uint64_t size, uint64_t offset)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint64_t read_offset = o->vfu_mig_section_offset + offset;

    if (read_offset > o->vfu_mig_buf_size) {
        warn_report("vfu: buffer overflow - offset outside range");
        return -1;
    }

    if ((read_offset + size) > o->vfu_mig_buf_size) {
        warn_report("vfu: buffer overflow - size outside range");
        size = o->vfu_mig_buf_size - read_offset;
    }

    memcpy(buf, (o->vfu_mig_buf + read_offset), size);

    return size;
}

static ssize_t vfu_mig_write_data(vfu_ctx_t *vfu_ctx, void *data,
                                  uint64_t size, uint64_t offset)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint64_t end = o->vfu_mig_data_written + offset + size;

    if (end > o->vfu_mig_buf_size) {
        o->vfu_mig_buf = g_realloc(o->vfu_mig_buf, end);
        o->vfu_mig_buf_size = end;
    }

    memcpy((o->vfu_mig_buf + o->vfu_mig_data_written + offset), data, size);

    return size;
}

static int vfu_mig_data_written(vfu_ctx_t *vfu_ctx, uint64_t count)
{
    VfuObject *o = vfu_get_private(vfu_ctx);

    o->vfu_mig_data_written += count;

    return 0;
}

static const vfu_migration_callbacks_t vfu_mig_cbs = {
    .version = VFU_MIGR_CALLBACKS_VERS,
    .transition = &vfu_mig_transition,
    .get_pending_bytes = &vfu_mig_get_pending_bytes,
    .prepare_data = &vfu_mig_prepare_data,
    .read_data = &vfu_mig_read_data,
    .data_written = &vfu_mig_data_written,
    .write_data = &vfu_mig_write_data,
};

static void vfu_object_ctx_run(void *opaque)
{
    VfuObject *o = opaque;
    const char *id = NULL;
    int ret = -1;

    while (ret != 0) {
        ret = vfu_run_ctx(o->vfu_ctx);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOTCONN) {
                id = object_get_canonical_path_component(OBJECT(o));
                qapi_event_send_vfu_client_hangup(id, o->device);
                qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
                o->vfu_poll_fd = -1;
                object_unparent(OBJECT(o));
                break;
            } else {
                VFU_OBJECT_ERROR(o, "vfu: Failed to run device %s - %s",
                                 o->device, strerror(errno));
                break;
            }
        }
    }
}

static void vfu_object_attach_ctx(void *opaque)
{
    VfuObject *o = opaque;
    GPollFD pfds[1];
    int ret;

    qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);

    pfds[0].fd = o->vfu_poll_fd;
    pfds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;

retry_attach:
    ret = vfu_attach_ctx(o->vfu_ctx);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /**
         * vfu_object_attach_ctx can block QEMU's main loop
         * during attach - the monitor and other IO
         * could be unresponsive during this time.
         */
        (void)qemu_poll_ns(pfds, 1, 500 * (int64_t)SCALE_MS);
        goto retry_attach;
    } else if (ret < 0) {
        VFU_OBJECT_ERROR(o, "vfu: Failed to attach device %s to context - %s",
                         o->device, strerror(errno));
        return;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        VFU_OBJECT_ERROR(o, "vfu: Failed to get poll fd %s", o->device);
        return;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_ctx_run, NULL, o);
}

static ssize_t vfu_object_cfg_access(vfu_ctx_t *vfu_ctx, char * const buf,
                                     size_t count, loff_t offset,
                                     const bool is_write)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint32_t pci_access_width = sizeof(uint32_t);
    size_t bytes = count;
    uint32_t val = 0;
    char *ptr = buf;
    int len;

    while (bytes > 0) {
        len = (bytes > pci_access_width) ? pci_access_width : bytes;
        if (is_write) {
            memcpy(&val, ptr, len);
            pci_host_config_write_common(o->pci_dev, offset,
                                         pci_config_size(o->pci_dev),
                                         val, len);
            trace_vfu_cfg_write(offset, val);
        } else {
            val = pci_host_config_read_common(o->pci_dev, offset,
                                              pci_config_size(o->pci_dev), len);
            memcpy(ptr, &val, len);
            trace_vfu_cfg_read(offset, val);
        }
        offset += len;
        ptr += len;
        bytes -= len;
    }

    return count;
}

static void dma_register(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    AddressSpace *dma_as = NULL;
    MemoryRegion *subregion = NULL;
    g_autofree char *name = NULL;
    struct iovec *iov = &info->iova;

    if (!info->vaddr) {
        return;
    }

    name = g_strdup_printf("mem-%s-%"PRIx64"", o->device,
                           (uint64_t)info->vaddr);

    subregion = g_new0(MemoryRegion, 1);

    memory_region_init_ram_ptr(subregion, NULL, name,
                               iov->iov_len, info->vaddr);

    dma_as = pci_device_iommu_address_space(o->pci_dev);

    memory_region_add_subregion(dma_as->root, (hwaddr)iov->iov_base, subregion);

    trace_vfu_dma_register((uint64_t)iov->iov_base, iov->iov_len);
}

static void dma_unregister(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    AddressSpace *dma_as = NULL;
    MemoryRegion *mr = NULL;
    ram_addr_t offset;

    mr = memory_region_from_host(info->vaddr, &offset);
    if (!mr) {
        return;
    }

    dma_as = pci_device_iommu_address_space(o->pci_dev);

    memory_region_del_subregion(dma_as->root, mr);

    object_unparent((OBJECT(mr)));

    trace_vfu_dma_unregister((uint64_t)info->iova.iov_base);
}

static size_t vfu_object_bar_rw(PCIDevice *pci_dev, int pci_bar,
                                hwaddr offset, char * const buf,
                                hwaddr len, const bool is_write)
{
    uint8_t *ptr = (uint8_t *)buf;
    uint8_t *ram_ptr = NULL;
    bool release_lock = false;
    MemoryRegionSection section = { 0 };
    MemoryRegion *mr = NULL;
    int access_size;
    hwaddr size = 0;
    MemTxResult result;
    uint64_t val;

    section = memory_region_find(pci_dev->io_regions[pci_bar].memory,
                                 offset, len);

    if (!section.mr) {
        return 0;
    }

    mr = section.mr;

    if (is_write && mr->readonly) {
        warn_report("vfu: attempting to write to readonly region in "
                    "bar %d - [0x%"PRIx64" - 0x%"PRIx64"]",
                    pci_bar, offset, (offset + len));
        return 0;
    }

    if (memory_access_is_direct(mr, is_write)) {
        /**
         * Some devices expose a PCI expansion ROM, which could be buffer
         * based as compared to other regions which are primarily based on
         * MemoryRegionOps. memory_region_find() would already check
         * for buffer overflow, we don't need to repeat it here.
         */
        ram_ptr = memory_region_get_ram_ptr(mr);

        size = len;

        if (is_write) {
            memcpy(ram_ptr, buf, size);
        } else {
            memcpy(buf, ram_ptr, size);
        }

        goto exit;
    }

    while (len > 0) {
        /**
         * The read/write logic used below is similar to the ones in
         * flatview_read/write_continue()
         */
        release_lock = prepare_mmio_access(mr);

        access_size = memory_access_size(mr, len, offset);

        if (is_write) {
            val = ldn_he_p(ptr, access_size);

            result = memory_region_dispatch_write(mr, offset, val,
                                                  size_memop(access_size),
                                                  MEMTXATTRS_UNSPECIFIED);
        } else {
            result = memory_region_dispatch_read(mr, offset, &val,
                                                 size_memop(access_size),
                                                 MEMTXATTRS_UNSPECIFIED);

            stn_he_p(ptr, access_size, val);
        }

        if (release_lock) {
            qemu_mutex_unlock_iothread();
            release_lock = false;
        }

        if (result != MEMTX_OK) {
            warn_report("vfu: failed to %s 0x%"PRIx64"",
                        is_write ? "write to" : "read from",
                        (offset - size));

            goto exit;
        }

        len -= access_size;
        size += access_size;
        ptr += access_size;
        offset += access_size;
    }

exit:
    memory_region_unref(mr);

    return size;
}

/**
 * VFU_OBJECT_BAR_HANDLER - macro for defining handlers for PCI BARs.
 *
 * To create handler for BAR number 2, VFU_OBJECT_BAR_HANDLER(2) would
 * define vfu_object_bar2_handler
 */
#define VFU_OBJECT_BAR_HANDLER(BAR_NO)                                         \
    static ssize_t vfu_object_bar##BAR_NO##_handler(vfu_ctx_t *vfu_ctx,        \
                                        char * const buf, size_t count,        \
                                        loff_t offset, const bool is_write)    \
    {                                                                          \
        VfuObject *o = vfu_get_private(vfu_ctx);                               \
        PCIDevice *pci_dev = o->pci_dev;                                       \
                                                                               \
        return vfu_object_bar_rw(pci_dev, BAR_NO, offset,                      \
                                 buf, count, is_write);                        \
    }                                                                          \

VFU_OBJECT_BAR_HANDLER(0)
VFU_OBJECT_BAR_HANDLER(1)
VFU_OBJECT_BAR_HANDLER(2)
VFU_OBJECT_BAR_HANDLER(3)
VFU_OBJECT_BAR_HANDLER(4)
VFU_OBJECT_BAR_HANDLER(5)
VFU_OBJECT_BAR_HANDLER(6)

static vfu_region_access_cb_t *vfu_object_bar_handlers[PCI_NUM_REGIONS] = {
    &vfu_object_bar0_handler,
    &vfu_object_bar1_handler,
    &vfu_object_bar2_handler,
    &vfu_object_bar3_handler,
    &vfu_object_bar4_handler,
    &vfu_object_bar5_handler,
    &vfu_object_bar6_handler,
};

/**
 * vfu_object_register_bars - Identify active BAR regions of pdev and setup
 *                            callbacks to handle read/write accesses
 */
static void vfu_object_register_bars(vfu_ctx_t *vfu_ctx, PCIDevice *pdev)
{
    int flags = VFU_REGION_FLAG_RW;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        if (!pdev->io_regions[i].size) {
            continue;
        }

        if ((i == VFU_PCI_DEV_ROM_REGION_IDX) ||
            pdev->io_regions[i].memory->readonly) {
            flags &= ~VFU_REGION_FLAG_WRITE;
        }

        vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX + i,
                         (size_t)pdev->io_regions[i].size,
                         vfu_object_bar_handlers[i],
                         flags, NULL, 0, -1, 0);

        trace_vfu_bar_register(i, pdev->io_regions[i].addr,
                               pdev->io_regions[i].size);
    }
}

static void vfu_object_irq_trigger(int pci_bdf, unsigned vector)
{
    vfu_ctx_t *vfu_ctx = NULL;

    if (!vfu_object_bdf_to_ctx_table) {
        return;
    }

    vfu_ctx = g_hash_table_lookup(vfu_object_bdf_to_ctx_table,
                                  INT2VOIDP(pci_bdf));

    if (vfu_ctx) {
        vfu_irq_trigger(vfu_ctx, vector);
    }
}

static int vfu_object_map_irq(PCIDevice *pci_dev, int intx)
{
    int pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                pci_dev->devfn);

    return pci_bdf;
}

static void vfu_object_set_irq(void *opaque, int pirq, int level)
{
    if (level) {
        vfu_object_irq_trigger(pirq, 0);
    }
}

static void vfu_object_msi_notify(PCIDevice *pci_dev, unsigned vector)
{
    int pci_bdf;

    pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)), pci_dev->devfn);

    vfu_object_irq_trigger(pci_bdf, vector);
}

static int vfu_object_setup_irqs(VfuObject *o, PCIDevice *pci_dev)
{
    vfu_ctx_t *vfu_ctx = o->vfu_ctx;
    int ret, pci_bdf;

    ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_INTX_IRQ, 1);
    if (ret < 0) {
        return ret;
    }

    ret = 0;
    if (msix_nr_vectors_allocated(pci_dev)) {
        ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSIX_IRQ,
                                       msix_nr_vectors_allocated(pci_dev));

        pci_dev->msix_notify = vfu_object_msi_notify;
    } else if (msi_nr_vectors_allocated(pci_dev)) {
        ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSI_IRQ,
                                       msi_nr_vectors_allocated(pci_dev));

        pci_dev->msi_notify = vfu_object_msi_notify;
    }

    if (ret < 0) {
        return ret;
    }

    pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)), pci_dev->devfn);

    g_hash_table_insert(vfu_object_bdf_to_ctx_table, INT2VOIDP(pci_bdf),
                        o->vfu_ctx);

    return 0;
}

void vfu_object_set_bus_irq(PCIBus *pci_bus)
{
    pci_bus_irqs(pci_bus, vfu_object_set_irq, vfu_object_map_irq, NULL, 1);
}

static bool vfu_object_migratable(VfuObject *o)
{
    DeviceClass *dc = DEVICE_GET_CLASS(o->pci_dev);

    return dc->vmsd && !dc->vmsd->unmigratable;
}

/*
 * TYPE_VFU_OBJECT depends on the availability of the 'socket' and 'device'
 * properties. It also depends on devices instantiated in QEMU. These
 * dependencies are not available during the instance_init phase of this
 * object's life-cycle. As such, the server is initialized after the
 * machine is setup. machine_init_done_notifier notifies TYPE_VFU_OBJECT
 * when the machine is setup, and the dependencies are available.
 */
static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    Error *err = NULL;

    vfu_object_init_ctx(o, &err);

    if (err) {
        error_propagate(&error_abort, err);
    }
}

static void vfu_object_init_ctx(VfuObject *o, Error **errp)
{
    ERRP_GUARD();
    DeviceState *dev = NULL;
    vfu_pci_type_t pci_type = VFU_PCI_TYPE_CONVENTIONAL;
    uint64_t migr_regs_size, migr_size;
    int ret;

    if (o->vfu_ctx || !o->socket || !o->device ||
            !phase_check(PHASE_MACHINE_READY)) {
        return;
    }

    if (o->err) {
        error_propagate(errp, o->err);
        o->err = NULL;
        return;
    }

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket->u.q_unix.path,
                                LIBVFIO_USER_FLAG_ATTACH_NB,
                                o, VFU_DEV_TYPE_PCI);
    if (o->vfu_ctx == NULL) {
        error_setg(errp, "vfu: Failed to create context - %s", strerror(errno));
        return;
    }

    dev = qdev_find_recursive(sysbus_get_default(), o->device);
    if (dev == NULL) {
        error_setg(errp, "vfu: Device %s not found", o->device);
        goto fail;
    }

    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(errp, "vfu: %s not a PCI device", o->device);
        goto fail;
    }

    o->pci_dev = PCI_DEVICE(dev);

    if (pci_is_express(o->pci_dev)) {
        pci_type = VFU_PCI_TYPE_EXPRESS;
    }

    ret = vfu_pci_init(o->vfu_ctx, pci_type, PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        error_setg(errp,
                   "vfu: Failed to attach PCI device %s to context - %s",
                   o->device, strerror(errno));
        goto fail;
    }

    error_setg(&o->unplug_blocker,
               "vfu: %s for %s must be deleted before unplugging",
               TYPE_VFU_OBJECT, o->device);
    qdev_add_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);

    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_CFG_REGION_IDX,
                           pci_config_size(o->pci_dev), &vfu_object_cfg_access,
                           VFU_REGION_FLAG_RW | VFU_REGION_FLAG_ALWAYS_CB,
                           NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(errp,
                   "vfu: Failed to setup config space handlers for %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

    ret = vfu_setup_device_dma(o->vfu_ctx, &dma_register, &dma_unregister);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup DMA handlers for %s",
                   o->device);
        goto fail;
    }

    vfu_object_register_bars(o->vfu_ctx, o->pci_dev);

    ret = vfu_object_setup_irqs(o, o->pci_dev);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup interrupts for %s",
                   o->device);
        goto fail;
    }

    migr_regs_size = vfu_get_migr_register_area_size();
    migr_size = migr_regs_size + VFU_OBJECT_MIG_WINDOW;

    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_MIGR_REGION_IDX,
                           migr_size, NULL,
                           VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to register migration BAR %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

    if (!vfu_object_migratable(o)) {
        goto realize_ctx;
    }

    ret = vfu_setup_device_migration_callbacks(o->vfu_ctx, &vfu_mig_cbs,
                                               migr_regs_size);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to setup migration %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

realize_ctx:
    ret = vfu_realize_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(errp, "vfu: Failed to realize device %s- %s",
                   o->device, strerror(errno));
        goto fail;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        error_setg(errp, "vfu: Failed to get poll fd %s", o->device);
        goto fail;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_attach_ctx, NULL, o);

    return;

fail:
    vfu_destroy_ctx(o->vfu_ctx);
    if (o->unplug_blocker && o->pci_dev) {
        qdev_del_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);
        error_free(o->unplug_blocker);
        o->unplug_blocker = NULL;
    }
    o->vfu_ctx = NULL;
    o->pci_dev = NULL;
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs++;

    if (!phase_check(PHASE_MACHINE_READY)) {
        o->machine_done.notify = vfu_object_machine_done;
        qemu_add_machine_init_done_notifier(&o->machine_done);
    }

    if (!object_dynamic_cast(OBJECT(current_machine), TYPE_REMOTE_MACHINE)) {
        error_setg(&o->err, "vfu: %s only compatible with %s machine",
                   TYPE_VFU_OBJECT, TYPE_REMOTE_MACHINE);
        return;
    }

    o->vfu_poll_fd = -1;

    o->vfu_state = VFU_MIGR_STATE_STOP;
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);
    int pci_bdf;

    k->nr_devs--;

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

    if (o->vfu_poll_fd != -1) {
        qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
        o->vfu_poll_fd = -1;
    }

    if (o->vfu_ctx) {
        vfu_destroy_ctx(o->vfu_ctx);
    }

    g_free(o->device);

    o->device = NULL;

    if (o->unplug_blocker && o->pci_dev) {
        qdev_del_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker);
        error_free(o->unplug_blocker);
        o->unplug_blocker = NULL;
    }

    if (o->pci_dev) {
        pci_bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(o->pci_dev)),
                                o->pci_dev->devfn);
        g_hash_table_remove(vfu_object_bdf_to_ctx_table, INT2VOIDP(pci_bdf));
    }

    o->pci_dev = NULL;

    if (!k->nr_devs && k->auto_shutdown) {
        g_hash_table_destroy(vfu_object_bdf_to_ctx_table);
        vfu_object_bdf_to_ctx_table = NULL;
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }

    if (o->machine_done.notify) {
        qemu_remove_machine_init_done_notifier(&o->machine_done);
        o->machine_done.notify = NULL;
    }
}

static void vfu_object_class_init(ObjectClass *klass, void *data)
{
    VfuObjectClass *k = VFU_OBJECT_CLASS(klass);

    k->nr_devs = 0;

    k->auto_shutdown = true;

    msi_nonbroken = true;

    vfu_object_bdf_to_ctx_table = g_hash_table_new_full(NULL, NULL, NULL, NULL);

    object_class_property_add(klass, "socket", "SocketAddress", NULL,
                              vfu_object_set_socket, NULL, NULL);
    object_class_property_set_description(klass, "socket",
                                          "SocketAddress "
                                          "(ex: type=unix,path=/tmp/sock). "
                                          "Only UNIX is presently supported");
    object_class_property_add_str(klass, "device", NULL,
                                  vfu_object_set_device);
    object_class_property_set_description(klass, "device",
                                          "device ID - only PCI devices "
                                          "are presently supported");
}

static const TypeInfo vfu_object_info = {
    .name = TYPE_VFU_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VfuObject),
    .instance_init = vfu_object_init,
    .instance_finalize = vfu_object_finalize,
    .class_size = sizeof(VfuObjectClass),
    .class_init = vfu_object_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void vfu_register_types(void)
{
    type_register_static(&vfu_object_info);
}

type_init(vfu_register_types);
