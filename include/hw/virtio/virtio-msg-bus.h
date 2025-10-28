/*
 * VirtIO MSG bus.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VIRTIO_MSG_BUS_H
#define QEMU_VIRTIO_MSG_BUS_H

#include "qom/object.h"
#include "system/dma.h"
#include "hw/qdev-core.h"
#include "hw/virtio/virtio-msg-prot.h"

#define TYPE_VIRTIO_MSG_BUS "virtio-msg-bus"
DECLARE_INSTANCE_CHECKER(BusState, VIRTIO_MSG_BUS,
                         TYPE_VIRTIO_MSG_BUS)


#define TYPE_VIRTIO_MSG_BUS_DEVICE "virtio-msg-bus-device"
OBJECT_DECLARE_TYPE(VirtIOMSGBusDevice, VirtIOMSGBusDeviceClass,
                    VIRTIO_MSG_BUS_DEVICE)

typedef struct VirtIOMSGBusPort {
    int (*receive)(VirtIOMSGBusDevice *bd, VirtIOMSG *msg);
    bool is_driver;
} VirtIOMSGBusPort;

struct VirtIOMSGBusDeviceClass {
    /*< private >*/
    DeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;

    /*
     * Ask the bus to receive and process all messages that
     * are readily available. The bus will call the registered
     * VirtIOMSGBusPort.receive() function for each message.
     *
     * Will return immediately if no messages are available.
     */
    void (*process)(VirtIOMSGBusDevice *bd);

    /*
     * Called by the transport to send a message.
     */
    int (*send)(VirtIOMSGBusDevice *bd, VirtIOMSG *msg_req);
};

typedef struct VirtIOMSGBusDevice {
    DeviceState parent;

    const VirtIOMSGBusPort *peer;
    void *opaque;
} VirtIOMSGBusDevice;

static inline VirtIOMSGBusDevice *virtio_msg_bus_get_device(BusState *qbus)
{
    BusChild *kid = QTAILQ_FIRST(&qbus->children);
    DeviceState *qdev = kid ? kid->child : NULL;

    return (VirtIOMSGBusDevice *)qdev;
}

static inline bool virtio_msg_bus_connected(BusState *bus)
{
    VirtIOMSGBusDevice *bd = virtio_msg_bus_get_device(bus);

    return bd && bd->peer != NULL;
}

void virtio_msg_bus_process(VirtIOMSGBusDevice *bd);

bool virtio_msg_bus_connect(BusState *bus,
                            const VirtIOMSGBusPort *port,
                            void *opaque);

static inline void
virtio_msg_bus_receive(VirtIOMSGBusDevice *bd, VirtIOMSG *msg)
{
    virtio_msg_unpack(msg);
    bd->peer->receive(bd, msg);
}

int virtio_msg_bus_send(BusState *bus, VirtIOMSG *msg_req);

static inline AddressSpace *virtio_msg_bus_get_remote_as(BusState *bus)
{
    return &address_space_memory;
}
#endif
