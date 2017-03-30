#include <hw/net/pvrdma/pvrdma.h>
#include <hw/net/pvrdma/pvrdma_utils.h>
#include <hw/net/pvrdma/pvrdma_rm.h>
#include <hw/net/pvrdma/pvrdma-uapi.h>
#include <hw/net/pvrdma/pvrdma_kdbr.h>
#include <qemu/bitmap.h>
#include <qemu/atomic.h>
#include <cpu.h>

/* Page directory and page tables */
#define PG_DIR_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }
#define PG_TBL_SZ { TARGET_PAGE_SIZE / sizeof(__u64) }

/* Global local and remote keys */
__u64 global_lkey = 1;
__u64 global_rkey = 1;

static inline int res_tbl_init(const char *name, RmResTbl *tbl, u32 tbl_sz,
                               u32 res_sz)
{
    tbl->tbl = malloc(tbl_sz * res_sz);
    if (!tbl->tbl) {
        return -ENOMEM;
    }

    strncpy(tbl->name, name, MAX_RING_NAME_SZ);
    tbl->name[MAX_RING_NAME_SZ - 1] = 0;

    tbl->bitmap = bitmap_new(tbl_sz);
    tbl->tbl_sz = tbl_sz;
    tbl->res_sz = res_sz;
    qemu_mutex_init(&tbl->lock);

    return 0;
}

static inline void res_tbl_free(RmResTbl *tbl)
{
    qemu_mutex_destroy(&tbl->lock);
    free(tbl->tbl);
    bitmap_zero_extend(tbl->bitmap, tbl->tbl_sz, 0);
}

static inline void *res_tbl_get(RmResTbl *tbl, u32 handle)
{
    pr_dbg("%s, handle=%d\n", tbl->name, handle);

    if ((handle < tbl->tbl_sz) && (test_bit(handle, tbl->bitmap))) {
        return tbl->tbl + handle * tbl->res_sz;
    } else {
        pr_dbg("Invalid handle %d\n", handle);
        return NULL;
    }
}

static inline void *res_tbl_alloc(RmResTbl *tbl, u32 *handle)
{
    qemu_mutex_lock(&tbl->lock);

    *handle = find_first_zero_bit(tbl->bitmap, tbl->tbl_sz);
    if (*handle > tbl->tbl_sz) {
        pr_dbg("Fail to alloc, bitmap is full\n");
        qemu_mutex_unlock(&tbl->lock);
        return NULL;
    }

    set_bit(*handle, tbl->bitmap);

    qemu_mutex_unlock(&tbl->lock);

    pr_dbg("%s, handle=%d\n", tbl->name, *handle);

    return tbl->tbl + *handle * tbl->res_sz;
}

static inline void res_tbl_dealloc(RmResTbl *tbl, u32 handle)
{
    pr_dbg("%s, handle=%d\n", tbl->name, handle);

    qemu_mutex_lock(&tbl->lock);

    if (handle < tbl->tbl_sz) {
        clear_bit(handle, tbl->bitmap);
    }

    qemu_mutex_unlock(&tbl->lock);
}

int rm_alloc_pd(PVRDMADev *dev, __u32 *pd_handle, __u32 ctx_handle)
{
    RmPD *pd;

    pd = res_tbl_alloc(&dev->pd_tbl, pd_handle);
    if (!pd) {
        return -ENOMEM;
    }

    pd->ctx_handle = ctx_handle;

    return 0;
}

void rm_dealloc_pd(PVRDMADev *dev, __u32 pd_handle)
{
    res_tbl_dealloc(&dev->pd_tbl, pd_handle);
}

RmCQ *rm_get_cq(PVRDMADev *dev, __u32 cq_handle)
{
    return res_tbl_get(&dev->cq_tbl, cq_handle);
}

int rm_alloc_cq(PVRDMADev *dev, struct pvrdma_cmd_create_cq *cmd,
                struct pvrdma_cmd_create_cq_resp *resp)
{
    int rc = 0;
    RmCQ *cq;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    __u64 *dir = 0, *tbl = 0;
    char ring_name[MAX_RING_NAME_SZ];
    u32 cqe;

    cq = res_tbl_alloc(&dev->cq_tbl, &resp->cq_handle);
    if (!cq) {
        return -ENOMEM;
    }

    memset(cq, 0, sizeof(RmCQ));

    memcpy(&cq->init_args, cmd, sizeof(*cmd));
    cq->comp_type = CCT_NONE;

    /* Get pointer to CQ */
    dir = pvrdma_pci_dma_map(pci_dev, cq->init_args.pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        pr_err("Fail to map to CQ page directory\n");
        rc = -ENOMEM;
        goto out_free_cq;
    }
    tbl = pvrdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        pr_err("Fail to map to CQ page table\n");
        rc = -ENOMEM;
        goto out_free_cq;
    }

    cq->ring_state = (struct pvrdma_ring *)
            pvrdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!cq->ring_state) {
        pr_err("Fail to map to CQ header page\n");
        rc = -ENOMEM;
        goto out_free_cq;
    }

    sprintf(ring_name, "cq%d", resp->cq_handle);
    cqe = MIN(cmd->cqe, dev->dsr_info.dsr->caps.max_cqe);
    rc = ring_init(&cq->cq, ring_name, pci_dev, &cq->ring_state[1],
                   cqe, sizeof(struct pvrdma_cqe), (dma_addr_t *)&tbl[1],
                   cmd->nchunks - 1 /* first page is ring state */);
    if (rc != 0) {
        pr_err("Fail to initialize CQ ring\n");
        rc = -ENOMEM;
        goto out_free_ring_state;
    }


    resp->cqe = cmd->cqe;

    goto out;

out_free_ring_state:
    pvrdma_pci_dma_unmap(pci_dev, cq->ring_state, TARGET_PAGE_SIZE);

out_free_cq:
    rm_dealloc_cq(dev, resp->cq_handle);

out:
    if (tbl) {
        pvrdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);
    }
    if (dir) {
        pvrdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);
    }

    return rc;
}

void rm_req_notify_cq(PVRDMADev *dev, __u32 cq_handle, u32 flags)
{
    RmCQ *cq;

    pr_dbg("cq_handle=%d, flags=0x%x\n", cq_handle, flags);

    cq = rm_get_cq(dev, cq_handle);
    if (!cq) {
        return;
    }

    cq->comp_type = (flags & PVRDMA_UAR_CQ_ARM_SOL) ? CCT_SOLICITED :
                     CCT_NEXT_COMP;
    pr_dbg("comp_type=%d\n", cq->comp_type);
}

void rm_dealloc_cq(PVRDMADev *dev, __u32 cq_handle)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    RmCQ *cq;

    cq = rm_get_cq(dev, cq_handle);
    if (!cq) {
        return;
    }

    ring_free(&cq->cq);
    pvrdma_pci_dma_unmap(pci_dev, cq->ring_state, TARGET_PAGE_SIZE);
    res_tbl_dealloc(&dev->cq_tbl, cq_handle);
}

int rm_alloc_mr(PVRDMADev *dev, struct pvrdma_cmd_create_mr *cmd,
                struct pvrdma_cmd_create_mr_resp *resp)
{
    RmMR *mr;

    mr = res_tbl_alloc(&dev->mr_tbl, &resp->mr_handle);
    if (!mr) {
        return -ENOMEM;
    }

    mr->pd_handle = cmd->pd_handle;
    resp->lkey = mr->lkey = global_lkey++;
    resp->rkey = mr->rkey = global_rkey++;

    return 0;
}

void rm_dealloc_mr(PVRDMADev *dev, __u32 mr_handle)
{
    res_tbl_dealloc(&dev->mr_tbl, mr_handle);
}

int rm_alloc_qp(PVRDMADev *dev, struct pvrdma_cmd_create_qp *cmd,
                struct pvrdma_cmd_create_qp_resp *resp)
{
    int rc = 0;
    RmQP *qp;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    __u64 *dir = 0, *tbl = 0;
    int wqe_size;
    char ring_name[MAX_RING_NAME_SZ];

    if (!rm_get_cq(dev, cmd->send_cq_handle) ||
        !rm_get_cq(dev, cmd->recv_cq_handle)) {
        pr_err("Invalid send_cqn or recv_cqn (%d, %d)\n",
               cmd->send_cq_handle, cmd->recv_cq_handle);
        return -EINVAL;
    }

    qp = res_tbl_alloc(&dev->qp_tbl, &resp->qpn);
    if (!qp) {
        return -EINVAL;
    }

    memset(qp, 0, sizeof(RmQP));

    memcpy(&qp->init_args, cmd, sizeof(*cmd));

    pr_dbg("qp_type=%d\n", qp->init_args.qp_type);
    pr_dbg("send_cq_handle=%d\n", qp->init_args.send_cq_handle);
    pr_dbg("max_send_sge=%d\n", qp->init_args.max_send_sge);
    pr_dbg("recv_cq_handle=%d\n", qp->init_args.recv_cq_handle);
    pr_dbg("max_recv_sge=%d\n", qp->init_args.max_recv_sge);
    pr_dbg("total_chunks=%d\n", cmd->total_chunks);
    pr_dbg("send_chunks=%d\n", cmd->send_chunks);
    pr_dbg("recv_chunks=%d\n", cmd->total_chunks - cmd->send_chunks);

    qp->qp_state = PVRDMA_QPS_ERR;

    /* Get pointer to send & recv rings */
    dir = pvrdma_pci_dma_map(pci_dev, qp->init_args.pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        pr_err("Fail to map to QP page directory\n");
        rc = -ENOMEM;
        goto out_free_qp;
    }
    tbl = pvrdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        pr_err("Fail to map to QP page table\n");
        rc = -ENOMEM;
        goto out_free_qp;
    }

    /* Send ring */
    qp->sq_ring_state = (struct pvrdma_ring *)
            pvrdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!qp->sq_ring_state) {
        pr_err("Fail to map to QP header page\n");
        rc = -ENOMEM;
        goto out_free_qp;
    }

    wqe_size = roundup_pow_of_two(sizeof(struct pvrdma_sq_wqe_hdr) +
                                  sizeof(struct pvrdma_sge) *
                                  qp->init_args.max_send_sge);
    sprintf(ring_name, "qp%d_sq", resp->qpn);
    rc = ring_init(&qp->sq, ring_name, pci_dev, qp->sq_ring_state,
                   qp->init_args.max_send_wr, wqe_size,
                   (dma_addr_t *)&tbl[1], cmd->send_chunks);
    if (rc != 0) {
        pr_err("Fail to initialize SQ ring\n");
        rc = -ENOMEM;
        goto out_free_ring_state;
    }

    /* Recv ring */
    qp->rq_ring_state = &qp->sq_ring_state[1];
    wqe_size = roundup_pow_of_two(sizeof(struct pvrdma_rq_wqe_hdr) +
                                  sizeof(struct pvrdma_sge) *
                                  qp->init_args.max_recv_sge);
    pr_dbg("wqe_size=%d\n", wqe_size);
    pr_dbg("pvrdma_rq_wqe_hdr=%ld\n", sizeof(struct pvrdma_rq_wqe_hdr));
    pr_dbg("pvrdma_sge=%ld\n", sizeof(struct pvrdma_sge));
    pr_dbg("init_args.max_recv_sge=%d\n", qp->init_args.max_recv_sge);
    sprintf(ring_name, "qp%d_rq", resp->qpn);
    rc = ring_init(&qp->rq, ring_name, pci_dev, qp->rq_ring_state,
                   qp->init_args.max_recv_wr, wqe_size,
                   (dma_addr_t *)&tbl[2], cmd->total_chunks -
                   cmd->send_chunks - 1 /* first page is ring state */);
    if (rc != 0) {
        pr_err("Fail to initialize RQ ring\n");
        rc = -ENOMEM;
        goto out_free_send_ring;
    }

    resp->max_send_wr = cmd->max_send_wr;
    resp->max_recv_wr = cmd->max_recv_wr;
    resp->max_send_sge = cmd->max_send_sge;
    resp->max_recv_sge = cmd->max_recv_sge;
    resp->max_inline_data = cmd->max_inline_data;

    goto out;

out_free_send_ring:
    ring_free(&qp->sq);

out_free_ring_state:
    pvrdma_pci_dma_unmap(pci_dev, qp->sq_ring_state, TARGET_PAGE_SIZE);

out_free_qp:
    rm_dealloc_qp(dev, resp->qpn);

out:
    if (tbl) {
        pvrdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);
    }
    if (dir) {
        pvrdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);
    }

    return rc;
}

int rm_modify_qp(PVRDMADev *dev, __u32 qp_handle,
                 struct pvrdma_cmd_modify_qp *modify_qp_args)
{
    RmQP *qp;

    pr_dbg("qp_handle=%d\n", qp_handle);
    pr_dbg("new_state=%d\n", modify_qp_args->attrs.qp_state);

    qp = res_tbl_get(&dev->qp_tbl, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    pr_dbg("qp_type=%d\n", qp->init_args.qp_type);

    if (modify_qp_args->attr_mask & PVRDMA_QP_PORT) {
        qp->port_num = modify_qp_args->attrs.port_num - 1;
    }
    if (modify_qp_args->attr_mask & PVRDMA_QP_DEST_QPN) {
        qp->dest_qp_num = modify_qp_args->attrs.dest_qp_num;
    }
    if (modify_qp_args->attr_mask & PVRDMA_QP_AV) {
        qp->dgid = modify_qp_args->attrs.ah_attr.grh.dgid;
        qp->port_num = modify_qp_args->attrs.ah_attr.port_num - 1;
    }
    if (modify_qp_args->attr_mask & PVRDMA_QP_STATE) {
        qp->qp_state = modify_qp_args->attrs.qp_state;
    }

    /* kdbr connection */
    if (qp->qp_state == PVRDMA_QPS_RTR) {
        qp->kdbr_connection_id =
            kdbr_open_connection(dev->ports[qp->port_num].kdbr_port,
                                 qp_handle, qp->dgid, qp->dest_qp_num,
                                 qp->init_args.qp_type == PVRDMA_QPT_RC);
        if (qp->kdbr_connection_id == 0) {
            return -EIO;
        }
    }

    return 0;
}

void rm_dealloc_qp(PVRDMADev *dev, __u32 qp_handle)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    RmQP *qp;

    qp = res_tbl_get(&dev->qp_tbl, qp_handle);
    if (!qp) {
        return;
    }

    if (qp->kdbr_connection_id) {
        kdbr_close_connection(dev->ports[qp->port_num].kdbr_port,
                              qp->kdbr_connection_id);
    }

    ring_free(&qp->rq);
    ring_free(&qp->sq);

    pvrdma_pci_dma_unmap(pci_dev, qp->sq_ring_state, TARGET_PAGE_SIZE);

    res_tbl_dealloc(&dev->qp_tbl, qp_handle);
}

RmQP *rm_get_qp(PVRDMADev *dev, __u32 qp_handle)
{
    return res_tbl_get(&dev->qp_tbl, qp_handle);
}

void *rm_get_wqe_ctx(PVRDMADev *dev, unsigned long wqe_ctx_id)
{
    void **wqe_ctx;

    wqe_ctx = res_tbl_get(&dev->wqe_ctx_tbl, wqe_ctx_id);
    if (!wqe_ctx) {
        return NULL;
    }

    pr_dbg("ctx=%p\n", *wqe_ctx);

    return *wqe_ctx;
}

int rm_alloc_wqe_ctx(PVRDMADev *dev, unsigned long *wqe_ctx_id, void *ctx)
{
    void **wqe_ctx;

    wqe_ctx = res_tbl_alloc(&dev->wqe_ctx_tbl, (u32 *)wqe_ctx_id);
    if (!wqe_ctx) {
        return -ENOMEM;
    }

    pr_dbg("ctx=%p\n", ctx);
    *wqe_ctx = ctx;

    return 0;
}

void rm_dealloc_wqe_ctx(PVRDMADev *dev, unsigned long wqe_ctx_id)
{
    res_tbl_dealloc(&dev->wqe_ctx_tbl, (u32) wqe_ctx_id);
}

int rm_init(PVRDMADev *dev)
{
    int ret = 0;

    ret = res_tbl_init("PD", &dev->pd_tbl, MAX_PDS, sizeof(RmPD));
    if (ret != 0) {
        goto cln_pds;
    }

    ret = res_tbl_init("CQ", &dev->cq_tbl, MAX_CQS, sizeof(RmCQ));
    if (ret != 0) {
        goto cln_cqs;
    }

    ret = res_tbl_init("MR", &dev->mr_tbl, MAX_MRS, sizeof(RmMR));
    if (ret != 0) {
        goto cln_mrs;
    }

    ret = res_tbl_init("QP", &dev->qp_tbl, MAX_QPS, sizeof(RmQP));
    if (ret != 0) {
        goto cln_qps;
    }

    ret = res_tbl_init("WQE_CTX", &dev->wqe_ctx_tbl, MAX_QPS * MAX_QP_WRS,
               sizeof(void *));
    if (ret != 0) {
        goto cln_wqe_ctxs;
    }

    goto out;

cln_wqe_ctxs:
    res_tbl_free(&dev->wqe_ctx_tbl);

cln_qps:
    res_tbl_free(&dev->qp_tbl);

cln_mrs:
    res_tbl_free(&dev->mr_tbl);

cln_cqs:
    res_tbl_free(&dev->cq_tbl);

cln_pds:
    res_tbl_free(&dev->pd_tbl);

out:
    if (ret != 0) {
        pr_err("Fail to initialize RM\n");
    }

    return ret;
}

void rm_fini(PVRDMADev *dev)
{
    res_tbl_free(&dev->pd_tbl);
    res_tbl_free(&dev->cq_tbl);
    res_tbl_free(&dev->mr_tbl);
    res_tbl_free(&dev->qp_tbl);
    res_tbl_free(&dev->wqe_ctx_tbl);
}
