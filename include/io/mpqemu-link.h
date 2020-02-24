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
#include "exec/cpu-common.h"
#include "exec/hwaddr.h"

#define TYPE_MPQEMU_LINK "mpqemu-link"
#define MPQEMU_LINK(obj) \
    OBJECT_CHECK(MPQemuLinkState, (obj), TYPE_MPQEMU_LINK)

#define REMOTE_MAX_FDS 8

#define MPQEMU_MSG_HDR_SIZE offsetof(MPQemuMsg, data1.u64)

/*
 * TODO: Dont use mpqemu link object since it is
 * not needed to be created via -object.
 */

/**
 * mpqemu_cmd_t:
 * PCI_CONFIG_READ        PCI configuration space read
 * PCI_CONFIG_WRITE       PCI configuration space write
 * SYNC_SYSMEM      Shares QEMU's RAM with remote device's RAM
 *
 * proc_cmd_t enum type to specify the command to be executed on the remote
 * device.
 */
typedef enum {
    INIT = 0,
    PCI_CONFIG_READ,
    PCI_CONFIG_WRITE,
    SYNC_SYSMEM,
    MAX,
} mpqemu_cmd_t;

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
    hwaddr gpas[REMOTE_MAX_FDS];
    uint64_t sizes[REMOTE_MAX_FDS];
    ram_addr_t offsets[REMOTE_MAX_FDS];
} sync_sysmem_msg_t;

typedef struct {
    mpqemu_cmd_t cmd;
    int bytestream;
    size_t size;

    union {
        uint64_t u64;
        sync_sysmem_msg_t sync_sysmem;
    } data1;

    int fds[REMOTE_MAX_FDS];
    int num_fds;

    uint8_t *data2;
} MPQemuMsg;

struct conf_data_msg {
    uint32_t addr;
    uint32_t val;
    int l;
};

/**
 * MPQemuChannel:
 * @gsrc: GSource object to be used by loop
 * @gpfd: GPollFD object containing the socket & events to monitor
 * @sock: Socket to send/receive communication, same as the one in gpfd
 * @send_lock: Mutex to synchronize access to the send stream
 * @recv_lock: Mutex to synchronize access to the recv stream
 *
 * Defines the channel that make up the communication link
 * between QEMU and remote process
 */

typedef struct MPQemuChannel {
    GSource gsrc;
    GPollFD gpfd;
    int sock;
    QemuMutex send_lock;
    QemuMutex recv_lock;
} MPQemuChannel;

typedef void (*mpqemu_link_callback)(GIOCondition cond, MPQemuChannel *chan);

/*
 * MPQemuLinkState Instance info. of the communication
 * link between QEMU and remote process. The Link could
 * be made up of multiple channels.
 *
 * ctx        GMainContext to be used for communication
 * loop       Main loop that would be used to poll for incoming data
 * com        Communication channel to transport control messages
 *
 */

typedef struct MPQemuLinkState {
    Object obj;

    GMainContext *ctx;
    GMainLoop *loop;

    MPQemuChannel *com;

    mpqemu_link_callback callback;
} MPQemuLinkState;

MPQemuLinkState *mpqemu_link_create(void);
void mpqemu_link_finalize(MPQemuLinkState *s);

void mpqemu_msg_send(MPQemuMsg *msg, MPQemuChannel *chan);
int mpqemu_msg_recv(MPQemuMsg *msg, MPQemuChannel *chan);

void mpqemu_init_channel(MPQemuLinkState *s, MPQemuChannel **chan, int fd);
void mpqemu_destroy_channel(MPQemuChannel *chan);
void mpqemu_link_set_callback(MPQemuLinkState *s,
                              mpqemu_link_callback callback);
void mpqemu_start_coms(MPQemuLinkState *s);

#define GET_REMOTE_WAIT eventfd(0, EFD_CLOEXEC)
#define PUT_REMOTE_WAIT(wait) close(wait)
#define PROXY_LINK_WAIT_DONE 1

uint64_t wait_for_remote(int efd);
void notify_proxy(int fd, uint64_t val);

#endif
