/*
 * USB redirector, server side
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_REDIRECT_SERVER_H
#define HW_USB_REDIRECT_SERVER_H

#include "qemu/units.h"
#include "hw/core/sysbus.h"
#include "hw/usb/usb.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#include <usbredirparser.h>
#include <usbredirproto.h>

#define TYPE_USB_REDIR_SERVER "usb-redir-server"
OBJECT_DECLARE_SIMPLE_TYPE(USBRedirServer, USB_REDIR_SERVER)

/*
 * The usbredir ep_info message has one slot per endpoint and direction.
 * Index 0-15 are the OUT endpoints, 16-31 the IN ones.
 */
#define USBREDIR_SERVER_MAX_EP 32
#define USBREDIR_SERVER_EP_IN_BASE 16

/* An endpoint number is 4 bits, so 0 to 15. */
#define USBREDIR_SERVER_MAX_EP_NR 16

/*
 * The bulk length field is 32 bits, so the host can ask for up to 4 GB.
 * This is the largest transfer accepted.
 */
#define USBREDIR_SERVER_MAX_BULK (1 * MiB)

/* Buffer size for an interrupt IN endpoint before its descriptor is seen. */
#define USBREDIR_SERVER_INTR_DEFAULT_LEN 64

#define USBREDIR_SERVER_CTRL_SETUP 0
#define USBREDIR_SERVER_CTRL_STATUS 1
#define USBREDIR_SERVER_BULK 2
#define USBREDIR_SERVER_INTR 3
#define USBREDIR_SERVER_INTR_STREAM 4

/* Which message answers the host when a control transfer ends. */
typedef enum {
    USBREDIR_SERVER_REPLY_CONTROL,
    USBREDIR_SERVER_REPLY_CONFIG,
    USBREDIR_SERVER_REPLY_ALT,
} USBRedirServerReply;

typedef struct USBRedirServerPkt {
    USBPacket pkt;

    /* Saved headers for the usbredir response */
    struct usb_redir_interrupt_packet_header intr_hdr;
    struct usb_redir_control_packet_header ctrl_hdr;
    struct usb_redir_bulk_packet_header bulk_hdr;

    /*
     * The IOV points here. IN data lands in it, OUT data is copied in.
     * Allocated and freed with the packet; data_size is how big it is.
     */
    uint8_t *data;
    int data_size;
    QTAILQ_ENTRY(USBRedirServerPkt) next;
    USBRedirServerReply reply;
    uint64_t redir_id;
    int type;
} USBRedirServerPkt;

struct USBRedirServer {
    SysBusDevice parent_obj;

    /* USB bus */
    USBBus bus;
    USBPort port;

    /* Properties */
    CharFrontend cs;

    /* usbredir over the chardev */
    struct usbredirparser *parser;
    QEMUTimer *announce_timer;
    QEMUBH *chardev_close_bh;
    const uint8_t *read_buf;
    int read_buf_size;
    bool in_write;
    guint watch;
    bool host_connected;
    bool device_announced;

    /*
     * Interrupt IN streaming, indexed by endpoint number. intr_bh asks the
     * device again; intr_retry does the same after a delay on NAK.
     */
    bool intr_in_started[USBREDIR_SERVER_MAX_EP_NR];
    QEMUBH *intr_bh;
    QEMUTimer *intr_retry;

    /* In-flight packet tracking */
    QTAILQ_HEAD(, USBRedirServerPkt) inflight;
    uint64_t next_id;

    /* Endpoint tables for the usbredir ep_info message. */
    uint8_t ep_type[USBREDIR_SERVER_MAX_EP];
    uint16_t ep_max_packet[USBREDIR_SERVER_MAX_EP];
    uint8_t ep_interval[USBREDIR_SERVER_MAX_EP];
    uint8_t ep_iface[USBREDIR_SERVER_MAX_EP];
};

#endif /* HW_USB_REDIRECT_SERVER_H */
