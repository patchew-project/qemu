/*
 * RDMA device: Definitions of Resource Manager structures
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMA_RM_DEFS_H
#define RDMA_RM_DEFS_H

#include "rdma_backend_defs.h"

#define MAX_PORTS             1
#define MAX_PORT_GIDS         1
#define MAX_PORT_PKEYS        1
#define MAX_PKEYS             1
#define MAX_GIDS              2048
#define MAX_UCS               512
#define MAX_MR_SIZE           (1UL << 27)
#define MAX_QP                1024
#define MAX_SGE               4
#define MAX_CQ                2048
#define MAX_MR                1024
#define MAX_PD                1024
#define MAX_QP_RD_ATOM        16
#define MAX_QP_INIT_RD_ATOM   16
#define MAX_AH                64

#define MAX_RMRESTBL_NAME_SZ 16
typedef struct RdmaRmResTbl {
    char name[MAX_RMRESTBL_NAME_SZ];
    unsigned long *bitmap;
    size_t tbl_sz;
    size_t res_sz;
    void *tbl;
    QemuMutex lock;
} RdmaRmResTbl;

typedef struct RdmaRmPD {
    uint32_t ctx_handle;
    RdmaBackendPD backend_pd;
} RdmaRmPD;

typedef struct RdmaRmCQ {
    void *opaque;
    bool notify;
    RdmaBackendCQ backend_cq;
} RdmaRmCQ;

typedef struct RdmaRmUserMR {
    uint64_t host_virt;
    uint64_t guest_start;
    size_t length;
} RdmaRmUserMR;

/* MR (DMA region) */
typedef struct RdmaRmMR {
    uint32_t pd_handle;
    uint32_t lkey;
    uint32_t rkey;
    RdmaBackendMR backend_mr;
    RdmaRmUserMR user_mr;
    uint64_t addr;
    size_t length;
} RdmaRmMR;

typedef struct RdmaRmUC {
    uint64_t uc_handle;
} RdmaRmUC;

typedef struct RdmaRmQP {
    uint32_t qp_type;
    enum ibv_qp_state qp_state;
    uint32_t qpn;
    void *opaque;
    uint32_t send_cq_handle;
    uint32_t recv_cq_handle;
    RdmaBackendQP backend_qp;
} RdmaRmQP;

typedef struct RdmaRmPort {
    enum ibv_port_state state;
    union ibv_gid gid_tbl[MAX_PORT_GIDS];
    int *pkey_tbl; /* TODO: Not yet supported */
} RdmaRmPort;

typedef struct RdmaDeviceResources {
    RdmaRmPort ports[MAX_PORTS];
    RdmaRmResTbl pd_tbl;
    RdmaRmResTbl mr_tbl;
    RdmaRmResTbl uc_tbl;
    RdmaRmResTbl qp_tbl;
    RdmaRmResTbl cq_tbl;
    RdmaRmResTbl cqe_ctx_tbl;
    GHashTable *qp_hash; /* Keeps mapping between real and emulated */
} RdmaDeviceResources;

#endif
