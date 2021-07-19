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

static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    DeviceState *dev = NULL;
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
