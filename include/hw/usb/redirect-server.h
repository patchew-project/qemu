/*
 * USB redirector, server side
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_REDIRECT_SERVER_H
#define HW_USB_REDIRECT_SERVER_H

#include "hw/core/sysbus.h"
#include "hw/usb/usb.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#include <usbredirparser.h>

#define TYPE_USB_REDIR_SERVER "usb-redir-server"
OBJECT_DECLARE_SIMPLE_TYPE(USBRedirServer, USB_REDIR_SERVER)

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
};

#endif /* HW_USB_REDIRECT_SERVER_H */
