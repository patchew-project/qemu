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
 *     -machine x-remote
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

#define TYPE_VFU_OBJECT "x-vfio-user-server"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

/**
 * VFU_OBJECT_ERROR - reports an error message. If auto_shutdown
 * is set, it abort the machine on error. Otherwise, it logs an
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
};

static void vfu_object_init_ctx(VfuObject *o, Error **errp);

static void vfu_object_set_socket(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        return;
    }

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

    visit_type_SocketAddress(v, name, &o->socket, errp);

    if (o->socket->type != SOCKET_ADDRESS_TYPE_UNIX) {
        qapi_free_SocketAddress(o->socket);
        o->socket = NULL;
        error_setg(errp, "vfu: Unsupported socket type - %s",
                   SocketAddressType_str(o->socket->type));
        return;
    }

    trace_vfu_prop("socket", o->socket->u.q_unix.path);

    vfu_object_init_ctx(o, errp);
}

static void vfu_object_set_device(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    if (o->vfu_ctx) {
        return;
    }

    g_free(o->device);

    o->device = g_strdup(str);

    trace_vfu_prop("device", str);

    vfu_object_init_ctx(o, errp);
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
                qapi_event_send_vfu_client_hangup(o->device,
                                                  o->socket->u.q_unix.path);
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
        qemu_poll_ns(pfds, 1, 500 * (int64_t)SCALE_MS);
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

    memory_region_add_subregion(pci_address_space(o->pci_dev),
                                (hwaddr)iov->iov_base, subregion);

    trace_vfu_dma_register((uint64_t)iov->iov_base, iov->iov_len);
}

static void dma_unregister(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    VfuObject *o = vfu_get_private(vfu_ctx);
    MemoryRegion *mr = NULL;
    ram_addr_t offset;

    mr = memory_region_from_host(info->vaddr, &offset);
    if (!mr) {
        return;
    }

    memory_region_del_subregion(pci_address_space(o->pci_dev), mr);

    object_unparent((OBJECT(mr)));

    trace_vfu_dma_unregister((uint64_t)info->iova.iov_base);
}

static ssize_t vfu_object_bar_rw(PCIDevice *pci_dev, hwaddr addr, size_t count,
                                 char * const buf, const bool is_write,
                                 bool is_io)
{
    AddressSpace *as = NULL;
    MemTxResult res;

    if (is_io) {
        as = pci_isol_as_io(pci_dev);
        as = as ? as : &address_space_io;
    } else {
        as = pci_isol_as_mem(pci_dev);
        as = as ? as : &address_space_memory;
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

    error_setg(&o->unplug_blocker, "%s is in use", o->device);
    qdev_add_unplug_blocker(DEVICE(o->pci_dev), o->unplug_blocker, errp);
    if (*errp) {
        goto fail;
    }

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
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

    qapi_free_SocketAddress(o->socket);

    o->socket = NULL;

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

    o->pci_dev = NULL;

    if (!k->nr_devs && k->auto_shutdown) {
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
