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
 *     x-usb-redir-server (the server, this file)
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
 * run one x-usb-redir-server per device, each with its own chardev.
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
#include "hw/core/sysbus.h"
#include "hw/usb/usb.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#include <usbredirparser.h>
#include <usbredirproto.h>

#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "desc.h"
#include "trace.h"

#define TYPE_USB_REDIR_SERVER "x-usb-redir-server"
OBJECT_DECLARE_SIMPLE_TYPE(USBRedirServer, USB_REDIR_SERVER)

#define USBREDIR_SERVER_VERSION "qemu " TYPE_USB_REDIR_SERVER " " QEMU_VERSION

/*
 * The usbredir ep_info message has one slot per endpoint and direction.
 * Index 0-15 are the OUT endpoints, 16-31 the IN ones.
 */
#define USBREDIR_SERVER_MAX_EP 32
#define USBREDIR_SERVER_EP_IN_BASE 16

/*
 * Ask again this often after an endpoint answered NAK. Most devices
 * wake the endpoint when data arrives, and then we ask at once. This
 * timer is for the devices that do not.
 */
#define USBREDIR_SERVER_RETRY_MS 1000

/*
 * The bulk length field is 32 bits, so the host can ask for up to 4 GB.
 * This is the largest transfer accepted.
 */
#define USBREDIR_SERVER_MAX_BULK (1 * MiB)

/* Buffer size for an interrupt IN endpoint before its descriptor is seen. */
#define USBREDIR_SERVER_INTR_STREAM_DEFAULT_LEN 64

#define USBREDIR_SERVER_CTRL 0
#define USBREDIR_SERVER_BULK 1
#define USBREDIR_SERVER_INTERRUPT 2
#define USBREDIR_SERVER_INTR_STREAM 3

/* Which message answers the host when a control transfer ends. */
typedef enum {
    USBREDIR_SERVER_REPLY_CONTROL,
    USBREDIR_SERVER_REPLY_CONFIG,
    USBREDIR_SERVER_REPLY_ALT,
} USBRedirServerReply;

/* What the device said when usbredir_server_ask_device() asked it. */
typedef enum {
    /* It took the request and will call back. */
    USBREDIR_SERVER_ASK_ASYNC,
    /* It has nothing now, so the request went back on the list. */
    USBREDIR_SERVER_ASK_NAK,
    /* It answered, and the request is freed. */
    USBREDIR_SERVER_ASK_COMPLETE,
} USBRedirServerAsk;

typedef struct USBRedirServerPkt {
    USBPacket pkt;

    /* Kept for the usbredir response. */
    struct usb_redir_interrupt_packet_header intr_hdr;
    struct usb_redir_control_packet_header ctrl_hdr;
    struct usb_redir_bulk_packet_header bulk_hdr;

    /*
     * The IOV points here. IN data lands in it, OUT data is copied in.
     * Allocated and freed with the packet.
     */
    uint8_t *data;

    /* Every request is on this list. */
    QTAILQ_ENTRY(USBRedirServerPkt) inflight_link;

    /* Only a request that must wait is on this list. */
    QTAILQ_ENTRY(USBRedirServerPkt) waiting_link;

    USBRedirServerReply reply;
    uint64_t redir_id;
    int ep_idx;
    int type;
} USBRedirServerPkt;

typedef struct USBRedirServerEp {
    USBRedirServer *s;

    /*
     * Requests the device has not taken, oldest first. A new request
     * goes on the end. We always ask about the first one, and a NAK
     * puts it back on the front, so the order never changes.
     */
    QTAILQ_HEAD(, USBRedirServerPkt) waiting;

    /* Asks again for a device that does not wake the endpoint. */
    QEMUTimer *retry;

    /*
     * Interrupt IN streaming. The timer asks the device again at the
     * endpoint's own rate, and more slowly after a NAK or an error.
     * The bottom half asks at once when the device says it has data.
     */
    QEMUTimer *stream_timer;
    QEMUBH *stream_bh;
    bool streaming;

    uint16_t max_packet;
    uint8_t interval;
    uint8_t iface;
    uint8_t type;
    uint8_t idx;
} USBRedirServerEp;

struct USBRedirServer {
    SysBusDevice parent_obj;

    /* USB bus */
    USBBus bus;
    USBPort port;

    /* Properties */
    CharFrontend cs;

    /* usbredir over the chardev */
    struct usbredirparser *parser;
    QEMUBH *chardev_close_bh;
    const uint8_t *read_buf;
    int read_buf_size;
    bool in_write;
    guint watch;
    bool host_connected;
    bool device_announced;

    /* Every request the host is still waiting for an answer to. */
    QTAILQ_HEAD(, USBRedirServerPkt) inflight;
    uint64_t next_id;

    USBRedirServerEp ep[USBREDIR_SERVER_MAX_EP];
    uint8_t alt_setting[USB_MAX_INTERFACES];
    GHashTable *config_descs;
    uint8_t config_value;
};

static void usbredir_server_pkt_free(USBRedirServer *s, USBRedirServerPkt *rp);
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

static int usbredir_server_ep_index(uint8_t addr)
{
    int ep_nr = addr & 0x0f;

    return (addr & USB_DIR_IN) ? ep_nr + USBREDIR_SERVER_EP_IN_BASE : ep_nr;
}

static void usbredir_server_clear_ep_table(USBRedirServer *s)
{
    int i;

    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        s->ep[i].max_packet = 0;
        s->ep[i].interval = 0;
        s->ep[i].iface = 0;
        s->ep[i].type = USB_ENDPOINT_XFER_INVALID;
    }
}

static uint8_t usbredir_server_ep_status(USBRedirServer *s, uint8_t addr,
                                         uint8_t want_type)
{
    bool is_ctrl = want_type == USB_ENDPOINT_XFER_CONTROL;
    USBDevice *device = usbredir_server_device(s);
    int idx = usbredir_server_ep_index(addr);
    int ep_nr = addr & 0x0f;

    if (!s->host_connected || !device || !device->attached) {
        return usb_redir_ioerror;
    }

    /* Endpoint 0 carries control transfers, and nothing else. */
    if (ep_nr == 0 && !is_ctrl) {
        return usb_redir_inval;
    }

    /* An absent endpoint has type INVALID, which never matches. */
    if (!is_ctrl && s->ep[idx].type != want_type) {
        return usb_redir_inval;
    }

    return usb_redir_success;
}

static void usbredir_server_record_endpoint(USBRedirServer *s,
                                            const USBDescriptor *desc,
                                            uint8_t iface)
{
    uint8_t addr;
    int ep_nr;
    int idx;

    addr = desc->u.endpoint.bEndpointAddress;
    ep_nr = addr & 0x0f;
    idx = usbredir_server_ep_index(addr);
    if (ep_nr < 1 || idx >= USBREDIR_SERVER_MAX_EP) {
        trace_usbredir_server_endpoint_dropped(addr);
        return;
    }

    s->ep[idx].type = desc->u.endpoint.bmAttributes & 0x03;
    s->ep[idx].max_packet = (desc->u.endpoint.wMaxPacketSize_hi << 8) |
                            desc->u.endpoint.wMaxPacketSize_lo;
    s->ep[idx].interval = desc->u.endpoint.bInterval;
    s->ep[idx].iface = iface;

    /*
     * bInterval says how often to poll this endpoint. Pass on what the
     * descriptor said, but never 0 for an interrupt or isochronous
     * endpoint: redirect.c throws the whole device away when it sees 0,
     * so store 1 instead.
     */
    if (s->ep[idx].interval == 0 &&
        (s->ep[idx].type == USB_ENDPOINT_XFER_INT ||
         s->ep[idx].type == USB_ENDPOINT_XFER_ISOC)) {
        s->ep[idx].interval = 1;
    }
}

static void usbredir_server_save_config_desc(USBRedirServer *s,
                                             const uint8_t *data, int len)
{
    const USBDescriptor *conf = (const USBDescriptor *)data;
    uint16_t total;

    if (len < 4) {
        return;
    }
    total = lduw_le_p(data + 2);

    /*
     * The host reads this twice. The first read is a few bytes, just to
     * learn the length from wTotalLength. The second read has the whole
     * descriptor.
     *
     * A configuration descriptor is 9 bytes, so a shorter one is broken.
     */
    if (total < 9 || len < total) {
        return;
    }

    g_hash_table_insert(s->config_descs,
                        GUINT_TO_POINTER(conf->u.config.bConfigurationValue),
                        g_memdup2(data, total));
}

/*
 * Some devices only forward transfers. They never read their own
 * descriptors, so the USB core leaves their endpoint types at
 * USB_ENDPOINT_XFER_INVALID. Sending that in ep_info does not work: the
 * host sees type 255 and will not move data. So read the configuration
 * descriptor and fill the tables here.
 *
 * Only what the host selected counts. A device can offer several
 * configurations, and an interface several alternate settings. The
 * endpoints of the ones not chosen are not there.
 */
static void usbredir_server_build_ep_table(USBRedirServer *s)
{
    const USBDescriptor *desc;
    const uint8_t *config;
    bool active = false;
    uint8_t iface = 0;
    uint8_t alt;
    int i = 0;
    int total;

    usbredir_server_clear_ep_table(s);

    config = g_hash_table_lookup(s->config_descs,
                                 GUINT_TO_POINTER(s->config_value));
    if (!config) {
        trace_usbredir_server_config_not_found(
            s->config_value, g_hash_table_size(s->config_descs));
        return;
    }
    total = lduw_le_p(config + 2);

    while (i + 2 <= total) {
        desc = (const USBDescriptor *)(config + i);

        if (desc->bLength < 2) {
            trace_usbredir_server_bad_descriptor(i, desc->bLength,
                                                 desc->bDescriptorType);
            break;
        }
        if (i + desc->bLength > total) {
            trace_usbredir_server_descriptor_overrun(i, desc->bLength, total);
            break;
        }

        switch (desc->bDescriptorType) {
        case USB_DT_INTERFACE:
            if (desc->bLength < 9) {
                trace_usbredir_server_bad_descriptor(i, desc->bLength,
                                                     desc->bDescriptorType);
                break;
            }
            iface = desc->u.interface.bInterfaceNumber;
            alt = desc->u.interface.bAlternateSetting;
            active = iface < USB_MAX_INTERFACES &&
                     alt == s->alt_setting[iface];
            break;
        case USB_DT_ENDPOINT:
            if (desc->bLength < 7) {
                trace_usbredir_server_bad_descriptor(i, desc->bLength,
                                                     desc->bDescriptorType);
                break;
            }
            if (active) {
                usbredir_server_record_endpoint(s, desc, iface);
            }
            break;
        }

        i += desc->bLength;
    }
}

static void usbredir_server_send_ep_info(USBRedirServer *s)
{
    struct usb_redir_ep_info_header ep_info = {};
    int i;

    usbredir_server_build_ep_table(s);

    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        ep_info.type[i] = s->ep[i].type;
        ep_info.max_packet_size[i] = s->ep[i].max_packet;
        ep_info.interface[i] = s->ep[i].iface;
        ep_info.interval[i] = s->ep[i].interval;
        if (s->ep[i].type != USB_ENDPOINT_XFER_INVALID) {
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
    struct usb_redir_device_connect_header conn = {};

    if (!s->host_connected || s->device_announced ||
        !device || !device->attached) {
        return;
    }
    s->device_announced = true;
    conn.speed = usbredir_server_speed(device);

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

static void usbredir_server_ctrl_reply(USBRedirServer *s, uint64_t id,
    const struct usb_redir_control_packet_header *hdr,
    USBRedirServerReply reply, uint8_t status, uint8_t *data, int actual)
{
    struct usb_redir_configuration_status_header cfg = {};
    struct usb_redir_control_packet_header resp = *hdr;
    struct usb_redir_alt_setting_status_header alt = {
        .interface = hdr->index,
    };
    bool is_in = !!(hdr->requesttype & USB_DIR_IN);
    uint8_t value = hdr->value;

    /* IN reports what the device sent, OUT echoes what the host chose. */
    if (is_in) {
        value = actual > 0 ? data[0] : 0;
    }

    switch (reply) {
    case USBREDIR_SERVER_REPLY_CONTROL:
        resp.status = status;
        resp.length = actual;
        usbredirparser_send_control_packet(s->parser, id, &resp,
                                           actual > 0 ? data : NULL, actual);
        break;
    case USBREDIR_SERVER_REPLY_CONFIG:
        cfg.status = status;
        cfg.configuration = value;
        usbredirparser_send_configuration_status(s->parser, id, &cfg);
        break;
    case USBREDIR_SERVER_REPLY_ALT:
        alt.status = status;
        alt.alt = value;
        usbredirparser_send_alt_setting_status(s->parser, id, &alt);
        break;
    }
    usbredirparser_do_write(s->parser);
}

/*
 * The device answered the control transfer. Some requests change the
 * endpoint list, so save what the host asked for.
 */
static void usbredir_server_ctrl_complete(USBRedirServer *s,
                                          USBRedirServerPkt *rp)
{
    uint8_t status = usbredir_server_status(rp->pkt.status);
    bool is_in = !!(rp->ctrl_hdr.requesttype & USB_DIR_IN);
    int actual = is_in ? rp->pkt.actual_length : 0;

    trace_usbredir_server_ctrl_complete(rp->redir_id, status, actual);
    usbredir_server_ctrl_reply(s, rp->redir_id, &rp->ctrl_hdr, rp->reply,
                               status, rp->data, actual);

    if (status != usb_redir_success) {
        return;
    }

    switch ((rp->ctrl_hdr.requesttype << 8) | rp->ctrl_hdr.request) {
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        /* Keep the configuration descriptor. It says what endpoints exist. */
        if ((rp->ctrl_hdr.value >> 8) == USB_DT_CONFIG && actual > 0) {
            usbredir_server_save_config_desc(s, rp->data, actual);
        }
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        /* A new configuration puts every interface back on alternate 0. */
        s->config_value = rp->ctrl_hdr.value;
        memset(s->alt_setting, 0, sizeof(s->alt_setting));
        usbredir_server_send_ep_info(s);
        break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        if (rp->ctrl_hdr.index >= USB_MAX_INTERFACES) {
            return;
        }
        s->alt_setting[rp->ctrl_hdr.index] = rp->ctrl_hdr.value;
        usbredir_server_send_ep_info(s);
        break;
    default:
        break;
    }
}

static void usbredir_server_bulk_reply(USBRedirServer *s, uint64_t id,
    const struct usb_redir_bulk_packet_header *hdr, uint8_t status,
    uint8_t *data, int len)
{
    struct usb_redir_bulk_packet_header resp = *hdr;

    resp.status = status;
    resp.length = len;
    resp.length_high = len >> 16;

    usbredirparser_send_bulk_packet(s->parser, id, &resp,
                                    len > 0 ? data : NULL, len);
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_bulk_complete(USBRedirServer *s,
                                          USBRedirServerPkt *rp)
{
    uint8_t status = usbredir_server_status(rp->pkt.status);
    bool is_in = !!(rp->bulk_hdr.endpoint & USB_DIR_IN);
    int actual = rp->pkt.actual_length;
    /* Only an IN transfer carries data back. */
    int len = is_in ? actual : 0;

    trace_usbredir_server_bulk_complete(rp->redir_id, rp->bulk_hdr.endpoint,
                                        status, actual);
    usbredir_server_bulk_reply(s, rp->redir_id, &rp->bulk_hdr, status,
                               rp->data, len);
}

static void usbredir_server_interrupt_reply(USBRedirServer *s, uint64_t id,
    const struct usb_redir_interrupt_packet_header *hdr, uint8_t status,
    uint8_t *data, int len)
{
    struct usb_redir_interrupt_packet_header resp = *hdr;

    resp.status = status;
    resp.length = len;

    usbredirparser_send_interrupt_packet(s->parser, id, &resp,
                                         len > 0 ? data : NULL, len);
    usbredirparser_do_write(s->parser);
}

static void usbredir_server_interrupt_complete(USBRedirServer *s,
                                               USBRedirServerPkt *rp)
{
    uint8_t status = usbredir_server_status(rp->pkt.status);
    bool is_in = !!(rp->intr_hdr.endpoint & USB_DIR_IN);
    int actual = rp->pkt.actual_length;
    /* Only an IN transfer carries data back. */
    int len = is_in ? actual : 0;

    trace_usbredir_server_interrupt_complete(rp->redir_id,
                                             rp->intr_hdr.endpoint,
                                             status, actual);
    usbredir_server_interrupt_reply(s, rp->redir_id, &rp->intr_hdr, status,
                                    rp->data, len);
}

/*
 * How long to wait before asking the device again. bInterval means
 * different things at different speeds: a number of frames at low and
 * full speed, an exponent at high speed.
 */
static int64_t usbredir_server_intr_stream_period_ns(USBRedirServer *s,
                                                     int ep_nr)
{
    int idx = usbredir_server_ep_index(ep_nr | USB_DIR_IN);
    USBDevice *device = usbredir_server_device(s);
    uint8_t interval = s->ep[idx].interval;

    if (interval == 0) {
        interval = 1;
    }

    if (device && device->speed == USB_SPEED_HIGH) {
        interval = MIN(interval, 16);
        return (1LL << (interval - 1)) * 125 * SCALE_US;
    }

    return (int64_t)interval * SCALE_MS;
}

/*
 * The device answered a streamed request. Send the answer to the host, then
 * ask again, because the host still wants more. Do not ask from here: the
 * device may answer at once, and this function would call itself over and
 * over. Arm the timer instead. It also waits for the endpoint's bInterval.
 */
static void usbredir_server_intr_stream_complete(USBRedirServer *s,
                                                 USBRedirServerPkt *rp)
{
    struct usb_redir_interrupt_packet_header resp = rp->intr_hdr;
    struct usb_redir_interrupt_receiving_status_header st;
    int idx = usbredir_server_ep_index(resp.endpoint);
    int ep_nr = resp.endpoint & 0x0f;
    USBRedirServerEp *ep = &s->ep[idx];
    USBPacket *p = &rp->pkt;
    int actual = p->actual_length;
    bool retry = false;
    int64_t delay_ns;

    trace_usbredir_server_intr_stream_complete(ep_nr, p->status, actual);

    switch (p->status) {
    case USB_RET_SUCCESS:
        resp.status = usb_redir_success;
        resp.length = actual;
        usbredirparser_send_interrupt_packet(s->parser, 0, &resp,
                                             actual ? rp->data : NULL,
                                             actual);
        usbredirparser_do_write(s->parser);
        break;
    case USB_RET_NAK:
        /* no data yet; wait, unless the device wakes us first */
        retry = true;
        break;
    default:
        /*
         * The endpoint failed. There is no data, so send a status
         * message instead. The host gives it to its guest.
         *
         * redirect.c ends the stream only on a stall, so do the same
         * here. If we end it on any other error, the host still
         * thinks the stream runs, and it never asks again.
         */
        st.endpoint = resp.endpoint;
        st.status = usbredir_server_status(p->status);
        usbredirparser_send_interrupt_receiving_status(s->parser, 0, &st);
        usbredirparser_do_write(s->parser);
        if (p->status == USB_RET_STALL) {
            ep->streaming = false;
        } else {
            /* the stream stays up, but do not ask at the endpoint's rate */
            retry = true;
        }
        break;
    }

    if (ep->streaming) {
        if (retry) {
            delay_ns = (int64_t)USBREDIR_SERVER_RETRY_MS * SCALE_MS;
        } else {
            delay_ns = usbredir_server_intr_stream_period_ns(s, ep_nr);
        }
        timer_mod(ep->stream_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns);
    }
}

/*
 * USB port ops
 */

static void usbredir_server_port_attach(USBPort *port)
{
    USBRedirServer *s = port->opaque;

    trace_usbredir_server_attach();
    usbredir_server_announce_device(s);
}

static void usbredir_server_port_detach(USBPort *port)
{
    USBRedirServer *s = port->opaque;

    trace_usbredir_server_detach(s->device_announced);

    usbredir_server_stop_transfers(s);

    if (s->host_connected && s->parser && s->device_announced) {
        trace_usbredir_server_disconnect();
        usbredirparser_send_device_disconnect(s->parser);
        usbredirparser_do_write(s->parser);
    }
    s->device_announced = false;

    /* Clear the endpoint table: it belongs to the device that is leaving. */
    usbredir_server_clear_ep_table(s);
    memset(s->alt_setting, 0, sizeof(s->alt_setting));
    g_hash_table_remove_all(s->config_descs);
    s->config_value = 0;
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
 * USB_RET_ASYNC. usbredir_server_ask_device() calls it for the rest.
 */
static void usbredir_server_packet_complete(USBPort *port, USBPacket *p)
{
    USBRedirServer *s = port->opaque;
    USBRedirServerPkt *rp = container_of(p, USBRedirServerPkt, pkt);
    USBDevice *device = usbredir_server_device(s);

    /* A cancel can arrive while we answer, so it must not find this one. */
    QTAILQ_REMOVE(&s->inflight, rp, inflight_link);

    if (!s->parser || !device) {
        usbredir_server_pkt_free(s, rp);
        return;
    }

    switch (rp->type) {
    case USBREDIR_SERVER_CTRL:
        usbredir_server_ctrl_complete(s, rp);
        break;
    case USBREDIR_SERVER_BULK:
        usbredir_server_bulk_complete(s, rp);
        break;
    case USBREDIR_SERVER_INTERRUPT:
        usbredir_server_interrupt_complete(s, rp);
        break;
    case USBREDIR_SERVER_INTR_STREAM:
        usbredir_server_intr_stream_complete(s, rp);
        break;
    default:
        g_assert_not_reached();
    }

    usbredir_server_pkt_free(s, rp);
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

/*
 * Hand one request to the device. USB_RET_ASYNC means it answers
 * later, and the core then calls usbredir_server_packet_complete()
 * for us. A NAK means it has nothing yet, so keep the request.
 * Anything else is the answer, and the core calls nothing, so we call
 * usbredir_server_packet_complete() ourselves.
 */
static USBRedirServerAsk usbredir_server_ask_device(USBRedirServer *s,
                                                    USBRedirServerPkt *rp)
{
    USBRedirServerEp *ep = &s->ep[rp->ep_idx];

    usb_handle_packet(usbredir_server_device(s), &rp->pkt);

    if (rp->pkt.status == USB_RET_ASYNC) {
        return USBREDIR_SERVER_ASK_ASYNC;
    }

    /*
     * Keep the request and send it again when the device wakes the
     * endpoint. A streamed request asks again by itself, with its own
     * timer.
     */
    if (rp->pkt.status == USB_RET_NAK &&
        rp->type != USBREDIR_SERVER_INTR_STREAM) {
        trace_usbredir_server_nak_park(rp->redir_id, rp->ep_idx);
        QTAILQ_INSERT_HEAD(&ep->waiting, rp, waiting_link);
        timer_mod(ep->retry, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (int64_t)USBREDIR_SERVER_RETRY_MS * SCALE_MS);
        return USBREDIR_SERVER_ASK_NAK;
    }

    usbredir_server_packet_complete(&s->port, &rp->pkt);
    return USBREDIR_SERVER_ASK_COMPLETE;
}

/*
 * Send everything this endpoint is holding, oldest first. The device
 * answering NAK stops it again, and the next wakeup or the timer picks
 * it up.
 */
static void usbredir_server_kick_ep(USBRedirServer *s, int idx)
{
    USBRedirServerEp *ep = &s->ep[idx];
    USBRedirServerPkt *rp;

    /* We are asking now, so drop the timer. A new NAK sets it again. */
    timer_del(ep->retry);

    while ((rp = QTAILQ_FIRST(&ep->waiting)) != NULL) {
        QTAILQ_REMOVE(&ep->waiting, rp, waiting_link);
        trace_usbredir_server_nak_retry(rp->redir_id, idx);
        if (usbredir_server_ask_device(s, rp) == USBREDIR_SERVER_ASK_NAK) {
            return;
        }
    }
}

static void usbredir_server_retry_ep(void *opaque)
{
    USBRedirServerEp *ep = opaque;

    usbredir_server_kick_ep(ep->s, ep->idx);
}

static void usbredir_server_wakeup_ep(USBBus *bus, USBEndpoint *ep,
                                      unsigned int stream)
{
    USBRedirServer *s = container_of(bus, USBRedirServer, bus);
    uint8_t addr = ep->nr | (ep->pid == USB_TOKEN_IN ? USB_DIR_IN : 0);
    int idx = usbredir_server_ep_index(addr);

    /*
     * The device has data. A streaming interrupt IN endpoint may have
     * answered NAK. Ask now instead of waiting for the retry timer.
     */
    if (s->ep[idx].streaming) {
        qemu_bh_schedule(s->ep[idx].stream_bh);
    }

    /* The host may also have a request stopped on this endpoint. */
    usbredir_server_kick_ep(s, idx);
}

static USBBusOps usbredir_server_bus_ops = {
    .wakeup_endpoint = usbredir_server_wakeup_ep,
};

/* An endpoint answers in order, so this waits behind anything else. */
static void usbredir_server_submit(USBRedirServer *s, USBRedirServerPkt *rp)
{
    USBRedirServerEp *ep = &s->ep[rp->ep_idx];

    QTAILQ_INSERT_TAIL(&s->inflight, rp, inflight_link);
    if (!QTAILQ_EMPTY(&ep->waiting)) {
        QTAILQ_INSERT_TAIL(&ep->waiting, rp, waiting_link);
        return;
    }
    usbredir_server_ask_device(s, rp);
}

static USBRedirServerPkt *usbredir_server_pkt_alloc(int size, int ep_idx)
{
    USBRedirServerPkt *rp = g_new0(USBRedirServerPkt, 1);

    qemu_iovec_init(&rp->pkt.iov, 1);
    rp->data = g_malloc0(size);
    rp->ep_idx = ep_idx;
    return rp;
}

/*
 * Take @rp off every list and free it. Do not use usb_packet_complete():
 * it calls back into usbredir_server_packet_complete(), which would free
 * the request a second time. usb_cancel_packet() only tells the device
 * to let go.
 */
static void usbredir_server_pkt_free(USBRedirServer *s, USBRedirServerPkt *rp)
{
    USBRedirServerEp *ep = &s->ep[rp->ep_idx];

    if (QTAILQ_IN_USE(rp, waiting_link)) {
        QTAILQ_REMOVE(&ep->waiting, rp, waiting_link);
    }
    if (QTAILQ_IN_USE(rp, inflight_link)) {
        QTAILQ_REMOVE(&s->inflight, rp, inflight_link);
    }
    if (usb_packet_is_inflight(&rp->pkt)) {
        usb_cancel_packet(&rp->pkt);
    }
    usb_packet_cleanup(&rp->pkt);
    g_free(rp->data);
    g_free(rp);
}

/*
 * Interrupt IN streaming
 */

static bool usbredir_server_intr_stream_inflight(USBRedirServer *s, int ep_nr)
{
    USBRedirServerPkt *rp;

    QTAILQ_FOREACH(rp, &s->inflight, inflight_link) {
        if (rp->type == USBREDIR_SERVER_INTR_STREAM &&
            (rp->intr_hdr.endpoint & 0x0f) == ep_nr) {
            return true;
        }
    }
    return false;
}

/*
 * Ask the device for data on @ep_nr and leave the request waiting. A device
 * sends data only when asked. Do nothing if a request on @ep_nr is still
 * unanswered.
 */
static void usbredir_server_intr_stream_park(USBRedirServer *s, int ep_nr)
{
    USBDevice *device = usbredir_server_device(s);
    int idx = usbredir_server_ep_index(ep_nr | USB_DIR_IN);
    USBRedirServerPkt *rp;
    USBEndpoint *ep;
    int len;

    /* No device to ask. */
    if (!device || !device->attached) {
        return;
    }

    /* The host did not ask for this endpoint, or a request is unanswered. */
    if (!s->ep[idx].streaming ||
        usbredir_server_intr_stream_inflight(s, ep_nr)) {
        return;
    }

    len = s->ep[idx].max_packet;
    if (len == 0) {
        len = USBREDIR_SERVER_INTR_STREAM_DEFAULT_LEN;
    }
    ep = usb_ep_get(device, USB_TOKEN_IN, ep_nr);
    rp = usbredir_server_pkt_alloc(len, idx);
    rp->type = USBREDIR_SERVER_INTR_STREAM;
    /* streamed data carries no host id */
    rp->redir_id = 0;
    rp->intr_hdr.endpoint = ep_nr | USB_DIR_IN;

    usb_packet_setup(&rp->pkt, USB_TOKEN_IN, ep, 0, s->next_id++,
                     false, false);
    usb_packet_addbuf(&rp->pkt, rp->data, len);

    /*
     * The timer is there for a device that does not wake the endpoint.
     * We are asking now, so drop it. The answer sets it again.
     */
    timer_del(s->ep[idx].stream_timer);

    trace_usbredir_server_intr_stream_park(ep_nr, len);
    usbredir_server_submit(s, rp);
}

/* BH and timer callback: ask again on one streaming endpoint. */
static void usbredir_server_intr_stream_kick(void *opaque)
{
    USBRedirServerEp *ep = opaque;
    int ep_nr = ep->idx - USBREDIR_SERVER_EP_IN_BASE;

    usbredir_server_intr_stream_park(ep->s, ep_nr);
}

static void usbredir_server_intr_stream_cancel(USBRedirServer *s, int ep_nr)
{
    USBRedirServerPkt *tmp;
    USBRedirServerPkt *rp;

    QTAILQ_FOREACH_SAFE(rp, &s->inflight, inflight_link, tmp) {
        if (rp->type != USBREDIR_SERVER_INTR_STREAM ||
            (rp->intr_hdr.endpoint & 0x0f) != ep_nr) {
            continue;
        }
        usbredir_server_pkt_free(s, rp);
    }
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
    usbredir_server_announce_device(s);
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
    USBEndpoint *ep0;
    uint8_t status;
    bool is_in;
    int size;

    trace_usbredir_server_control(id, hdr->requesttype, hdr->request,
                                  hdr->value, hdr->index, hdr->length);

    /* The host still waits for an answer, even when there is nobody to ask. */
    status = usbredir_server_ep_status(s, 0, USB_ENDPOINT_XFER_CONTROL);
    if (status != usb_redir_success) {
        trace_usbredir_server_ctrl_refused(id, status);
        usbredir_server_ctrl_reply(s, id, hdr, reply, status, NULL, 0);
        return;
    }

    is_in = !!(hdr->requesttype & USB_DIR_IN);
    ep0 = usb_ep_get(device, USB_TOKEN_SETUP, 0);

    /* Room for the data of either direction. */
    size = MAX(hdr->length, data_len);

    rp = usbredir_server_pkt_alloc(size, 0);
    rp->redir_id = id;
    rp->reply = reply;
    rp->ctrl_hdr = *hdr;
    rp->type = USBREDIR_SERVER_CTRL;
    if (!is_in && data_len > 0) {
        memcpy(rp->data, data, data_len);
    }

    usb_packet_setup(&rp->pkt, is_in ? USB_TOKEN_IN : USB_TOKEN_OUT, ep0,
                     0, s->next_id++, false, false);
    if (hdr->length > 0) {
        usb_packet_addbuf(&rp->pkt, rp->data, hdr->length);
    }

    rp->pkt.parameter = (uint64_t)hdr->requesttype |
                        (uint64_t)hdr->request << 8 |
                        (uint64_t)hdr->value << 16 |
                        (uint64_t)hdr->index << 32 |
                        (uint64_t)hdr->length << 48;

    usbredir_server_submit(s, rp);
}

static void usbredir_server_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *hdr,
    uint8_t *data, int data_len)
{
    USBRedirServer *s = priv;

    usbredir_server_do_control(s, id, hdr, data, data_len,
                               USBREDIR_SERVER_REPLY_CONTROL);
    usbredirparser_free_packet_data(s->parser, data);
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
    int idx = usbredir_server_ep_index(hdr->endpoint);
    USBDevice *device = usbredir_server_device(s);
    bool is_in = !!(hdr->endpoint & USB_DIR_IN);
    int pid = is_in ? USB_TOKEN_IN : USB_TOKEN_OUT;
    int ep_nr = hdr->endpoint & 0x0f;
    USBRedirServerPkt *rp;
    USBEndpoint *ep;
    uint8_t status;
    uint32_t len;

    /* The host still waits for an answer, even to a request we refuse. */
    status = usbredir_server_ep_status(s, hdr->endpoint,
                                       USB_ENDPOINT_XFER_BULK);
    if (status != usb_redir_success) {
        trace_usbredir_server_refused(id, hdr->endpoint, status,
                                      USB_ENDPOINT_XFER_BULK,
                                      s->ep[idx].type);
        usbredir_server_bulk_reply(s, id, hdr, status, NULL, 0);
        usbredirparser_free_packet_data(s->parser, data);
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
        trace_usbredir_server_bulk_too_big(id, hdr->endpoint, len);
        usbredir_server_bulk_reply(s, id, hdr, usb_redir_inval, NULL, 0);
        usbredirparser_free_packet_data(s->parser, data);
        return;
    }

    ep = usb_ep_get(device, pid, ep_nr);
    rp = usbredir_server_pkt_alloc(len, idx);
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
    usbredirparser_free_packet_data(s->parser, data);
    usbredir_server_submit(s, rp);
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
    int idx = usbredir_server_ep_index(hdr->endpoint);
    USBDevice *device = usbredir_server_device(s);
    bool is_in = !!(hdr->endpoint & USB_DIR_IN);
    int pid = is_in ? USB_TOKEN_IN : USB_TOKEN_OUT;
    int ep_nr = hdr->endpoint & 0x0f;
    USBRedirServerPkt *rp;
    USBEndpoint *ep;
    uint8_t status;
    int len;

    /* The host still waits for an answer, even to a request we refuse. */
    status = usbredir_server_ep_status(s, hdr->endpoint,
                                       USB_ENDPOINT_XFER_INT);
    if (status != usb_redir_success) {
        trace_usbredir_server_refused(id, hdr->endpoint, status,
                                      USB_ENDPOINT_XFER_INT,
                                      s->ep[idx].type);
        usbredir_server_interrupt_reply(s, id, hdr, status, NULL, 0);
        usbredirparser_free_packet_data(s->parser, data);
        return;
    }

    /* IN: what the host asked for. OUT: what the host sent. */
    len = is_in ? hdr->length : data_len;

    ep = usb_ep_get(device, pid, ep_nr);
    rp = usbredir_server_pkt_alloc(len, idx);
    rp->redir_id = id;
    rp->type = USBREDIR_SERVER_INTERRUPT;
    rp->intr_hdr = *hdr;

    usb_packet_setup(&rp->pkt, pid, ep, 0, s->next_id++, false, false);

    /* OUT data comes from the host. IN data is written by the device. */
    if (!is_in && len > 0) {
        memcpy(rp->data, data, len);
    }
    usb_packet_addbuf(&rp->pkt, rp->data, len);

    trace_usbredir_server_interrupt(id, hdr->endpoint, len);
    usbredirparser_free_packet_data(s->parser, data);
    usbredir_server_submit(s, rp);
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

/*
 * The host asks to stream an interrupt IN endpoint. Send the receiving
 * status first. Without that status the host throws the interrupt
 * packet away. Then ask the device once.
 */
static void usbredir_server_start_interrupt_receiving(void *priv,
    uint64_t id, struct usb_redir_start_interrupt_receiving_header *hdr)
{
    struct usb_redir_interrupt_receiving_status_header st = {
        .endpoint = hdr->endpoint,
        .status = usb_redir_success,
    };
    USBRedirServer *s = priv;
    int ep_nr = hdr->endpoint & 0x0f;

    st.status = usbredir_server_ep_status(s, hdr->endpoint,
                                          USB_ENDPOINT_XFER_INT);

    /* Only an interrupt IN endpoint can stream. */
    if (!(hdr->endpoint & USB_DIR_IN)) {
        st.status = usb_redir_inval;
    }

    if (st.status == usb_redir_success) {
        s->ep[usbredir_server_ep_index(hdr->endpoint)].streaming = true;
    }

    trace_usbredir_server_intr_stream_start(hdr->endpoint, st.status);
    usbredirparser_send_interrupt_receiving_status(s->parser, id, &st);
    usbredirparser_do_write(s->parser);

    if (st.status == usb_redir_success) {
        usbredir_server_intr_stream_park(s, ep_nr);
    }
}

static void usbredir_server_stop_interrupt_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_interrupt_receiving_header *hdr)
{
    int idx = usbredir_server_ep_index(hdr->endpoint);
    int ep_nr = hdr->endpoint & 0x0f;
    USBRedirServer *s = priv;

    trace_usbredir_server_intr_stream_stop(hdr->endpoint);

    /* Endpoint 0 is control, and an OUT endpoint never streams. */
    if (ep_nr == 0 || !(hdr->endpoint & USB_DIR_IN)) {
        return;
    }

    s->ep[idx].streaming = false;
    qemu_bh_cancel(s->ep[idx].stream_bh);
    timer_del(s->ep[idx].stream_timer);
    usbredir_server_intr_stream_cancel(s, ep_nr);
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
    USBRedirServer *s = priv;
    USBRedirServerPkt *rp;
    int idx;

    QTAILQ_FOREACH(rp, &s->inflight, inflight_link) {
        if (rp->redir_id == id) {
            idx = rp->ep_idx;

            trace_usbredir_server_cancel(id, true);
            usbredir_server_send_cancelled(s, rp);
            usbredir_server_pkt_free(s, rp);

            /* The rest were waiting behind this one, so send them now. */
            usbredir_server_kick_ep(s, idx);
            return;
        }
    }

    /*
     * Not on our list means we already answered it. The host uses the
     * id to find the request, and the same id is used again later. A
     * second answer could land on the wrong request. Send nothing.
     */
    trace_usbredir_server_cancel(id, false);
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
    case USBREDIR_SERVER_CTRL:
        ctrl = rp->ctrl_hdr;
        ctrl.status = usb_redir_cancelled;
        ctrl.length = 0;
        usbredirparser_send_control_packet(s->parser, rp->redir_id,
                                           &ctrl, NULL, 0);
        break;
    case USBREDIR_SERVER_BULK:
        bulk = rp->bulk_hdr;
        bulk.status = usb_redir_cancelled;
        bulk.length_high = 0;
        bulk.length = 0;
        usbredirparser_send_bulk_packet(s->parser, rp->redir_id,
                                        &bulk, NULL, 0);
        break;
    case USBREDIR_SERVER_INTERRUPT:
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
    int i;

    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        timer_del(s->ep[i].retry);
    }

    for (i = USBREDIR_SERVER_EP_IN_BASE + 1; i < USBREDIR_SERVER_MAX_EP; i++) {
        s->ep[i].streaming = false;
        qemu_bh_cancel(s->ep[i].stream_bh);
        timer_del(s->ep[i].stream_timer);
    }

    /*
     * No "cancelled" response here. This runs on a bus reset, a detach or
     * a closed chardev, and the host has dropped its own queues already.
     */
    while ((rp = QTAILQ_FIRST(&s->inflight)) != NULL) {
        usbredir_server_pkt_free(s, rp);
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
    s->parser->start_interrupt_receiving_func =
        usbredir_server_start_interrupt_receiving;
    s->parser->stop_interrupt_receiving_func =
        usbredir_server_stop_interrupt_receiving;
    s->parser->alloc_bulk_streams_func = usbredir_server_alloc_bulk_streams;
    s->parser->cancel_data_packet_func = usbredir_server_cancel_data_packet;
    s->parser->start_bulk_receiving_func =
        usbredir_server_start_bulk_receiving;
    s->parser->stop_bulk_receiving_func =
        usbredir_server_stop_bulk_receiving;

    /*
     * Capabilities: 64-bit IDs, connect_device_version, ep_info sizes
     * and 32-bit bulk lengths.
     */
    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_32bits_bulk_length);

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
    case CHR_EVENT_RESIZE:
        break;
    }
}

/*
 * Device registration
 */

static void usbredir_server_realize(DeviceState *dev, Error **errp)
{
    USBRedirServer *s = USB_REDIR_SERVER(dev);
    int i;

    if (!qemu_chr_fe_backend_connected(&s->cs)) {
        error_setg(errp,
                   TYPE_USB_REDIR_SERVER ": 'chardev' property must be set");
        return;
    }

    QTAILQ_INIT(&s->inflight);
    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        QTAILQ_INIT(&s->ep[i].waiting);
        s->ep[i].s = s;
        s->ep[i].idx = i;
        s->ep[i].retry = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      usbredir_server_retry_ep, &s->ep[i]);
    }

    /* Only an interrupt IN endpoint streams, so endpoint 0 and OUT are out. */
    for (i = USBREDIR_SERVER_EP_IN_BASE + 1; i < USBREDIR_SERVER_MAX_EP; i++) {
        s->ep[i].stream_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                             usbredir_server_intr_stream_kick,
                                             &s->ep[i]);
        s->ep[i].stream_bh =
            qemu_bh_new_guarded(usbredir_server_intr_stream_kick, &s->ep[i],
                                &dev->mem_reentrancy_guard);
    }
    s->config_descs = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                            NULL, g_free);
    usbredir_server_clear_ep_table(s);

    /* One port: usbredir carries a single device. */
    usb_bus_new(&s->bus, sizeof(s->bus), &usbredir_server_bus_ops, dev);
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
    int i;

    qemu_chr_fe_deinit(&s->cs, true);
    usbredir_server_destroy_parser(s);

    g_clear_pointer(&s->config_descs, g_hash_table_destroy);

    if (s->chardev_close_bh) {
        qemu_bh_delete(s->chardev_close_bh);
        s->chardev_close_bh = NULL;
    }

    for (i = 0; i < USBREDIR_SERVER_MAX_EP; i++) {
        timer_free(s->ep[i].retry);
        s->ep[i].retry = NULL;
    }

    for (i = USBREDIR_SERVER_EP_IN_BASE + 1; i < USBREDIR_SERVER_MAX_EP; i++) {
        timer_free(s->ep[i].stream_timer);
        s->ep[i].stream_timer = NULL;
        qemu_bh_delete(s->ep[i].stream_bh);
        s->ep[i].stream_bh = NULL;
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
