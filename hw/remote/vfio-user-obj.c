/*
 * QEMU vfio-user server object
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
 *     -object vfio-user,id=<id>,socket=<socket-path>,devid=<pci-dev-id>
 *
 * Note that vfio-user object must be used with x-remote machine only. This
 * server could only support PCI devices for now.
 *
 * socket is path to a file. This file will be created by the server. It is
 * a required option
 *
 * devid is the id of a PCI device on the server. It is also a required option.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include <errno.h>

#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "hw/remote/iohub.h"
#include "hw/remote/machine.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/global_state.h"
#include "block/block.h"

#include "libvfio-user/include/libvfio-user.h"

#define TYPE_VFU_OBJECT "vfio-user"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

struct VfuObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;

    /* Maximum number of devices the server could support*/
    unsigned int max_devs;
};

struct VfuObject {
    /* private */
    Object parent;

    char *socket;
    char *devid;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;

    QemuThread vfu_ctx_thread;

    /*
     * vfu_mig_buf holds the migration data. In the remote process, this
     * buffer replaces the role of an IO channel which links the source
     * and the destination.
     *
     * Whenever the client QEMU process initiates migration, the libvfio-user
     * library notifies that to this server. The remote/server QEMU sets up a
     * QEMUFile object using this buffer as backend. The remote passes this
     * object to its migration subsystem, and it slirps the VMSDs of all its
     * devices and stores them in this buffer.
     *
     * libvfio-user library subsequetly asks the remote for any data that needs
     * to be moved over to the destination using its vfu_migration_callbacks_t
     * APIs. The remote hands over this buffer as data at this time.
     *
     * A reverse of this process happens at the destination.
     */
    uint8_t *vfu_mig_buf;

    uint64_t vfu_mig_buf_size;

    uint64_t vfu_mig_buf_pending;

    QEMUFile *vfu_mig_file;
};

static void vfu_object_set_socket(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->socket);

    o->socket = g_strdup(str);

    trace_vfu_prop("socket", str);
}

static void vfu_object_set_devid(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->devid);

    o->devid = g_strdup(str);

    trace_vfu_prop("devid", str);
}

/**
 * Migration helper functions
 *
 * vfu_mig_buf_read & vfu_mig_buf_write are used by QEMU's migration
 * subsystem - qemu_remote_savevm & qemu_remote_loadvm. savevm/loadvm
 * call these functions via QEMUFileOps to save/load the VMSD of all
 * the devices into vfu_mig_buf
 *
 */
static ssize_t vfu_mig_buf_read(void *opaque, uint8_t *buf, int64_t pos,
                                size_t size, Error **errp)
{
    VfuObject *o = opaque;

    if (pos > o->vfu_mig_buf_size) {
        size = 0;
    } else if ((pos + size) > o->vfu_mig_buf_size) {
        size = o->vfu_mig_buf_size;
    }

    memcpy(buf, (o->vfu_mig_buf + pos), size);

    o->vfu_mig_buf_size -= size;

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
static void vfu_mig_state_precopy(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    int ret;

    if (!o->vfu_mig_file) {
        o->vfu_mig_file = qemu_fopen_ops(o, &vfu_mig_fops_save);
    }

    global_state_store();

    ret = qemu_remote_savevm(o->vfu_mig_file);
    if (ret) {
        qemu_file_shutdown(o->vfu_mig_file);
        return;
    }

    qemu_fflush(o->vfu_mig_file);

    bdrv_inactivate_all();
}

static void vfu_mig_state_running(vfu_ctx_t *vfu_ctx)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    Error *local_err = NULL;
    int ret;

    ret = qemu_remote_loadvm(o->vfu_mig_file);
    if (ret) {
        error_setg(&error_abort, "vfu: failed to restore device state");
        return;
    }

    bdrv_invalidate_cache_all(&local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    vm_start();
}

static int vfu_mig_transition(vfu_ctx_t *vfu_ctx, vfu_migr_state_t state)
{
    switch (state) {
    case VFU_MIGR_STATE_RESUME:
    case VFU_MIGR_STATE_STOP_AND_COPY:
    case VFU_MIGR_STATE_STOP:
        break;
    case VFU_MIGR_STATE_PRE_COPY:
        vfu_mig_state_precopy(vfu_ctx);
        break;
    case VFU_MIGR_STATE_RUNNING:
        if (!runstate_is_running()) {
            vfu_mig_state_running(vfu_ctx);
        }
        break;
    default:
        warn_report("vfu: Unknown migration state %d", state);
    }

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

    if (!o->vfu_mig_file) {
        o->vfu_mig_file = qemu_fopen_ops(o, &vfu_mig_fops_load);
    }

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

static void *vfu_object_ctx_run(void *opaque)
{
    VfuObject *o = opaque;
    int ret;

    ret = vfu_realize_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to realize device %s- %s",
                   o->devid, strerror(errno));
        return NULL;
    }

    ret = vfu_attach_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach device %s to context - %s",
                   o->devid, strerror(errno));
        return NULL;
    }

    do {
        ret = vfu_run_ctx(o->vfu_ctx);
        if (ret < 0) {
            if (errno == EINTR) {
                ret = 0;
            } else if (errno == ENOTCONN) {
                object_unparent(OBJECT(o));
                break;
            } else {
                error_setg(&error_abort, "vfu: Failed to run device %s - %s",
                           o->devid, strerror(errno));
            }
        }
    } while (ret == 0);

    return NULL;
}

static ssize_t vfu_object_cfg_access(vfu_ctx_t *vfu_ctx, char * const buf,
                                     size_t count, loff_t offset,
                                     const bool is_write)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    uint32_t val = 0;
    int i;

    qemu_mutex_lock_iothread();

    for (i = 0; i < count; i++) {
        if (is_write) {
            val = *((uint8_t *)(buf + i));
            trace_vfu_cfg_write((offset + i), val);
            pci_default_write_config(PCI_DEVICE(o->pci_dev),
                                     (offset + i), val, 1);
        } else {
            val = pci_default_read_config(PCI_DEVICE(o->pci_dev),
                                          (offset + i), 1);
            *((uint8_t *)(buf + i)) = (uint8_t)val;
            trace_vfu_cfg_read((offset + i), val);
        }
    }

    qemu_mutex_unlock_iothread();

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

    qemu_mutex_lock_iothread();

    memory_region_init_ram_ptr(subregion, NULL, name,
                               iov->iov_len, info->vaddr);

    memory_region_add_subregion(get_system_memory(), (hwaddr)iov->iov_base,
                                subregion);

    qemu_mutex_unlock_iothread();

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

    qemu_mutex_lock_iothread();

    memory_region_del_subregion(get_system_memory(), mr);

    object_unparent((OBJECT(mr)));

    qemu_mutex_unlock_iothread();

    trace_vfu_dma_unregister((uint64_t)info->iova.iov_base);

    return 0;
}

static ssize_t vfu_object_bar_rw(PCIDevice *pci_dev, hwaddr addr, size_t count,
                                 char * const buf, const bool is_write,
                                 uint8_t type)
{
    AddressSpace *as = NULL;
    MemTxResult res;

    if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
        as = pci_device_iommu_address_space(pci_dev);
    } else {
        as = &address_space_io;
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
        hwaddr addr = (hwaddr)(pci_get_long(o->pci_dev->config +               \
                                            PCI_BASE_ADDRESS_0 +               \
                                            (4 * BAR_NO)) + offset);           \
                                                                               \
        return vfu_object_bar_rw(o->pci_dev, addr, count, buf, is_write,       \
                                 o->pci_dev->io_regions[BAR_NO].type);         \
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
    uint32_t orig_val, new_val;
    int i, size;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        orig_val = pci_default_read_config(pdev,
                                           PCI_BASE_ADDRESS_0 + (4 * i), 4);
        new_val = 0xffffffff;
        pci_default_write_config(pdev,
                                 PCI_BASE_ADDRESS_0 + (4 * i), new_val, 4);
        new_val = pci_default_read_config(pdev,
                                          PCI_BASE_ADDRESS_0 + (4 * i), 4);
        size = (~(new_val & 0xFFFFFFF0)) + 1;
        pci_default_write_config(pdev, PCI_BASE_ADDRESS_0 + (4 * i),
                                 orig_val, 4);
        if (size) {
            vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX + i, size,
                             vfu_object_bar_handlers[i], VFU_REGION_FLAG_RW,
                             NULL, 0, -1, 0);
        }
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

static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    DeviceState *dev = NULL;
    size_t migr_area_size;
    int ret;

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket, 0,
                                o, VFU_DEV_TYPE_PCI);
    if (o->vfu_ctx == NULL) {
        error_setg(&error_abort, "vfu: Failed to create context - %s",
                   strerror(errno));
        return;
    }

    dev = qdev_find_recursive(sysbus_get_default(), o->devid);
    if (dev == NULL) {
        error_setg(&error_abort, "vfu: Device %s not found", o->devid);
        return;
    }
    o->pci_dev = PCI_DEVICE(dev);

    ret = vfu_pci_init(o->vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL,
                       PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach PCI device %s to context - %s",
                   o->devid, strerror(errno));
        return;
    }

    vfu_pci_set_id(o->vfu_ctx,
                   pci_get_word(o->pci_dev->config + PCI_VENDOR_ID),
                   pci_get_word(o->pci_dev->config + PCI_DEVICE_ID),
                   pci_get_word(o->pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID),
                   pci_get_word(o->pci_dev->config + PCI_SUBSYSTEM_ID));

    ret = vfu_setup_region(o->vfu_ctx, VFU_PCI_DEV_CFG_REGION_IDX,
                           pci_config_size(o->pci_dev), &vfu_object_cfg_access,
                           VFU_REGION_FLAG_RW | VFU_REGION_FLAG_ALWAYS_CB,
                           NULL, 0, -1, 0);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to setup config space handlers for %s- %s",
                   o->devid, strerror(errno));
        return;
    }

    ret = vfu_setup_device_dma(o->vfu_ctx, &dma_register, &dma_unregister);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup DMA handlers for %s",
                   o->devid);
        return;
    }

    vfu_object_register_bars(o->vfu_ctx, o->pci_dev);

    ret = vfu_object_setup_irqs(o->vfu_ctx, o->pci_dev);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup interrupts for %s",
                   o->devid);
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
                   o->devid, strerror(errno));
        return;
    }

    migr_area_size = vfu_get_migr_register_area_size();
    ret = vfu_setup_device_migration_callbacks(o->vfu_ctx, &vfu_mig_cbs,
                                               migr_area_size);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to setup migration %s- %s",
                   o->devid, strerror(errno));
        return;
    }

    qemu_thread_create(&o->vfu_ctx_thread, "VFU ctx runner", vfu_object_ctx_run,
                       o, QEMU_THREAD_JOINABLE);
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    /* Add test for remote machine and PCI device */

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of vfio-user devices: %u",
                     k->max_devs);
        return;
    }

    k->nr_devs++;

    o->machine_done.notify = vfu_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);

    o->vfu_mig_file = NULL;

    o->vfu_mig_buf = NULL;

    o->vfu_mig_buf_size = 0;

    o->vfu_mig_buf_pending = 0;
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

    vfu_destroy_ctx(o->vfu_ctx);

    g_free(o->socket);
    g_free(o->devid);

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

    object_class_property_add_str(klass, "socket", NULL,
                                  vfu_object_set_socket);
    object_class_property_add_str(klass, "devid", NULL,
                                  vfu_object_set_devid);
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
