/**
 * QEMU vfio-user-server server object
 *
 * Copyright © 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

/**
 * Usage: add options:
 *     -machine x-remote
 *     -device <PCI-device>,id=<pci-dev-id>
 *     -object vfio-user-server,id=<id>,type=unix,path=<socket-path>,
 *             device=<pci-dev-id>
 *
 * Note that vfio-user-server object must be used with x-remote machine only.
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
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "libvfio-user.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/remote/iohub.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/global_state.h"
#include "block/block.h"

#define TYPE_VFU_OBJECT "vfio-user-server"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

struct VfuObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;

    /* Maximum number of devices the server could support */
    unsigned int max_devs;
};

struct VfuObject {
    /* private */
    Object parent;

    SocketAddress *socket;

    char *device;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;

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

    QEMUFile *vfu_mig_file;

    vfu_migr_state_t vfu_state;
};

static void vfu_object_set_socket(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->socket);

    o->socket = NULL;

    visit_type_SocketAddress(v, name, &o->socket, errp);

    if (o->socket->type != SOCKET_ADDRESS_TYPE_UNIX) {
        error_setg(&error_abort, "vfu: Unsupported socket type - %s",
                   o->socket->u.q_unix.path);
        return;
    }

    trace_vfu_prop("socket", o->socket->u.q_unix.path);
}

static void vfu_object_set_device(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->device);

    o->device = g_strdup(str);

    trace_vfu_prop("device", str);
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
    VfuObject *o = opaque;
    uint64_t end = pos + iov_size(iov, iovcnt);
    int i;

    if (end > o->vfu_mig_buf_size) {
        o->vfu_mig_buf = g_realloc(o->vfu_mig_buf, end);
    }

    for (i = 0; i < iovcnt; i++) {
        memcpy((o->vfu_mig_buf + o->vfu_mig_buf_size), iov[i].iov_base,
               iov[i].iov_len);
        o->vfu_mig_buf_size += iov[i].iov_len;
        o->vfu_mig_buf_pending += iov[i].iov_len;
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
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(OBJECT(o));
    static int migrated_devs;
    Error *local_err = NULL;
    int ret;

    /**
     * TODO: move to VFU_MIGR_STATE_RESUME handler. Presently, the
     * VMSD data from source is not available at RESUME state.
     * Working on a fix for this.
     */
    if (!o->vfu_mig_file) {
        o->vfu_mig_file = qemu_fopen_ops(o, &vfu_mig_fops_load, false);
    }

    ret = qemu_remote_loadvm(o->vfu_mig_file);
    if (ret) {
        error_setg(&error_abort, "vfu: failed to restore device state");
        return;
    }

    qemu_file_shutdown(o->vfu_mig_file);
    o->vfu_mig_file = NULL;

    /* VFU_MIGR_STATE_RUNNING begins here */
    if (++migrated_devs == k->nr_devs) {
        bdrv_invalidate_cache_all(&local_err);
        if (local_err) {
            error_report_err(local_err);
            return;
        }

        vm_start();
    }
}

static void vfu_mig_state_stop(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(OBJECT(o));
    static int migrated_devs;

    /**
     * note: calling bdrv_inactivate_all() is not the best approach.
     *
     *  Ideally, we would identify the block devices (if any) indirectly
     *  linked (such as via a scs-hd device) to each of the migrated devices,
     *  and inactivate them individually. This is essential while operating
     *  the server in a storage daemon mode, with devices from different VMs.
     *
     *  However, we currently don't have this capability. As such, we need to
     *  inactivate all devices at the same time when migration is completed.
     */
    if (++migrated_devs == k->nr_devs) {
        bdrv_inactivate_all();
        vm_stop(RUN_STATE_PAUSED);
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
        if (!runstate_is_running()) {
            vfu_mig_state_running(vfu_ctx);
        }
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

    return o->vfu_mig_buf_pending;
}

static int vfu_mig_prepare_data(vfu_ctx_t *vfu_ctx, uint64_t *offset,
                                uint64_t *size)
{
    VfuObject *o = vfu_get_private(vfu_ctx);

    if (offset) {
        *offset = 0;
    }

    if (size) {
        *size = o->vfu_mig_buf_size;
    }

    return 0;
}

static ssize_t vfu_mig_read_data(vfu_ctx_t *vfu_ctx, void *buf,
                                 uint64_t size, uint64_t offset)
{
    VfuObject *o = vfu_get_private(vfu_ctx);

    if (offset > o->vfu_mig_buf_size) {
        return -1;
    }

    if ((offset + size) > o->vfu_mig_buf_size) {
        warn_report("vfu: buffer overflow - check pending_bytes");
        size = o->vfu_mig_buf_size - offset;
    }

    memcpy(buf, (o->vfu_mig_buf + offset), size);

    o->vfu_mig_buf_pending -= size;

    return size;
}

static ssize_t vfu_mig_write_data(vfu_ctx_t *vfu_ctx, void *data,
                                  uint64_t size, uint64_t offset)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint64_t end = offset + size;

    if (end > o->vfu_mig_buf_size) {
        o->vfu_mig_buf = g_realloc(o->vfu_mig_buf, end);
        o->vfu_mig_buf_size = end;
    }

    memcpy((o->vfu_mig_buf + offset), data, size);

    return size;
}

static int vfu_mig_data_written(vfu_ctx_t *vfu_ctx, uint64_t count)
{
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
    int ret = -1;

    while (ret != 0) {
        ret = vfu_run_ctx(o->vfu_ctx);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOTCONN) {
                qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
                o->vfu_poll_fd = -1;
                object_unparent(OBJECT(o));
                break;
            } else {
                error_setg(&error_abort, "vfu: Failed to run device %s - %s",
                           o->device, strerror(errno));
                 break;
            }
        }
    }
}

static void vfu_object_attach_ctx(void *opaque)
{
    VfuObject *o = opaque;
    int ret;

    qemu_set_fd_handler(o->vfu_poll_fd, NULL, NULL, NULL);
    o->vfu_poll_fd = -1;

retry_attach:
    ret = vfu_attach_ctx(o->vfu_ctx);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        goto retry_attach;
    } else if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach device %s to context - %s",
                   o->device, strerror(errno));
        return;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        error_setg(&error_abort, "vfu: Failed to get poll fd %s", o->device);
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
    MemoryRegion *subregion = NULL;
    g_autofree char *name = NULL;
    static unsigned int suffix;
    struct iovec *iov = &info->iova;

    if (!info->vaddr) {
        return;
    }

    name = g_strdup_printf("remote-mem-%u", suffix++);

    subregion = g_new0(MemoryRegion, 1);

    memory_region_init_ram_ptr(subregion, NULL, name,
                               iov->iov_len, info->vaddr);

    memory_region_add_subregion(get_system_memory(), (hwaddr)iov->iov_base,
                                subregion);

    trace_vfu_dma_register((uint64_t)iov->iov_base, iov->iov_len);
}

static int dma_unregister(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    MemoryRegion *mr = NULL;
    ram_addr_t offset;

    mr = memory_region_from_host(info->vaddr, &offset);
    if (!mr) {
        return 0;
    }

    memory_region_del_subregion(get_system_memory(), mr);

    object_unparent((OBJECT(mr)));

    trace_vfu_dma_unregister((uint64_t)info->iova.iov_base);

    return 0;
}

static ssize_t vfu_object_bar_rw(PCIDevice *pci_dev, hwaddr addr, size_t count,
                                 char * const buf, const bool is_write,
                                 bool is_io)
{
    AddressSpace *as = NULL;
    MemTxResult res;

    if (is_io) {
        as = &address_space_io;
    } else {
        as = pci_device_iommu_address_space(pci_dev);
    }

    trace_vfu_bar_rw_enter(is_write ? "Write" : "Read", (uint64_t)addr);

    res = address_space_rw(as, addr, MEMTXATTRS_UNSPECIFIED, (void *)buf,
                           (hwaddr)count, is_write);
    if (res != MEMTX_OK) {
        warn_report("vfu: failed to %s 0x%"PRIx64"",
                    is_write ? "write to" : "read from",
                    addr);
        return -1;
    }

    trace_vfu_bar_rw_exit(is_write ? "Write" : "Read", (uint64_t)addr);

    return count;
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
        hwaddr addr = (hwaddr)(pci_get_bar_addr(pci_dev, BAR_NO) + offset);    \
        bool is_io = !!(pci_dev->io_regions[BAR_NO].type &                     \
                        PCI_BASE_ADDRESS_SPACE);                               \
                                                                               \
        return vfu_object_bar_rw(pci_dev, addr, count, buf, is_write, is_io);  \
    }                                                                          \

VFU_OBJECT_BAR_HANDLER(0)
VFU_OBJECT_BAR_HANDLER(1)
VFU_OBJECT_BAR_HANDLER(2)
VFU_OBJECT_BAR_HANDLER(3)
VFU_OBJECT_BAR_HANDLER(4)
VFU_OBJECT_BAR_HANDLER(5)

static vfu_region_access_cb_t *vfu_object_bar_handlers[PCI_NUM_REGIONS] = {
    &vfu_object_bar0_handler,
    &vfu_object_bar1_handler,
    &vfu_object_bar2_handler,
    &vfu_object_bar3_handler,
    &vfu_object_bar4_handler,
    &vfu_object_bar5_handler,
};

/**
 * vfu_object_register_bars - Identify active BAR regions of pdev and setup
 *                            callbacks to handle read/write accesses
 */
static void vfu_object_register_bars(vfu_ctx_t *vfu_ctx, PCIDevice *pdev)
{
    int i;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        if (!pdev->io_regions[i].size) {
            continue;
        }

        vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX + i,
                         (size_t)pdev->io_regions[i].size,
                         vfu_object_bar_handlers[i],
                         VFU_REGION_FLAG_RW, NULL, 0, -1, 0);

        trace_vfu_bar_register(i, pdev->io_regions[i].addr,
                               pdev->io_regions[i].size);
    }
}

static int vfu_object_setup_irqs(vfu_ctx_t *vfu_ctx, PCIDevice *pci_dev)
{
    RemoteMachineState *machine = REMOTE_MACHINE(current_machine);
    RemoteIOHubState *iohub = &machine->iohub;
    int pirq, intx, ret;

    ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_INTX_IRQ, 1);
    if (ret < 0) {
        return ret;
    }

    intx = pci_get_byte(pci_dev->config + PCI_INTERRUPT_PIN) - 1;

    pirq = remote_iohub_map_irq(pci_dev, intx);

    iohub->vfu_ctx[pirq] = vfu_ctx;

    return 0;
}

/*
 * vfio-user-server depends on the availability of the 'socket' and 'device'
 * properties. It also depends on devices instantiated in QEMU. These
 * dependencies are not available during the instance_init phase of this
 * object's life-cycle. As such, the server is initialized after the
 * machine is setup. machine_init_done_notifier notifies vfio-user-server
 * when the machine is setup, and the dependencies are available.
 */
static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    DeviceState *dev = NULL;
    vfu_pci_type_t pci_type = VFU_PCI_TYPE_CONVENTIONAL;
    size_t migr_area_size;
    int ret;

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket->u.q_unix.path,
                                LIBVFIO_USER_FLAG_ATTACH_NB,
                                o, VFU_DEV_TYPE_PCI);
    if (o->vfu_ctx == NULL) {
        error_setg(&error_abort, "vfu: Failed to create context - %s",
                   strerror(errno));
        return;
    }

    dev = qdev_find_recursive(sysbus_get_default(), o->device);
    if (dev == NULL) {
        error_setg(&error_abort, "vfu: Device %s not found", o->device);
        return;
    }

    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(&error_abort, "vfu: %s not a PCI device", o->device);
        return;
    }

    o->pci_dev = PCI_DEVICE(dev);

    if (pci_is_express(o->pci_dev)) {
        pci_type = VFU_PCI_TYPE_EXPRESS;
    }

    ret = vfu_pci_init(o->vfu_ctx, pci_type, PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach PCI device %s to context - %s",
                   o->device, strerror(errno));
        return;
    }

    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_CFG_REGION_IDX,
                           pci_config_size(o->pci_dev), &vfu_object_cfg_access,
                           VFU_REGION_FLAG_RW | VFU_REGION_FLAG_ALWAYS_CB,
                           NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to setup config space handlers for %s- %s",
                   o->device, strerror(errno));
        return;
    }

    ret = vfu_setup_device_dma(o->vfu_ctx, &dma_register, &dma_unregister);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup DMA handlers for %s",
                   o->device);
        return;
    }

    vfu_object_register_bars(o->vfu_ctx, o->pci_dev);

    ret = vfu_object_setup_irqs(o->vfu_ctx, o->pci_dev);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup interrupts for %s",
                   o->device);
        return;
    }

    /*
     * TODO: The 0x20000 number used below is a temporary. We are working on
     *     a cleaner fix for this.
     *
     *     The libvfio-user library assumes that the remote knows the size of
     *     the data to be migrated at boot time, but that is not the case with
     *     VMSDs, as it can contain a variable-size buffer. 0x20000 is used
     *     as a sufficiently large buffer to demonstrate migration, but that
     *     cannot be used as a solution.
     *
     */
    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_MIGR_REGION_IDX,
                           0x20000, NULL,
                           VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to register migration BAR %s- %s",
                   o->device, strerror(errno));
        return;
    }

    migr_area_size = vfu_get_migr_register_area_size();
    ret = vfu_setup_device_migration_callbacks(o->vfu_ctx, &vfu_mig_cbs,
                                               migr_area_size);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup migration %s- %s",
                   o->device, strerror(errno));
        return;
    }

    ret = vfu_realize_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to realize device %s- %s",
                   o->device, strerror(errno));
        return;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        error_setg(&error_abort, "vfu: Failed to get poll fd %s", o->device);
        return;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_attach_ctx, NULL, o);
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    if (!object_dynamic_cast(OBJECT(current_machine), TYPE_REMOTE_MACHINE)) {
        error_setg(&error_abort, "vfu: %s only compatible with %s machine",
                   TYPE_VFU_OBJECT, TYPE_REMOTE_MACHINE);
        return;
    }

    if (k->nr_devs >= k->max_devs) {
        error_setg(&error_abort,
                   "Reached maximum number of vfio-user-server devices: %u",
                   k->max_devs);
        return;
    }

    o->vfu_ctx = NULL;

    k->nr_devs++;

    o->machine_done.notify = vfu_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);

    o->vfu_poll_fd = -1;

    o->vfu_mig_file = NULL;

    o->vfu_mig_buf = NULL;

    o->vfu_mig_buf_size = 0;

    o->vfu_mig_buf_pending = 0;

    o->vfu_state = VFU_MIGR_STATE_STOP;
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

    g_free(o->socket);

    if (o->vfu_ctx) {
        vfu_destroy_ctx(o->vfu_ctx);
    }

    g_free(o->device);

    if (k->nr_devs == 0) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void vfu_object_class_init(ObjectClass *klass, void *data)
{
    VfuObjectClass *k = VFU_OBJECT_CLASS(klass);

    /* Limiting maximum number of devices to 1 until IOMMU support is added */
    k->max_devs = 1;
    k->nr_devs = 0;

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
