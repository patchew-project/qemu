/*
 * vmnet_int.h
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef VMNET_INT_H
#define VMNET_INT_H

#include "qemu/osdep.h"
#include "vmnet_int.h"
#include "clients.h"

#include <vmnet/vmnet.h>
#include <dispatch/dispatch.h>

/**
 *  From vmnet.framework documentation
 *
 *  Each read/write call allows up to 200 packets to be
 *  read or written for a maximum of 256KB.
 *
 *  Each packet written should be a complete
 *  ethernet frame.
 *
 *  https://developer.apple.com/documentation/vmnet
 */
#define VMNET_PACKETS_LIMIT 200

typedef struct VmnetCommonState {
    NetClientState nc;
    interface_ref vmnet_if;

    bool send_scheduled;

    uint64_t mtu;
    uint64_t max_packet_size;

    struct vmpktdesc packets_buf[VMNET_PACKETS_LIMIT];
    struct iovec iov_buf[VMNET_PACKETS_LIMIT];

    dispatch_queue_t if_queue;

    QEMUBH *send_bh;
} VmnetCommonState;

const char *vmnet_status_map_str(vmnet_return_t status);

int vmnet_if_create(NetClientState *nc,
                    xpc_object_t if_desc,
                    Error **errp);

ssize_t vmnet_receive_common(NetClientState *nc,
                             const uint8_t *buf,
                             size_t size);

void vmnet_cleanup_common(NetClientState *nc);

#endif /* VMNET_INT_H */
