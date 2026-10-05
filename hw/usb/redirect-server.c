/*
 * USB redirector, server side
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Exports a locally emulated USB device to a remote USB host over the
 * usbredir protocol, so a device emulated in one QEMU instance can be
 * enumerated by a host controller emulated in another one.
 *
 * Architecture. The left column is a request going to the device. The
 * right column is the answer coming back. The middle hop carries
 * usbredir messages over a socket. The top and bottom hops carry
 * USBPackets inside QEMU.
 *
 *     remote QEMU: guest driver -> EHCI/XHCI
 *              |                     ^
 *    USBPacket |                     | USBPacket
 *              v                     |
 *     "usb-redir" (the client)
 *              |                     ^
 *     usbredir |   chardev socket    | usbredir
 *              v                     |
 *     usb-redir-server (the server, this file)
 *              |                     ^
 *    USBPacket |                     | USBPacket
 *              v                     |
 *     any USBDevice, "-device <dev>,bus=<id>.0"
 *
 * "usb-redir" (hw/usb/redirect.c) is the client:
 *   - it takes a USBPacket from the remote guest and writes it to the
 *     socket as a usbredir message
 *   - it reads the answer from the socket and completes the USBPacket
 *
 * This file is the server. It does the same thing, but backwards:
 *   - it reads a usbredir message from the socket and runs it as a
 *     USBPacket on the bus below
 *   - it takes the result of that USBPacket and writes it back to the
 *     same socket as a usbredir message, for the client to read
 *
 * A USB device has to sit on a USB bus, and in QEMU a USB bus is always
 * made by a host controller. So this file makes one and acts as the
 * host controller on this side. It models no real chip: its cable is
 * the chardev socket. The real host is in the other QEMU.
 *
 * usbredir carries one device, not a bus. A hub cannot be exported:
 * the protocol has no device address field. To export several devices,
 * run one usb-redir-server per device, each with its own chardev.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/usb/redirect-server.h"

/*
 * USB port ops
 */

static void usbredir_server_port_child_detach(USBPort *port, USBDevice *child)
{
    /* We only export the device on our own port. Nothing to do. */
}

static void usbredir_server_port_wakeup(USBPort *port)
{
    /* We do not pass remote wakeup to the host. Nothing to do. */
}

static USBPortOps usbredir_server_port_ops = {
    .child_detach = usbredir_server_port_child_detach,
    .wakeup = usbredir_server_port_wakeup,
};

/*
 * USB bus ops
 */

static USBBusOps usbredir_server_bus_ops = {
};

/*
 * Device registration
 */

static void usbredir_server_realize(DeviceState *dev, Error **errp)
{
    USBRedirServer *s = USB_REDIR_SERVER(dev);

    /* One port: usbredir carries a single device. */
    usb_bus_new(&s->bus, sizeof(s->bus), &usbredir_server_bus_ops, dev);
    s->bus.no_auto_hub = true;
    usb_register_port(&s->bus, &s->port, s, 0, &usbredir_server_port_ops,
                      USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                      USB_SPEED_MASK_HIGH);
}

/*
 * The link to the remote host is a chardev, and we cannot migrate
 * that. So this device cannot be migrated either.
 */
static const VMStateDescription vmstate_usbredir_server = {
    .name = TYPE_USB_REDIR_SERVER,
    .unmigratable = 1,
};

static void usbredir_server_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "USB Redirection Server";
    dc->realize = usbredir_server_realize;
    dc->vmsd = &vmstate_usbredir_server;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo usbredir_server_types[] = {
    {
        .name = TYPE_USB_REDIR_SERVER,
        .parent = TYPE_DYNAMIC_SYS_BUS_DEVICE,
        .instance_size = sizeof(USBRedirServer),
        .class_init = usbredir_server_class_init,
    },
};
module_obj(TYPE_USB_REDIR_SERVER);
module_kconfig(USB);

DEFINE_TYPES(usbredir_server_types)
