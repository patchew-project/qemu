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
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "hw/usb/redirect-server.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "desc.h"
#include "trace.h"

#define USBREDIR_SERVER_VERSION "qemu " TYPE_USB_REDIR_SERVER " " QEMU_VERSION

/* Wait this long after attach before we announce the device. */
#define USBREDIR_SERVER_ANNOUNCE_DEBOUNCE_MS 10

static void usbredir_server_pkt_free(USBRedirServerPkt *rp);
static void usbredir_server_stop_transfers(USBRedirServer *s);
static void usbredir_server_send_cancelled(USBRedirServer *s,
                                           USBRedirServerPkt *rp);

/*
 * The device is whatever USBDevice the user plugged into our port with
 * "-device <device>,bus=<id>.0". NULL until then.
 */
static USBDevice *usbredir_server_device(USBRedirServer *s)
{
    return s->port.dev;
}

/*
 * Descriptor snooping and device announcement
 */

static void usbredir_server_record_endpoint(USBRedirServer *s,
                                            const USBDescriptor *desc,
                                            uint8_t iface)
{
    uint8_t addr;
    int ep_nr;
    int idx;

    if (desc->bLength < 7) {
        return;
    }

    addr = desc->u.endpoint.bEndpointAddress;
    ep_nr = addr & 0x0f;
    idx = (addr & USB_DIR_IN) ? ep_nr + USBREDIR_SERVER_EP_IN_BASE : ep_nr;
    if (ep_nr < 1 || idx >= USBREDIR_SERVER_MAX_EP) {
        return;
    }

    s->ep_type[idx] = desc->u.endpoint.bmAttributes & 0x03;
    s->ep_max_packet[idx] = (desc->u.endpoint.wMaxPacketSize_hi << 8) |
                            desc->u.endpoint.wMaxPacketSize_lo;
    s->ep_interval[idx] = desc->u.endpoint.bInterval;
    s->ep_iface[idx] = iface;
}

/*
 * A device that only passes transfers through never gets its endpoint
 * types filled in: they stay INVALID and there is nothing to put in
 * ep_info. Take them from the configuration descriptor as it goes past.
 * Without them the host sees type 255 and refuses to move data.
 */
static void usbredir_server_snoop_config_desc(USBRedirServer *s,
                                              const uint8_t *data, int len)
{
    const USBDescriptor *desc;
    uint8_t iface = 0;
    int i = 0;

    while (i + 2 <= len) {
        desc = (const USBDescriptor *)(data + i);

        if (desc->bLength < 2 || i + desc->bLength > len) {
            break;
        }

        if (desc->bDescriptorType == USB_DT_INTERFACE && desc->bLength >= 3) {
            iface = desc->u.interface.bInterfaceNumber;
        } else if (desc->bDescriptorType == USB_DT_ENDPOINT) {
            usbredir_server_record_endpoint(s, desc, iface);
        }

        i += desc->bLength;
    }
}

static void usbredir_server_send_ep_info(USBRedirServer *s)
{
    struct usb_redir_ep_info_header ep_info = {};
    int i;

    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        ep_info.type[i] = s->ep_type[i];
        ep_info.max_packet_size[i] = s->ep_max_packet[i];
        ep_info.interface[i] = s->ep_iface[i];
        /*
         * bInterval says how often to poll this endpoint. Pass on what
         * the descriptor said, but never 0 for an interrupt or isochronous
         * endpoint: redirect.c throws the whole device away when it sees
         * 0, so send 1 instead.
         */
        ep_info.interval[i] = s->ep_interval[i];
        if (ep_info.interval[i] == 0 &&
            (s->ep_type[i] == USB_ENDPOINT_XFER_INT ||
             s->ep_type[i] == USB_ENDPOINT_XFER_ISOC)) {
            ep_info.interval[i] = 1;
        }
        if (s->ep_type[i] != USB_ENDPOINT_XFER_INVALID) {
            trace_usbredir_server_ep_info(i, ep_info.type[i],
                                          ep_info.max_packet_size[i],
                                          ep_info.interval[i],
                                          ep_info.interface[i]);
        }
    }

    usbredirparser_send_ep_info(s->parser, &ep_info);
    usbredirparser_do_write(s->parser);
}

static uint8_t usbredir_server_speed(USBDevice *device)
{
    switch (device->speed) {
    case USB_SPEED_LOW:
        return usb_redir_speed_low;
    case USB_SPEED_FULL:
        return usb_redir_speed_full;
    default:
        return usb_redir_speed_high;
    }
}

static void usbredir_server_announce_device(USBRedirServer *s)
{
    USBDevice *device = usbredir_server_device(s);
    struct usb_redir_interface_info_header iface_info = {
        .interface_count = 0,
    };
    struct usb_redir_device_connect_header conn = {
        .speed = usbredir_server_speed(device),
    };

    if (s->device_announced) {
        return;
    }
    s->device_announced = true;

    /* Put the device in DEFAULT state. The host may not reset the bus. */
    device->addr = 0;
    device->state = USB_STATE_DEFAULT;

    /* Send this before device_connect. The peer needs it to accept us. */
    usbredirparser_send_interface_info(s->parser, &iface_info);
    usbredirparser_do_write(s->parser);

    trace_usbredir_server_announce(conn.speed);
    usbredirparser_send_device_connect(s->parser, &conn);
    usbredirparser_do_write(s->parser);

    usbredir_server_send_ep_info(s);
}

/*
 * Packet completion
 */

static uint8_t usbredir_server_status(int status)
{
    switch (status) {
    case USB_RET_SUCCESS:
        return usb_redir_success;
    case USB_RET_STALL:
        return usb_redir_stall;
    case USB_RET_BABBLE:
        return usb_redir_babble;
    default:
        return usb_redir_ioerror;
    }
}

/*
 * The device answered the setup token of an IN control request. What it
 * wants to send is now in device->data_buf, so run the data and status
 * stages and pass the answer to the host.
 *
 * Each stage reuses rp->pkt, and usb_packet_setup() asserts iov->iov is
 * not NULL, so call qemu_iovec_init() before every stage.
 */
static void usbredir_server_ctrl_setup_complete(USBRedirServer *s,
                                                USBRedirServerPkt *rp)
{
    struct usb_redir_control_packet_header resp = rp->ctrl_hdr;
    struct usb_redir_configuration_status_header cfg = {};
    struct usb_redir_alt_setting_status_header alt = {
        .interface = rp->ctrl_hdr.index,
    };
    USBDevice *device = usbredir_server_device(s);
    USBEndpoint *ep_out = usb_ep_get(device, USB_TOKEN_OUT, 0);
    USBEndpoint *ep_in = usb_ep_get(device, USB_TOKEN_IN, 0);
    uint8_t status = usbredir_server_status(rp->pkt.status);
    int actual = 0;

    if (rp->pkt.status == USB_RET_SUCCESS) {
        /* Data stage: the core copies device->data_buf into our buffer. */
        qemu_iovec_init(&rp->pkt.iov, 1);
        usb_packet_setup(&rp->pkt, USB_TOKEN_IN, ep_in,
                         0, s->next_id++, false, false);
        usb_packet_addbuf(&rp->pkt, rp->data, rp->data_size);
        usb_handle_packet(device, &rp->pkt);
        actual = rp->pkt.actual_length;
        usb_packet_cleanup(&rp->pkt);

        /* Status stage: tell the device we got it. Its EP0 goes idle. */
        qemu_iovec_init(&rp->pkt.iov, 1);
        usb_packet_setup(&rp->pkt, USB_TOKEN_OUT, ep_out,
                         0, s->next_id++, false, false);
        usb_handle_packet(device, &rp->pkt);
        usb_packet_cleanup(&rp->pkt);

        if (rp->ctrl_hdr.request == USB_REQ_GET_DESCRIPTOR &&
            (rp->ctrl_hdr.value >> 8) == USB_DT_CONFIG && actual > 0) {
            usbredir_server_snoop_config_desc(s, rp->data, actual);
        }
    }

    trace_usbredir_server_ctrl_setup_complete(rp->redir_id, status,
                                              actual);

    switch (rp->reply) {
    case USBREDIR_SERVER_REPLY_CONTROL:
        resp.status = status;
        resp.length = actual;
        usbredirparser_send_control_packet(s->parser, rp->redir_id, &resp,
                                           actual > 0 ? rp->data : NULL,
                                           actual);
        break;
    case USBREDIR_SERVER_REPLY_CONFIG:
        cfg.status = status;
        cfg.configuration = actual > 0 ? rp->data[0] : 0;
        usbredirparser_send_configuration_status(s->parser, rp->redir_id,
                                                 &cfg);
        break;
    case USBREDIR_SERVER_REPLY_ALT:
        alt.status = status;
        alt.alt = actual > 0 ? rp->data[0] : 0;
        usbredirparser_send_alt_setting_status(s->parser, rp->redir_id,
                                               &alt);
        break;
    }
    usbredirparser_do_write(s->parser);
}

/*
 * The status stage of an OUT control request finished, so the device is
 * done. Tell the host how it went, with whichever message it is waiting
 * for.
 */
static void usbredir_server_ctrl_status_complete(USBRedirServer *s,
                                                 USBRedirServerPkt *rp)
{
    struct usb_redir_control_packet_header resp = rp->ctrl_hdr;
    uint8_t status = usbredir_server_status(rp->pkt.status);
    struct usb_redir_configuration_status_header cfg = {
        .configuration = rp->ctrl_hdr.value,
    };
    struct usb_redir_alt_setting_status_header alt = {
        .interface = rp->ctrl_hdr.index,
        .alt = rp->ctrl_hdr.value,
    };

    trace_usbredir_server_ctrl_status_complete(rp->redir_id, status);

    switch (rp->reply) {
    case USBREDIR_SERVER_REPLY_CONTROL:
        resp.status = status;
        resp.length = 0;
        usbredirparser_send_control_packet(s->parser, rp->redir_id,
                                           &resp, NULL, 0);
        break;
    case USBREDIR_SERVER_REPLY_CONFIG:
        cfg.status = status;
        usbredirparser_send_configuration_status(s->parser, rp->redir_id,
                                                 &cfg);
        break;
    case USBREDIR_SERVER_REPLY_ALT:
        alt.status = status;
        usbredirparser_send_alt_setting_status(s->parser, rp->redir_id,
                                               &alt);
        break;
    }
    usbredirparser_do_write(s->parser);

    /* The endpoint list changed. Send the new one. */
    if (status == usb_redir_success &&
        (rp->ctrl_hdr.request == USB_REQ_SET_CONFIGURATION ||
         rp->ctrl_hdr.request == USB_REQ_SET_INTERFACE)) {
        usbredir_server_send_ep_info(s);
    }
}

static void usbredir_server_bulk_complete(USBRedirServer *s,
                                          USBRedirServerPkt *rp)
{
    struct usb_redir_bulk_packet_header resp = rp->bulk_hdr;
    bool is_in = !!(rp->bulk_hdr.endpoint & USB_DIR_IN);
    int actual = rp->pkt.actual_length;
    int len;

    resp.status = usbredir_server_status(rp->pkt.status);

    /* Only an IN transfer carries data back. */
    len = is_in ? actual : 0;
    resp.length = len;
    resp.length_high = len >> 16;

    trace_usbredir_server_bulk_complete(rp->redir_id, resp.endpoint,
                                        resp.status, actual);

    usbredirparser_send_bulk_packet(s->parser, rp->redir_id, &resp,
                                    len > 0 ? rp->data : NULL, len);
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_intr_complete(USBRedirServer *s,
                                          USBRedirServerPkt *rp)
{
    struct usb_redir_interrupt_packet_header resp = rp->intr_hdr;
    bool is_in = !!(rp->intr_hdr.endpoint & USB_DIR_IN);
    int actual = rp->pkt.actual_length;
    int len;

    resp.status = usbredir_server_status(rp->pkt.status);

    /* Only an IN transfer carries data back. */
    len = is_in ? actual : 0;
    resp.length = len;

    trace_usbredir_server_intr_complete(rp->redir_id, resp.endpoint,
                                        resp.status, actual);

    usbredirparser_send_interrupt_packet(s->parser, rp->redir_id, &resp,
                                         len > 0 ? rp->data : NULL,
                                         len);
    usbredirparser_do_write(s->parser);
}

/*
 * USB port ops
 */

static void usbredir_server_schedule_announce(USBRedirServer *s)
{
    USBDevice *device = usbredir_server_device(s);

    if (!s->host_connected || s->device_announced || !device ||
        !device->attached) {
        return;
    }

    timer_mod(s->announce_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              USBREDIR_SERVER_ANNOUNCE_DEBOUNCE_MS);
}

static void usbredir_server_do_announce(void *opaque)
{
    USBRedirServer *s = opaque;
    USBDevice *device = usbredir_server_device(s);

    /* Only announce if the device is still attached and the host is here. */
    if (s->host_connected && s->parser && device && device->attached) {
        usbredir_server_announce_device(s);
    }
}

static void usbredir_server_port_attach(USBPort *port)
{
    USBRedirServer *s = port->opaque;

    trace_usbredir_server_attach();
    usbredir_server_schedule_announce(s);
}

static void usbredir_server_port_detach(USBPort *port)
{
    USBRedirServer *s = port->opaque;

    trace_usbredir_server_detach(s->device_announced);

    timer_del(s->announce_timer);

    usbredir_server_stop_transfers(s);

    if (s->host_connected && s->parser && s->device_announced) {
        trace_usbredir_server_disconnect();
        usbredirparser_send_device_disconnect(s->parser);
        usbredirparser_do_write(s->parser);
    }
    s->device_announced = false;

    /* Clear the endpoint tables: they belong to the device that is leaving. */
    memset(s->ep_type, USB_ENDPOINT_XFER_INVALID, sizeof(s->ep_type));
    memset(s->ep_max_packet, 0, sizeof(s->ep_max_packet));
    memset(s->ep_interval, 0, sizeof(s->ep_interval));
    memset(s->ep_iface, 0, sizeof(s->ep_iface));
}

static void usbredir_server_port_child_detach(USBPort *port, USBDevice *child)
{
    /* We only export the device on our own port. Nothing to do. */
}

static void usbredir_server_port_wakeup(USBPort *port)
{
    /* We do not pass remote wakeup to the host. Nothing to do. */
}

/*
 * The core calls this for a packet the device answered with
 * USB_RET_ASYNC. usbredir_server_submit_to_device() calls it for the rest.
 */
static void usbredir_server_packet_complete(USBPort *port, USBPacket *p)
{
    USBRedirServer *s = port->opaque;
    USBRedirServerPkt *rp = container_of(p, USBRedirServerPkt, pkt);
    USBDevice *device = usbredir_server_device(s);

    QTAILQ_REMOVE(&s->inflight, rp, next);
    usb_packet_cleanup(&rp->pkt);

    if (!s->parser || !device) {
        usbredir_server_pkt_free(rp);
        return;
    }

    switch (rp->type) {
    case USBREDIR_SERVER_CTRL_SETUP:
        usbredir_server_ctrl_setup_complete(s, rp);
        break;
    case USBREDIR_SERVER_CTRL_STATUS:
        usbredir_server_ctrl_status_complete(s, rp);
        break;
    case USBREDIR_SERVER_BULK:
        usbredir_server_bulk_complete(s, rp);
        break;
    case USBREDIR_SERVER_INTR:
        usbredir_server_intr_complete(s, rp);
        break;
    }

    usbredir_server_pkt_free(rp);
}

static USBPortOps usbredir_server_port_ops = {
    .attach = usbredir_server_port_attach,
    .detach = usbredir_server_port_detach,
    .child_detach = usbredir_server_port_child_detach,
    .wakeup = usbredir_server_port_wakeup,
    .complete = usbredir_server_packet_complete,
};

/*
 * USB bus ops
 */

static USBBusOps usbredir_server_bus_ops = {
};

/*
 * Submit a packet to the device. The core only calls our completion
 * callback when the device answers USB_RET_ASYNC, so call it here for
 * the rest.
 */
static void usbredir_server_submit_to_device(USBRedirServer *s,
                                             USBRedirServerPkt *rp)
{
    QTAILQ_INSERT_TAIL(&s->inflight, rp, next);
    usb_handle_packet(usbredir_server_device(s), &rp->pkt);
    if (rp->pkt.status != USB_RET_ASYNC) {
        usbredir_server_packet_complete(&s->port, &rp->pkt);
    }
}

static USBRedirServerPkt *usbredir_server_pkt_alloc(int size)
{
    USBRedirServerPkt *rp = g_new0(USBRedirServerPkt, 1);

    qemu_iovec_init(&rp->pkt.iov, 1);
    rp->data = g_malloc0(size);
    rp->data_size = size;
    return rp;
}

static void usbredir_server_pkt_free(USBRedirServerPkt *rp)
{
    g_free(rp->data);
    g_free(rp);
}

/*
 * Take @rp off the list and free it. Do not use usb_packet_complete():
 * it calls back into usbredir_server_packet_complete(), which would
 * remove the entry a second time and free it. usb_cancel_packet() only
 * tells the device to let go.
 */
static void usbredir_server_drop_pkt(USBRedirServer *s,
                                     USBRedirServerPkt *rp)
{
    QTAILQ_REMOVE(&s->inflight, rp, next);
    if (usb_packet_is_inflight(&rp->pkt)) {
        usb_cancel_packet(&rp->pkt);
    }
    usb_packet_cleanup(&rp->pkt);
    usbredir_server_pkt_free(rp);
}

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

/*
 * usbredirparser message callbacks
 */

static void usbredir_server_hello(void *priv,
                                  struct usb_redir_hello_header *hello)
{
    USBRedirServer *s = priv;
    char version[sizeof(hello->version) + 1];

    pstrcpy(version, sizeof(version), hello->version);
    trace_usbredir_server_hello(version);

    s->host_connected = true;
    usbredir_server_schedule_announce(s);
}

static void usbredir_server_reset(void *priv)
{
    USBRedirServer *s = priv;
    USBDevice *device = usbredir_server_device(s);

    trace_usbredir_server_bus_reset(device && device->attached);
    if (!device || !device->attached) {
        return;
    }

    usbredir_server_stop_transfers(s);
    usb_device_reset(device);
}

/*
 * Run one control transfer on the device. @reply says which message
 * answers the host when the transfer finishes.
 */
static void usbredir_server_do_control(USBRedirServer *s, uint64_t id,
    struct usb_redir_control_packet_header *hdr,
    uint8_t *data, int data_len, USBRedirServerReply reply)
{
    USBDevice *device = usbredir_server_device(s);
    USBRedirServerPkt *rp;
    USBEndpoint *ep_out;
    USBEndpoint *ep_in;
    USBEndpoint *ep0;
    bool is_in;
    int size;

    if (!s->host_connected || !device || !device->attached) {
        return;
    }

    trace_usbredir_server_control(id, hdr->requesttype, hdr->request,
                                  hdr->value, hdr->index, hdr->length);

    is_in = !!(hdr->requesttype & USB_DIR_IN);

    ep0 = usb_ep_get(device, USB_TOKEN_SETUP, 0);

    /* Room for the setup bytes and for the data of either direction. */
    size = MAX(hdr->length, data_len);
    size = MAX(size, (int)sizeof(device->setup_buf));

    rp = usbredir_server_pkt_alloc(size);
    rp->redir_id = id;
    rp->reply = reply;
    rp->ctrl_hdr = *hdr;

    /*
     * Build the raw 8-byte SETUP packet in rp->data. The IN path
     * overwrites it later with the answer from the device.
     */
    rp->data[0] = hdr->requesttype;
    rp->data[1] = hdr->request;
    rp->data[2] = hdr->value & 0xff;
    rp->data[3] = (hdr->value >> 8) & 0xff;
    rp->data[4] = hdr->index & 0xff;
    rp->data[5] = (hdr->index >> 8) & 0xff;
    rp->data[6] = hdr->length & 0xff;
    rp->data[7] = (hdr->length >> 8) & 0xff;

    if (is_in) {
        /*
         * An IN request. The device starts the work on the setup token
         * and may take its time, so send the token and pick the rest up
         * in usbredir_server_ctrl_setup_complete().
         */
        rp->type = USBREDIR_SERVER_CTRL_SETUP;
        usb_packet_setup(&rp->pkt, USB_TOKEN_SETUP, ep0,
                         0, s->next_id++, false, false);
        usb_packet_addbuf(&rp->pkt, rp->data, sizeof(device->setup_buf));
        usbredir_server_submit_to_device(s, rp);
    } else {
        /*
         * An OUT request. The device only stores the setup bytes now and
         * does the work on the status stage, so run the first two stages
         * here and wait on the last one.
         */
        ep_in = usb_ep_get(device, USB_TOKEN_IN, 0);
        ep_out = usb_ep_get(device, USB_TOKEN_OUT, 0);

        /* Setup stage: hand the device the 8 setup bytes. */
        usb_packet_setup(&rp->pkt, USB_TOKEN_SETUP, ep0,
                         0, s->next_id++, false, false);
        usb_packet_addbuf(&rp->pkt, rp->data, sizeof(device->setup_buf));
        usb_handle_packet(device, &rp->pkt);
        usb_packet_cleanup(&rp->pkt);

        /* Data stage: send the bytes that came with the request. */
        if (data_len > 0) {
            memcpy(rp->data, data, data_len);
            qemu_iovec_init(&rp->pkt.iov, 1);
            usb_packet_setup(&rp->pkt, USB_TOKEN_OUT, ep_out,
                             0, s->next_id++, false, false);
            usb_packet_addbuf(&rp->pkt, rp->data, data_len);
            /* The core copies our bytes into device->data_buf. */
            usb_handle_packet(device, &rp->pkt);
            usb_packet_cleanup(&rp->pkt);
        }

        /* Status stage: the device does the work here, so wait for it. */
        rp->type = USBREDIR_SERVER_CTRL_STATUS;
        qemu_iovec_init(&rp->pkt.iov, 1);
        usb_packet_setup(&rp->pkt, USB_TOKEN_IN, ep_in,
                         0, s->next_id++, false, false);
        /* async; the answer comes in usbredir_server_ctrl_status_complete() */
        usbredir_server_submit_to_device(s, rp);
    }
}

static void usbredir_server_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *hdr,
    uint8_t *data, int data_len)
{
    usbredir_server_do_control(priv, id, hdr, data, data_len,
                               USBREDIR_SERVER_REPLY_CONTROL);
}

static void usbredir_server_set_configuration(void *priv, uint64_t id,
    struct usb_redir_set_configuration_header *hdr)
{
    struct usb_redir_control_packet_header ctrl = {
        .endpoint = 0,
        .request = USB_REQ_SET_CONFIGURATION,
        /* Host->Device, Standard, Device */
        .requesttype = 0x00,
        .status = 0,
        .value = hdr->configuration,
        .index = 0,
        .length = 0,
    };
    USBRedirServer *s = priv;

    /* The host gets a configuration_status when the device answers. */
    usbredir_server_do_control(s, id, &ctrl, NULL, 0,
                               USBREDIR_SERVER_REPLY_CONFIG);
}

static void usbredir_server_get_configuration(void *priv, uint64_t id)
{
    struct usb_redir_control_packet_header ctrl = {
        .endpoint = 0,
        .request = USB_REQ_GET_CONFIGURATION,
        /* Device->Host, Standard, Device */
        .requesttype = 0x80,
        .status = 0,
        .value = 0,
        .index = 0,
        .length = 1,
    };
    USBRedirServer *s = priv;

    /* The host gets a configuration_status when the device answers. */
    usbredir_server_do_control(s, id, &ctrl, NULL, 0,
                               USBREDIR_SERVER_REPLY_CONFIG);
}

static void usbredir_server_set_alt_setting(void *priv, uint64_t id,
    struct usb_redir_set_alt_setting_header *hdr)
{
    struct usb_redir_control_packet_header ctrl = {
        .endpoint = 0,
        .request = USB_REQ_SET_INTERFACE,
        /* Host->Device, Standard, Interface */
        .requesttype = 0x01,
        .status = 0,
        .value = hdr->alt,
        .index = hdr->interface,
        .length = 0,
    };
    USBRedirServer *s = priv;

    /* The host gets an alt_setting_status when the device answers. */
    usbredir_server_do_control(s, id, &ctrl, NULL, 0,
                               USBREDIR_SERVER_REPLY_ALT);
}

static void usbredir_server_get_alt_setting(void *priv, uint64_t id,
    struct usb_redir_get_alt_setting_header *hdr)
{
    struct usb_redir_control_packet_header ctrl = {
        .endpoint = 0,
        .request = USB_REQ_GET_INTERFACE,
        /* Device->Host, Standard, Interface */
        .requesttype = 0x81,
        .status = 0,
        .value = 0,
        .index = hdr->interface,
        .length = 1,
    };
    USBRedirServer *s = priv;

    /* The host gets an alt_setting_status when the device answers. */
    usbredir_server_do_control(s, id, &ctrl, NULL, 0,
                               USBREDIR_SERVER_REPLY_ALT);
}

static void usbredir_server_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_bulk_packet_header *hdr,
    uint8_t *data, int data_len)
{
    USBRedirServer *s = priv;
    USBDevice *device = usbredir_server_device(s);
    bool is_in = !!(hdr->endpoint & USB_DIR_IN);
    int pid = is_in ? USB_TOKEN_IN : USB_TOKEN_OUT;
    struct usb_redir_bulk_packet_header resp;
    int ep_nr = hdr->endpoint & 0x0f;
    USBRedirServerPkt *rp;
    USBEndpoint *ep;
    uint32_t len;

    if (!s->host_connected || !device || !device->attached) {
        return;
    }

    /* IN: what the host asked for. OUT: what the host sent. */
    len = is_in ? (((uint32_t)hdr->length_high << 16) | hdr->length)
                : (uint32_t)data_len;

    /*
     * Too big. Tell the host the transfer failed.
     * Do not send back less data instead. The host would see the
     * missing bytes as an error and reset the device.
     */
    if (len > USBREDIR_SERVER_MAX_BULK) {
        resp = *hdr;
        resp.status = usb_redir_inval;
        resp.length = 0;
        resp.length_high = 0;

        trace_usbredir_server_bulk_too_big(id, hdr->endpoint, len);
        usbredirparser_send_bulk_packet(s->parser, id, &resp, NULL, 0);
        usbredirparser_do_write(s->parser);
        return;
    }

    ep = usb_ep_get(device, pid, ep_nr);
    rp = usbredir_server_pkt_alloc(len);
    rp->redir_id = id;
    rp->type = USBREDIR_SERVER_BULK;
    rp->bulk_hdr = *hdr;

    usb_packet_setup(&rp->pkt, pid, ep, 0, s->next_id++, false, false);

    /* OUT data comes from the host. IN data is written by the device. */
    if (!is_in && len > 0) {
        memcpy(rp->data, data, len);
    }
    usb_packet_addbuf(&rp->pkt, rp->data, len);

    trace_usbredir_server_bulk(id, hdr->endpoint, len);
    usbredir_server_submit_to_device(s, rp);
}

/*
 * The host sends an interrupt_packet. It is a request: interrupt OUT
 * data to write, or a single IN request to answer.
 */
static void usbredir_server_interrupt_packet(void *priv, uint64_t id,
    struct usb_redir_interrupt_packet_header *hdr,
    uint8_t *data, int data_len)
{
    USBRedirServer *s = priv;
    USBDevice *device = usbredir_server_device(s);
    bool is_in = !!(hdr->endpoint & USB_DIR_IN);
    int pid = is_in ? USB_TOKEN_IN : USB_TOKEN_OUT;
    int ep_nr = hdr->endpoint & 0x0f;
    USBRedirServerPkt *rp;
    USBEndpoint *ep;
    int len;

    if (!s->host_connected || !device || !device->attached) {
        return;
    }

    /* IN: what the host asked for. OUT: what the host sent. */
    len = is_in ? hdr->length : data_len;

    ep = usb_ep_get(device, pid, ep_nr);
    rp = usbredir_server_pkt_alloc(len);
    rp->redir_id = id;
    rp->type = USBREDIR_SERVER_INTR;
    rp->intr_hdr = *hdr;

    usb_packet_setup(&rp->pkt, pid, ep, 0, s->next_id++, false, false);

    /* OUT data comes from the host. IN data is written by the device. */
    if (!is_in && len > 0) {
        memcpy(rp->data, data, len);
    }
    usb_packet_addbuf(&rp->pkt, rp->data, len);

    trace_usbredir_server_interrupt(id, hdr->endpoint, len);
    usbredir_server_submit_to_device(s, rp);
}

static void usbredir_server_filter_reject(void *priv)
{
    trace_usbredir_server_filter_reject();
}

static void usbredir_server_filter_filter(void *priv,
    struct usbredirfilter_rule *rules, int rules_count)
{
    /* We accept any host. The callback owns the rules, so free them. */
    free(rules);
}

static void usbredir_server_device_disconnect_ack(void *priv)
{
    /* The host saw our device_disconnect. Nothing to do. */
}

static void usbredir_server_interface_info(void *priv,
    struct usb_redir_interface_info_header *hdr)
{
    /* The host should not send this to a device. Nothing to do. */
}

static void usbredir_server_alloc_bulk_streams(void *priv, uint64_t id,
    struct usb_redir_alloc_bulk_streams_header *hdr)
{
    /* We do not advertise bulk streams. Nothing to do. */
}

static void usbredir_server_start_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_start_bulk_receiving_header *hdr)
{
    /* We do not advertise this. Nothing to do. */
}

static void usbredir_server_stop_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_bulk_receiving_header *hdr)
{
    /* We do not advertise this. Nothing to do. */
}

static void usbredir_server_cancel_data_packet(void *priv, uint64_t id)
{
    struct usb_redir_control_packet_header resp = {
        .endpoint = 0,
        .status = usb_redir_cancelled,
        .length = 0,
    };
    USBRedirServer *s = priv;
    USBRedirServerPkt *rp;

    /*
     * The host has put this id in its cancelled queue and waits for one
     * answer carrying it. The device may have answered already, so the
     * request may no longer be on our list. Answer in both cases: the
     * host reuses ids, and a leftover entry would eat a later answer.
     */
    QTAILQ_FOREACH(rp, &s->inflight, next) {
        if (rp->redir_id == id) {
            trace_usbredir_server_cancel(id, true);
            usbredir_server_send_cancelled(s, rp);
            usbredir_server_drop_pkt(s, rp);
            return;
        }
    }

    /*
     * Already finished, so we no longer know what kind of transfer it
     * was. The host retires the entry on the id alone, and its control
     * handler ignores the endpoint field, so a control packet always
     * works.
     */
    trace_usbredir_server_cancel(id, false);
    usbredirparser_send_control_packet(s->parser, id, &resp, NULL, 0);
    usbredirparser_do_write(s->parser);
}

/*
 * Cancelled and in-flight packets
 */

/*
 * Every request must get one answer carrying its id, even an aborted one.
 * A dropped id stays in the host's cancelled queue. The host reuses ids,
 * so a later answer would be thrown away as a stale one.
 */
static void usbredir_server_send_cancelled(USBRedirServer *s,
                                           USBRedirServerPkt *rp)
{
    struct usb_redir_interrupt_packet_header intr;
    struct usb_redir_control_packet_header ctrl;
    struct usb_redir_bulk_packet_header bulk;

    switch (rp->type) {
    case USBREDIR_SERVER_CTRL_SETUP:
    case USBREDIR_SERVER_CTRL_STATUS:
        ctrl = rp->ctrl_hdr;
        ctrl.status = usb_redir_cancelled;
        ctrl.length = 0;
        usbredirparser_send_control_packet(s->parser, rp->redir_id,
                                           &ctrl, NULL, 0);
        break;
    case USBREDIR_SERVER_BULK:
        bulk = rp->bulk_hdr;
        bulk.status = usb_redir_cancelled;
        bulk.length = 0;
        bulk.length_high = 0;
        usbredirparser_send_bulk_packet(s->parser, rp->redir_id,
                                        &bulk, NULL, 0);
        break;
    case USBREDIR_SERVER_INTR:
        intr = rp->intr_hdr;
        intr.status = usb_redir_cancelled;
        intr.length = 0;
        usbredirparser_send_interrupt_packet(s->parser, rp->redir_id,
                                             &intr, NULL, 0);
        break;
    default:
        return;
    }
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_stop_transfers(USBRedirServer *s)
{
    USBRedirServerPkt *rp;

    /*
     * No "cancelled" response here. This runs on a bus reset, a detach or
     * a closed chardev, and the host has dropped its own queues already.
     */
    while ((rp = QTAILQ_FIRST(&s->inflight)) != NULL) {
        usbredir_server_drop_pkt(s, rp);
    }
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
    s->parser->reset_func = usbredir_server_reset;
    s->parser->control_packet_func = usbredir_server_control_packet;
    s->parser->bulk_packet_func = usbredir_server_bulk_packet;
    s->parser->interrupt_packet_func = usbredir_server_interrupt_packet;
    s->parser->set_configuration_func = usbredir_server_set_configuration;

    /* The parser calls these directly, so they must not be NULL. */
    s->parser->get_configuration_func = usbredir_server_get_configuration;
    s->parser->set_alt_setting_func = usbredir_server_set_alt_setting;
    s->parser->get_alt_setting_func = usbredir_server_get_alt_setting;
    s->parser->filter_reject_func = usbredir_server_filter_reject;
    s->parser->filter_filter_func = usbredir_server_filter_filter;
    s->parser->device_disconnect_ack_func =
        usbredir_server_device_disconnect_ack;
    s->parser->interface_info_func = usbredir_server_interface_info;
    s->parser->alloc_bulk_streams_func = usbredir_server_alloc_bulk_streams;
    s->parser->cancel_data_packet_func = usbredir_server_cancel_data_packet;
    s->parser->start_bulk_receiving_func =
        usbredir_server_start_bulk_receiving;
    s->parser->stop_bulk_receiving_func =
        usbredir_server_stop_bulk_receiving;

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
    s->device_announced = false;

    /* The announce timer may still be pending. */
    timer_del(s->announce_timer);
    g_clear_handle_id(&s->watch, g_source_remove);

    usbredir_server_stop_transfers(s);

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

    QTAILQ_INIT(&s->inflight);
    memset(s->ep_type, USB_ENDPOINT_XFER_INVALID, sizeof(s->ep_type));

    /* One port: usbredir carries a single device. */
    usb_bus_new(&s->bus, sizeof(s->bus), &usbredir_server_bus_ops, dev);
    s->bus.no_auto_hub = true;
    usb_register_port(&s->bus, &s->port, s, 0, &usbredir_server_port_ops,
                      USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                      USB_SPEED_MASK_HIGH);

    s->announce_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                     usbredir_server_do_announce, s);
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

    timer_free(s->announce_timer);

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
