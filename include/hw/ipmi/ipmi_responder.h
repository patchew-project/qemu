/*
 * IPMI responder interface
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef HW_IPMI_RESPONDER_H
#define HW_IPMI_RESPONDER_H

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

#define TYPE_IPMI_RESPONDER_PREFIX "ipmi-responder-"
#define TYPE_IPMI_RESPONDER "ipmi-responder"
#define IPMI_RESPONDER(obj) \
     INTERFACE_CHECK(IPMIResponder, (obj), TYPE_IPMI_RESPONDER)
#define IPMI_RESPONDER_CLASS(class) \
     OBJECT_CLASS_CHECK(IPMIResponderClass, (class), TYPE_IPMI_RESPONDER)
#define IPMI_RESPONDER_GET_CLASS(class) \
     OBJECT_GET_CLASS(IPMIResponderClass, (class), TYPE_IPMI_RESPONDER)

struct IPMIHost;

/**
 * This interface is implemented by each IPMI responder device (KCS, BT, PCI,
 * etc.) An IPMI host device uses it to transfer data to the emulated BMC.
 */
typedef struct IPMIResponder IPMIResponder;

/**
 * struct IPMIResponderClass implemented by an IPMI responder device like KCS to
 * handle commands from connected IPMI host device.
 * @get_host: Return the IPMI host (e.g. ipmi-host-extern) that uses this
 *  responder.
 * @set_host: Set the IPMI host (e.g. ipmi-host-extern) that uses this
 *  responder.
 * @get_backend_data: Return the backend device (e.g. KCS, BT) of the
 *  corresponding responder.
 * @handle_req: The IPMI Host device calls this function when it receives a sane
 *  IPMI message. A responder should handle this message.
 */
typedef struct IPMIResponderClass {
    InterfaceClass parent;

    struct IPMIHost *(*get_host)(struct IPMIResponder *s);

    void (*set_host)(struct IPMIResponder *s, struct IPMIHost *h);

    void *(*get_backend_data)(struct IPMIResponder *s);

    void (*handle_req)(struct IPMIResponder *s, uint8_t msg_id,
            unsigned char *req, unsigned req_len);
} IPMIResponderClass;

#endif /* HW_IPMI_RESPONDER_H */
