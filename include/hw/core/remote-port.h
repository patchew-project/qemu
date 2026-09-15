/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU remote port.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_H__
#define REMOTE_PORT_H__

#include <stdbool.h>
#include "hw/core/remote-port-proto.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qobject/qdict.h"

#define TYPE_REMOTE_PORT_DEVICE "remote-port-device"

#define REMOTE_PORT_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(RemotePortDeviceClass, (klass), TYPE_REMOTE_PORT_DEVICE)
#define REMOTE_PORT_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RemotePortDeviceClass, (obj), TYPE_REMOTE_PORT_DEVICE)
#define REMOTE_PORT_DEVICE(obj) \
     INTERFACE_CHECK(RemotePortDevice, (obj), TYPE_REMOTE_PORT_DEVICE)

typedef struct RemotePort RemotePort;

typedef struct RemotePortDevice {
    /*< private >*/
    Object parent_obj;
} RemotePortDevice;

typedef struct RemotePortDeviceClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /**
     * ops - operations to perform when remote port packets are recieved for
     * this device. Function N will be called for a remote port packet with
     * cmd == N in the header.
     *
     * @obj - Remote port device to recieve packet
     * @pkt - remote port packets
     */

    void (*ops[RP_CMD_max + 1])(RemotePortDevice *obj, struct rp_pkt *pkt);

} RemotePortDeviceClass;

#define TYPE_REMOTE_PORT "remote-port"
#define REMOTE_PORT(obj) OBJECT_CHECK(RemotePort, (obj), TYPE_REMOTE_PORT)

struct RemotePort {
    DeviceState parent;

    Chardev *chrdev;
    CharFrontend chr;
    bool finalizing;

    char *chrdev_id;

    const char *prefix;
    const char *remote_prefix;

    uint32_t current_id;
    bool reset_done;

#define REMOTE_PORT_MAX_DEVS 1024
    RemotePortDevice *devs[REMOTE_PORT_MAX_DEVS];
};

#endif
