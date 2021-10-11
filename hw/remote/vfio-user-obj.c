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
