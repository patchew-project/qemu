/*
 * QEMU paravirtual RDMA - rdmacm-mux declarations
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMACM_MUX_H
#define RDMACM_MUX_H

#include "linux/if.h"
#include "infiniband/verbs.h"
#include "infiniband/umad.h"
#include "rdma/rdma_user_cm.h"

typedef enum RdmaCmMuxMsgType {
    RDMACM_MUX_MSG_TYPE_REG   = 0,
    RDMACM_MUX_MSG_TYPE_UNREG = 1,
    RDMACM_MUX_MSG_TYPE_MAD   = 2,
} RdmaCmMuxMsgType;

typedef struct RdmaCmMuxHdr {
    RdmaCmMuxMsgType msg_type;
    union ibv_gid sgid;
    char ifname[IFNAMSIZ];
} RdmaCmUHdr;

typedef struct RdmaCmUMad {
    struct ib_user_mad hdr;
    char mad[RDMA_MAX_PRIVATE_DATA];
} RdmaCmUMad;

typedef struct RdmaCmMuxMsg {
    RdmaCmUHdr hdr;
    int umad_len;
    RdmaCmUMad umad;
} RdmaCmMuxMsg;

#endif
