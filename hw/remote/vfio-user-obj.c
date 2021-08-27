/**
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
#include "libvfio-user.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"

#define TYPE_VFU_OBJECT "vfio-user"
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

    char *socket;
    char *devid;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;

    int vfu_poll_fd;
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
                           o->devid, strerror(errno));
                 break;
            }
        }
    }
}

static void *vfu_object_attach_ctx(void *opaque)
{
    VfuObject *o = opaque;
    int ret;

retry_attach:
    ret = vfu_attach_ctx(o->vfu_ctx);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        goto retry_attach;
    } else if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach device %s to context - %s",
                   o->devid, strerror(errno));
        return NULL;
    }

    o->vfu_poll_fd = vfu_get_poll_fd(o->vfu_ctx);
    if (o->vfu_poll_fd < 0) {
        error_setg(&error_abort, "vfu: Failed to get poll fd %s", o->devid);
        return NULL;
    }

    qemu_set_fd_handler(o->vfu_poll_fd, vfu_object_ctx_run,
                        NULL, o);

    return NULL;
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
            pci_default_write_config(PCI_DEVICE(o->pci_dev),
                                     offset, val, len);
            trace_vfu_cfg_write(offset, val);
        } else {
            val = pci_default_read_config(PCI_DEVICE(o->pci_dev),
                                          offset, len);
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
    QemuThread thread;
    int ret;

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket,
                                LIBVFIO_USER_FLAG_ATTACH_NB,
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

    if (!object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_setg(&error_abort, "vfu: %s not a PCI devices", o->devid);
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

    ret = vfu_realize_ctx(o->vfu_ctx);
    if (ret < 0) {
        error_setg(&error_abort, "vfu: Failed to realize device %s- %s",
                   o->devid, strerror(errno));
        return;
    }

    qemu_thread_create(&thread, o->socket, vfu_object_attach_ctx, o,
                       QEMU_THREAD_DETACHED);
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    if (!object_dynamic_cast(OBJECT(current_machine), TYPE_REMOTE_MACHINE)) {
        error_report("vfu: %s only compatible with %s machine",
                     TYPE_VFU_OBJECT, TYPE_REMOTE_MACHINE);
        return;
    }

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of vfio-user devices: %u",
                     k->max_devs);
        return;
    }

    k->nr_devs++;

    o->machine_done.notify = vfu_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);

    o->vfu_poll_fd = -1;
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
