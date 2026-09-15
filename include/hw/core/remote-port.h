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
#include "qemu/event_notifier.h"
#include "hw/core/ptimer.h"

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

typedef struct RemotePortRespSlot {
            RemotePortDynPkt rsp;
            uint32_t id;
            bool used;
            bool valid;
} RemotePortRespSlot;

struct RemotePort {
    DeviceState parent;

    QemuThread thread;
    EventNotifier event_notifier;
    Chardev *chrdev;
    CharFrontend chr;
    bool do_sync;
    bool doing_sync;
    bool finalizing;
    /* To serialize writes to fd.  */
    QemuMutex write_mutex;

    char *chrdev_id;
    struct rp_peer_state peer;

    struct {
        ptimer_state *ptimer;
        ptimer_state *ptimer_resp;
        bool resp_timer_enabled;
        bool need_sync;
        struct rp_pkt rsp;
        uint64_t quantum;
    } sync;

    QemuMutex rsp_mutex;
    QemuCond progress_cond;

#define RX_QUEUE_SIZE 1024
    struct {
        /* This array must be sized minimum 2 and always a power of 2.  */
        RemotePortDynPkt pkt[RX_QUEUE_SIZE];
        bool inuse[RX_QUEUE_SIZE];
        QemuSemaphore sem;
        unsigned int wpos;
        unsigned int rpos;
    } rx_queue;

    /*
     * rsp holds responses for the remote side.
     * Used by the slave.
     */
    RemotePortDynPkt rsp;

    /*
     * rspqueue holds received responses from the remote side.
     * Only one for the moment but it might grow.
     * Used by the master.
     */
    RemotePortDynPkt rspqueue;

    const char *prefix;
    const char *remote_prefix;

    uint32_t current_id;
    bool reset_done;

#define REMOTE_PORT_MAX_DEVS 1024
#define RP_MAX_OUTSTANDING_TRANSACTIONS 32
    struct {
        RemotePortRespSlot rsp_queue[RP_MAX_OUTSTANDING_TRANSACTIONS];
    } dev_state[REMOTE_PORT_MAX_DEVS];

    RemotePortDevice *devs[REMOTE_PORT_MAX_DEVS];
};

void rp_register_dev(RemotePort *s, RemotePortDevice *dev, uint32_t chan);


void rp_rsp_mutex_lock(RemotePort *s);
void rp_rsp_mutex_unlock(RemotePort *s);

RemotePortDynPkt rp_wait_resp(RemotePort *s);

RemotePortRespSlot *rp_dev_wait_resp(RemotePort *s, uint32_t dev, uint32_t id);
RemotePortRespSlot *rp_dev_timed_wait_resp(RemotePort *s, uint32_t dev,
                                           uint32_t id, int timems);
void rp_restart_sync_timer(RemotePort *s);

int64_t rp_normalized_vmclk(RemotePort *s);

void rp_process(RemotePort *s);

ssize_t rp_write(RemotePort *s, const void *buf, size_t count);

struct rp_peer_state *rp_get_peer(RemotePort *s);

#endif
