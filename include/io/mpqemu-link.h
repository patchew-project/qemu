/*
 * Communication channel between QEMU and remote device process
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MPQEMU_LINK_H
#define MPQEMU_LINK_H

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qom/object.h"
#include "qemu/thread.h"
#include "io/channel.h"
#include "io/channel-socket.h"

#define REMOTE_MAX_FDS 8

#define MPQEMU_MSG_HDR_SIZE offsetof(MPQemuMsg, data1.u64)

/**
 * MPQemuCmd:
 *
 * MPQemuCmd enum type to specify the command to be executed on the remote
 * device.
 */
typedef enum {
    INIT = 0,
    RET_MSG,
    MAX = INT_MAX,
} MPQemuCmd;

/**
 * Maximum size of data2 field in the message to be transmitted.
 */
#define MPQEMU_MSG_DATA_MAX 256

/**
 * MPQemuMsg:
 * @cmd: The remote command
 * @bytestream: Indicates if the data to be shared is structured (data1)
 *              or unstructured (data2)
 * @size: Size of the data to be shared
 * @data1: Structured data
 * @fds: File descriptors to be shared with remote device
 * @data2: Unstructured data
 *
 * MPQemuMsg Format of the message sent to the remote device from QEMU.
 *
 */
typedef struct {
    int cmd;
    int bytestream;
    size_t size;

    union {
        uint64_t u64;
    } data1;

    int fds[REMOTE_MAX_FDS];
    int num_fds;

    /* Max size of data2 is MPQEMU_MSG_DATA_MAX */
    uint8_t *data2;
} MPQemuMsg;

struct MPQemuRequest {
    MPQemuMsg *msg;
    QIOChannelSocket *sioc;
    Coroutine *co;
    bool finished;
    int error;
    long ret;
};

typedef struct MPQemuRequest MPQemuRequest;

uint64_t mpqemu_msg_send_reply_co(MPQemuMsg *msg, QIOChannel *ioc,
                                  Error **errp);

void mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc);
int mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc);

bool mpqemu_msg_valid(MPQemuMsg *msg);

#endif
