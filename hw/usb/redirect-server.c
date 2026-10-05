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
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/usb/redirect-server.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "trace.h"

#define USBREDIR_SERVER_VERSION "qemu " TYPE_USB_REDIR_SERVER " " QEMU_VERSION

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
 * usbredirparser I/O and logging callbacks
 */

static void usbredir_server_log(void *priv, int level, const char *msg)
{
    switch (level) {
    case usbredirparser_error:
        error_report(TYPE_USB_REDIR_SERVER ": %s", msg);
        break;
    case usbredirparser_warning:
        warn_report(TYPE_USB_REDIR_SERVER ": %s", msg);
        break;
    default:
        trace_usbredir_server_log(msg);
        break;
    }
}

static int usbredir_server_read(void *priv, uint8_t *data, int count)
{
    USBRedirServer *s = priv;

    if (s->read_buf_size < count) {
        count = s->read_buf_size;
    }

    memcpy(data, s->read_buf, count);

    s->read_buf_size -= count;
    if (s->read_buf_size) {
        s->read_buf += count;
    } else {
        s->read_buf = NULL;
    }

    return count;
}

static gboolean usbredir_server_write_unblocked(void *do_not_use,
                                                GIOCondition cond,
                                                void *opaque)
{
    USBRedirServer *s = opaque;

    s->watch = 0;
    usbredirparser_do_write(s->parser);

    return G_SOURCE_REMOVE;
}

static int usbredir_server_write(void *priv, uint8_t *data, int count)
{
    USBRedirServer *s = priv;
    int ret;

    if (!qemu_chr_fe_backend_open(&s->cs)) {
        return 0;
    }

    /*
     * Re-entry guard. The chain is:
     *   do_write() -> this -> qemu_chr_fe_write() -> chardev feeds us
     *   -> do_read() -> a callback -> do_write() again
     *
     * The second do_write() would walk the same buffer queue as the
     * first. Returning 0 means "sent nothing", so the buffer stays for
     * the outer one to send.
     */
    if (s->in_write) {
        trace_usbredir_server_write_recursion();
        return 0;
    }
    s->in_write = true;

    ret = qemu_chr_fe_write(&s->cs, data, count);
    if (ret < count) {
        if (!s->watch) {
            s->watch = qemu_chr_fe_add_watch(&s->cs, G_IO_OUT | G_IO_HUP,
                                             usbredir_server_write_unblocked,
                                             s);
        }
        if (ret < 0) {
            ret = 0;
        }
    }

    s->in_write = false;

    return ret;
}

/* The remote host greets us once the socket is up. */
static void usbredir_server_hello(void *priv,
                                  struct usb_redir_hello_header *hello)
{
    USBRedirServer *s = priv;

    s->host_connected = true;
}

/*
 * Parser setup and teardown
 */

static void usbredir_server_create_parser(USBRedirServer *s)
{
    uint32_t caps[USB_REDIR_CAPS_SIZE] = {};

    s->parser = usbredirparser_create();
    if (!s->parser) {
        error_report(TYPE_USB_REDIR_SERVER ": failed to create usbredirparser");
        return;
    }

    s->parser->priv = s;
    s->parser->log_func = usbredir_server_log;
    s->parser->read_func = usbredir_server_read;
    s->parser->write_func = usbredir_server_write;

    /* Callbacks for messages the remote host sends to us */
    s->parser->hello_func = usbredir_server_hello;

    /* Capabilities: 64-bit IDs, connect_device_version, ep_info sizes */
    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);

    /*
     * In USB the host is the side that starts every transfer; a device
     * only answers. The exported device sits on our bus and we issue
     * its transfers, so for that device we are the host. That is what
     * fl_usb_host means, and why the side that exports a device sets it.
     *
     * The other QEMU does not drive the device, it receives it. In this
     * protocol that makes it the client, and redirect.c leaves the flag
     * clear.
     *
     * Without it the library refuses to send device_connect.
     */
    usbredirparser_init(s->parser, USBREDIR_SERVER_VERSION,
                        caps, USB_REDIR_CAPS_SIZE,
                        usbredirparser_fl_usb_host);
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_destroy_parser(USBRedirServer *s)
{
    s->host_connected = false;

    g_clear_handle_id(&s->watch, g_source_remove);

    if (s->parser) {
        usbredirparser_destroy(s->parser);
        s->parser = NULL;
    }
}

static void usbredir_server_chardev_close_bh(void *opaque)
{
    usbredir_server_destroy_parser(opaque);
}

/*
 * chardev callbacks
 */

static int usbredir_server_chardev_can_read(void *opaque)
{
    USBRedirServer *s = opaque;

    if (!s->parser) {
        return 0;
    }
    /* usbredirparser_do_read() consumes everything we hand it */
    return 1 * MiB;
}

static void usbredir_server_chardev_read(void *opaque, const uint8_t *buf,
                                         int size)
{
    USBRedirServer *s = opaque;

    if (!s->parser) {
        return;
    }

    /* No recursion allowed */
    assert(s->read_buf == NULL);

    s->read_buf = buf;
    s->read_buf_size = size;

    usbredirparser_do_read(s->parser);
    /* do_read() ran our callbacks; flush whatever replies they queued */
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_chardev_event(void *opaque,
                                          QEMUChrEvent event)
{
    USBRedirServer *s = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        trace_usbredir_server_chardev_open();
        /*
         * A close event only schedules chardev_close_bh. If it has not
         * run yet, it would destroy the parser we are about to create,
         * so run it now and cancel it.
         */
        usbredir_server_chardev_close_bh(s);
        qemu_bh_cancel(s->chardev_close_bh);
        usbredir_server_create_parser(s);
        break;
    case CHR_EVENT_CLOSED:
        trace_usbredir_server_chardev_close();
        qemu_bh_schedule(s->chardev_close_bh);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        break;
    }
}

/*
 * Device registration
 */

static void usbredir_server_realize(DeviceState *dev, Error **errp)
{
    USBRedirServer *s = USB_REDIR_SERVER(dev);

    if (!qemu_chr_fe_backend_connected(&s->cs)) {
        error_setg(errp,
                   TYPE_USB_REDIR_SERVER ": 'chardev' property must be set");
        return;
    }

    /* One port: usbredir carries a single device. */
    usb_bus_new(&s->bus, sizeof(s->bus), &usbredir_server_bus_ops, dev);
    s->bus.no_auto_hub = true;
    usb_register_port(&s->bus, &s->port, s, 0, &usbredir_server_port_ops,
                      USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                      USB_SPEED_MASK_HIGH);

    s->chardev_close_bh = qemu_bh_new_guarded(usbredir_server_chardev_close_bh,
                                              s, &dev->mem_reentrancy_guard);

    qemu_chr_fe_set_handlers(&s->cs,
                             usbredir_server_chardev_can_read,
                             usbredir_server_chardev_read,
                             usbredir_server_chardev_event,
                             NULL, s, NULL, true);
}

static void usbredir_server_unrealize(DeviceState *dev)
{
    USBRedirServer *s = USB_REDIR_SERVER(dev);

    qemu_chr_fe_deinit(&s->cs, true);
    usbredir_server_destroy_parser(s);

    if (s->chardev_close_bh) {
        qemu_bh_delete(s->chardev_close_bh);
        s->chardev_close_bh = NULL;
    }
}

static const Property usbredir_server_props[] = {
    DEFINE_PROP_CHR("chardev", USBRedirServer, cs),
};

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
    dc->unrealize = usbredir_server_unrealize;
    dc->vmsd = &vmstate_usbredir_server;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    device_class_set_props(dc, usbredir_server_props);
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
