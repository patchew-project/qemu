/*
 * Kernel Data Bridge driver - API
 *
 * Copyright 2016 Red Hat, Inc.
 * Copyright 2016 Oracle
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *   Yuval Shaia <yuval.shaia@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _KDBR_H
#define _KDBR_H

#ifdef __KERNEL__
#include <linux/uio.h>
#define KDBR_MAX_IOVEC_LEN    UIO_FASTIOV
#else
#include <sys/uio.h>
#define KDBR_MAX_IOVEC_LEN    8
#endif

#define KDBR_FILE_NAME "/dev/kdbr"
#define KDBR_MAX_PORTS 255

#define KDBR_IOC_MAGIC 0xBA

#define KDBR_REGISTER_PORT    _IOWR(KDBR_IOC_MAGIC, 0, struct kdbr_reg)
#define KDBR_UNREGISTER_PORT    _IOW(KDBR_IOC_MAGIC, 1, int)
#define KDBR_IOC_MAX        2


enum kdbr_ack_type {
    KDBR_ACK_IMMEDIATE,
    KDBR_ACK_DELAYED,
};

struct kdbr_gid {
    unsigned long net_id;
    unsigned long id;
};

struct kdbr_peer {
    struct kdbr_gid rgid;
    unsigned long rqueue;
};

struct list_head;
struct mutex;
struct kdbr_connection {
    unsigned long queue_id;
    struct kdbr_peer peer;
    enum kdbr_ack_type ack_type;
    /* TODO: hide the below fields in the .c file */
    struct list_head *sg_vecs_list;
    struct mutex *sg_vecs_mutex;
};

struct kdbr_reg {
    struct kdbr_gid gid; /* in */
    int port; /* out */
};

#define KDBR_REQ_SIGNATURE    0x000000AB
#define KDBR_REQ_POST_RECV    0x00000100
#define KDBR_REQ_POST_SEND    0x00000200
#define KDBR_REQ_POST_MREG    0x00000300
#define KDBR_REQ_POST_RDMA    0x00000400

struct kdbr_req {
    unsigned int flags; /* 8 bits signature, 8 bits msg_type */
    struct iovec vec[KDBR_MAX_IOVEC_LEN];
    int vlen; /* <= KDBR_MAX_IOVEC_LEN */
    int connection_id;
    struct kdbr_peer peer;
    unsigned long req_id;
};

#define KDBR_ERR_CODE_EMPTY_VEC           0x101
#define KDBR_ERR_CODE_NO_MORE_RECV_BUF    0x102
#define KDBR_ERR_CODE_RECV_BUF_PROT       0x103
#define KDBR_ERR_CODE_INV_ADDR            0x104
#define KDBR_ERR_CODE_INV_CONN_ID         0x105
#define KDBR_ERR_CODE_NO_PEER             0x106

struct kdbr_completion {
    int connection_id;
    unsigned long req_id;
    int status; /* 0 = Success */
};

#define KDBR_PORT_IOC_MAGIC    0xBB

#define KDBR_PORT_OPEN_CONN    _IOR(KDBR_PORT_IOC_MAGIC, 0, \
                     struct kdbr_connection)
#define KDBR_PORT_CLOSE_CONN    _IOR(KDBR_PORT_IOC_MAGIC, 1, int)
#define KDBR_PORT_IOC_MAX    4

#endif

