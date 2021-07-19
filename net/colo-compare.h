/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2017 FUJITSU LIMITED
 * Copyright (c) 2017 Intel Corporation
 *
 * Authors:
 *    zhanghailiang <zhang.zhanghailiang@huawei.com>
 *    Zhang Chen <zhangckid@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_COLO_COMPARE_H
#define QEMU_COLO_COMPARE_H

#include "net/net.h"
#include "chardev/char-fe.h"
#include "migration/colo.h"
#include "migration/migration.h"
#include "sysemu/iothread.h"
#include "colo.h"

#define TYPE_COLO_COMPARE "colo-compare"
typedef struct CompareState CompareState;
DECLARE_INSTANCE_CHECKER(CompareState, COLO_COMPARE,
                         TYPE_COLO_COMPARE)

typedef struct COLOSendCo {
    Coroutine *co;
    struct CompareState *s;
    CharBackend *chr;
    GQueue send_list;
    bool notify_remote_frame;
    bool done;
    int ret;
} COLOSendCo;

/*
 *  + CompareState ++
 *  |               |
 *  +---------------+   +---------------+         +---------------+
 *  |   conn list   + - >      conn     + ------- >      conn     + -- > ......
 *  +---------------+   +---------------+         +---------------+
 *  |               |     |           |             |          |
 *  +---------------+ +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 *                        |           |             |          |
 *                    +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 *                        |           |             |          |
 *                    +---v----+  +---v----+    +---v----+ +---v----+
 *                    |primary |  |secondary    |primary | |secondary
 *                    |packet  |  |packet  +    |packet  | |packet  +
 *                    +--------+  +--------+    +--------+ +--------+
 */
struct CompareState {
    Object parent;

    char *pri_indev;
    char *sec_indev;
    char *outdev;
    char *notify_dev;
    CharBackend chr_pri_in;
    CharBackend chr_sec_in;
    CharBackend chr_out;
    CharBackend chr_notify_dev;
    SocketReadState pri_rs;
    SocketReadState sec_rs;
    SocketReadState notify_rs;
    COLOSendCo out_sendco;
    COLOSendCo notify_sendco;
    bool vnet_hdr;
    uint64_t compare_timeout;
    uint32_t expired_scan_cycle;

    /*
     * Record the connection that through the NIC
     * Element type: Connection
     */
    GQueue conn_list;
    /* Record the connection without repetition */
    GHashTable *connection_track_table;

    IOThread *iothread;
    GMainContext *worker_context;
    QEMUTimer *packet_check_timer;

    QEMUBH *event_bh;
    enum colo_event event;

    QTAILQ_ENTRY(CompareState) next;
};

typedef struct CompareClass {
    ObjectClass parent_class;
} CompareClass;

void colo_notify_compares_event(void *opaque, int event, Error **errp);
void colo_compare_register_notifier(Notifier *notify);
void colo_compare_unregister_notifier(Notifier *notify);
void colo_compare_cleanup(void);

#endif /* QEMU_COLO_COMPARE_H */
