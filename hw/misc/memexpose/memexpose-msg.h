/*
 *  Memexpose core
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _MEMEXPOSE_MSG_H_
#define _MEMEXPOSE_MSG_H_

#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "chardev/char-fe.h"
#include "exec/memattrs.h"
#include <inttypes.h>

#define MEMEXPOSE_MAX_INTR_DATA_SIZE 128

enum memexpose_op_type {
    MOP_READ,
    MOP_READ_RET,
    MOP_WRITE,
    MOP_WRITE_RET,
    MOP_REG_INV,
    MOP_REG_INV_RET,
    MOP_INTR,
};

enum memexpose_memshare_type {
    MEMSHARE_NONE,
    MEMSHARE_FD,
};

/*
 * TODO - we'll need to share more info here, like access permissions for
 * example
 */
struct memexpose_memshare_info_fd {
    uint64_t start;
    uint64_t mmap_start;
    uint64_t size;
    uint8_t readonly;
    uint8_t nonvolatile;
} __attribute__((packed));

/* TODO - this might have variable size in the future */
struct memexpose_memshare_info {
    uint8_t type;
    union {
        struct memexpose_memshare_info_fd fd;
    };
} __attribute__((packed));

/* TODO - endianness */
struct memexpose_op_head {
    uint32_t size;
    uint8_t ot;
    uint8_t prio;
} __attribute__((packed));

struct memexpose_op_read {
    uint64_t offset;
    uint8_t size;
} __attribute__((packed));

struct memexpose_op_write {
    uint64_t offset;
    uint64_t value;
    uint8_t size;
} __attribute__((packed));

struct memexpose_op_read_ret {
    MemTxResult ret;
    uint64_t value;
    struct memexpose_memshare_info share;
} __attribute__((packed));

struct memexpose_op_write_ret {
    MemTxResult ret;
    struct memexpose_memshare_info share;
} __attribute__((packed));

struct memexpose_op_intr {
    uint64_t type;
    uint8_t data[MEMEXPOSE_MAX_INTR_DATA_SIZE];
} __attribute__((packed));

struct memexpose_op_reg_inv {
    uint64_t start;
    uint64_t size;
} __attribute__((packed));

union memexpose_op_all {
    struct memexpose_op_read read;
    struct memexpose_op_write write;
    struct memexpose_op_read_ret read_ret;
    struct memexpose_op_write_ret write_ret;
    struct memexpose_op_intr intr;
    struct memexpose_op_reg_inv reg_inv;
} __attribute__((packed));

struct memexpose_op {
    struct memexpose_op_head head;
    union memexpose_op_all body;
} __attribute__((packed));

enum MemexposeMsgState {
    MEMEXPOSE_MSG_READ_SIZE,
    MEMEXPOSE_MSG_READ_BODY,
    MEMEXPOSE_MSG_BROKEN,
};

typedef struct MemexposeMsg {
    int read_state;
    int bytes;
    struct memexpose_op buf;
} MemexposeMsg;

typedef struct MemexposeEp {
    CharBackend *chr;
    MemexposeMsg msg;
    bool is_async;
    int prio;
    void *data;
    void (*handle_msg)(void *data, struct memexpose_op *op, Error **err);

    int connected;
    struct memexpose_op queued_op;
    QEMUBH *queue_msg_bh;
} MemexposeEp;

void memexpose_ep_init(MemexposeEp *ep, CharBackend *chr, void *data, int prio,
                       void (*handle_msg)(void *data, struct memexpose_op *op,
                                          Error **errp));
void memexpose_ep_destroy(MemexposeEp *ep);

int memexpose_ep_connect(MemexposeEp *ep);
void memexpose_ep_disconnect(MemexposeEp *ep);

/* TODO - functions for header boilerplate */
void memexpose_ep_write_sync(MemexposeEp *ep, struct memexpose_op *op);
void memexpose_ep_write_async(MemexposeEp *ep, struct memexpose_op *op);
void memexpose_ep_send_fd(MemexposeEp *ep, int fd);
int memexpose_ep_recv_fd(MemexposeEp *ep);
int memexpose_ep_msg_prio(MemexposeEp *ep, enum memexpose_op_type);

#endif /* _MEMEXPOSE_MSG_H_ */
