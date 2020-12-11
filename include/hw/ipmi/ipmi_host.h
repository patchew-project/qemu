/*
 * IPMI host interface
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

#ifndef HW_IPMI_HOST_H
#define HW_IPMI_HOST_H

#include "hw/ipmi/ipmi_responder.h"

#define TYPE_IPMI_HOST "ipmi-host"
#define IPMI_HOST(obj) \
     OBJECT_CHECK(IPMIHost, (obj), TYPE_IPMI_HOST)
#define IPMI_HOST_CLASS(obj_class) \
     OBJECT_CLASS_CHECK(IPMIHostClass, (obj_class), TYPE_IPMI_HOST)
#define IPMI_HOST_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IPMIHostClass, (obj), TYPE_IPMI_HOST)

/**
 * struct IPMIHost defines an IPMI host interface. It can be a simulator or a
 * connection to an emulated or real host.
 * @responder: The IPMI responder that handles an IPMI message.
 */
typedef struct IPMIHost {
    DeviceState parent;

    IPMIResponder *responder;
} IPMIHost;

/**
 * struct IPMIHostClass defines an IPMI host class.
 * @handle_command: Handle a command to the host.
 */
typedef struct IPMIHostClass {
    DeviceClass parent;

    /*
     * Handle a command to the bmc.
     */
    void (*handle_command)(struct IPMIHost *s,
                           uint8_t *cmd, unsigned int cmd_len,
                           unsigned int max_cmd_len, uint8_t msg_id);
} IPMIHostClass;

#endif /* HW_IPMI_HOST_H */
