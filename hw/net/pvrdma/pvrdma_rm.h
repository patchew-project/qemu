/*
 * QEMU VMWARE paravirtual RDMA - Resource Manager
 *
 * Developed by Oracle & Redhat
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PVRDMA_RM_H
#define PVRDMA_RM_H

#include <hw/net/pvrdma/pvrdma_dev_api.h>
#include <hw/net/pvrdma/pvrdma-uapi.h>
#include <hw/net/pvrdma/pvrdma_ring.h>
#include <hw/net/pvrdma/kdbr.h>

/* TODO: More then 1 port it fails in ib_modify_qp, maybe something with
 * the MAC of the second port */
#define MAX_PORTS        1 /* Driver force to 1 see pvrdma_add_gid */
#define MAX_PORT_GIDS    1
#define MAX_PORT_PKEYS   1
#define MAX_PKEYS        1
#define MAX_PDS          2048
#define MAX_CQS          2048
#define MAX_CQES         1024 /* cqe size is 64 */
#define MAX_QPS          1024
#define MAX_GIDS         2048
#define MAX_QP_WRS       1024 /* wqe size is 128 */
#define MAX_SGES         4
#define MAX_MRS          2048
#define MAX_AH           1024

typedef struct PVRDMADev PVRDMADev;
typedef struct KdbrPort KdbrPort;

#define MAX_RMRESTBL_NAME_SZ 16
typedef struct RmResTbl {
    char name[MAX_RMRESTBL_NAME_SZ];
    unsigned long *bitmap;
    size_t tbl_sz;
    size_t res_sz;
    void *tbl;
    QemuMutex lock;
} RmResTbl;

enum cq_comp_type {
    CCT_NONE,
    CCT_SOLICITED,
    CCT_NEXT_COMP,
};

typedef struct RmPD {
    __u32 ctx_handle;
} RmPD;

typedef struct RmCQ {
    struct pvrdma_cmd_create_cq init_args;
    struct pvrdma_ring *ring_state;
    Ring cq;
    enum cq_comp_type comp_type;
} RmCQ;

/* MR (DMA region) */
typedef struct RmMR {
    __u32 pd_handle;
    __u32 lkey;
    __u32 rkey;
} RmMR;

typedef struct RmSqWqe {
    struct pvrdma_sq_wqe_hdr hdr;
    struct pvrdma_sge sge[0];
} RmSqWqe;

typedef struct RmRqWqe {
    struct pvrdma_rq_wqe_hdr hdr;
    struct pvrdma_sge sge[0];
} RmRqWqe;

typedef struct RmQP {
    struct pvrdma_cmd_create_qp init_args;
    enum pvrdma_qp_state qp_state;
    u8 port_num;
    u32 dest_qp_num;
    union pvrdma_gid dgid;

    struct pvrdma_ring *sq_ring_state;
    Ring sq;
    struct pvrdma_ring *rq_ring_state;
    Ring rq;

    unsigned long kdbr_connection_id;
} RmQP;

typedef struct RmPort {
    enum pvrdma_port_state state;
    union pvrdma_gid gid_tbl[MAX_PORT_GIDS];
    /* TODO: Change type */
    int *pkey_tbl;
    KdbrPort *kdbr_port;
} RmPort;

static inline int rm_get_max_port_gids(__u32 *max_port_gids)
{
    *max_port_gids = MAX_PORT_GIDS;
    return 0;
}

static inline int rm_get_max_port_pkeys(__u32 *max_port_pkeys)
{
    *max_port_pkeys = MAX_PORT_PKEYS;
    return 0;
}

static inline int rm_get_max_pkeys(__u16 *max_pkeys)
{
    *max_pkeys = MAX_PKEYS;
    return 0;
}

static inline int rm_get_max_cqs(__u32 *max_cqs)
{
    *max_cqs = MAX_CQS;
    return 0;
}

static inline int rm_get_max_cqes(__u32 *max_cqes)
{
    *max_cqes = MAX_CQES;
    return 0;
}

static inline int rm_get_max_pds(__u32 *max_pds)
{
    *max_pds = MAX_PDS;
    return 0;
}

static inline int rm_get_max_qps(__u32 *max_qps)
{
    *max_qps = MAX_QPS;
    return 0;
}

static inline int rm_get_max_gids(__u32 *max_gids)
{
    *max_gids = MAX_GIDS;
    return 0;
}

static inline int rm_get_max_qp_wrs(__u32 *max_qp_wrs)
{
    *max_qp_wrs = MAX_QP_WRS;
    return 0;
}

static inline int rm_get_max_sges(__u32 *max_sges)
{
    *max_sges = MAX_SGES;
    return 0;
}

static inline int rm_get_max_mrs(__u32 *max_mrs)
{
    *max_mrs = MAX_MRS;
    return 0;
}

static inline int rm_get_phys_port_cnt(__u8 *phys_port_cnt)
{
    *phys_port_cnt = MAX_PORTS;
    return 0;
}

static inline int rm_get_max_ah(__u32 *max_ah)
{
    *max_ah = MAX_AH;
    return 0;
}

int rm_init(PVRDMADev *dev);
void rm_fini(PVRDMADev *dev);

int rm_alloc_pd(PVRDMADev *dev, __u32 *pd_handle, __u32 ctx_handle);
void rm_dealloc_pd(PVRDMADev *dev, __u32 pd_handle);

RmCQ *rm_get_cq(PVRDMADev *dev, __u32 cq_handle);
int rm_alloc_cq(PVRDMADev *dev, struct pvrdma_cmd_create_cq *cmd,
        struct pvrdma_cmd_create_cq_resp *resp);
void rm_req_notify_cq(PVRDMADev *dev, __u32 cq_handle, u32 flags);
void rm_dealloc_cq(PVRDMADev *dev, __u32 cq_handle);

int rm_alloc_mr(PVRDMADev *dev, struct pvrdma_cmd_create_mr *cmd,
        struct pvrdma_cmd_create_mr_resp *resp);
void rm_dealloc_mr(PVRDMADev *dev, __u32 mr_handle);

RmQP *rm_get_qp(PVRDMADev *dev, __u32 qp_handle);
int rm_alloc_qp(PVRDMADev *dev, struct pvrdma_cmd_create_qp *cmd,
        struct pvrdma_cmd_create_qp_resp *resp);
int rm_modify_qp(PVRDMADev *dev, __u32 qp_handle,
         struct pvrdma_cmd_modify_qp *modify_qp_args);
void rm_dealloc_qp(PVRDMADev *dev, __u32 qp_handle);

void *rm_get_wqe_ctx(PVRDMADev *dev, unsigned long wqe_ctx_id);
int rm_alloc_wqe_ctx(PVRDMADev *dev, unsigned long *wqe_ctx_id, void *ctx);
void rm_dealloc_wqe_ctx(PVRDMADev *dev, unsigned long wqe_ctx_id);

#endif
