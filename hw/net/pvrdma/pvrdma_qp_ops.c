#include "hw/net/pvrdma/pvrdma.h"
#include "hw/net/pvrdma/pvrdma_utils.h"
#include "hw/net/pvrdma/pvrdma_qp_ops.h"
#include "hw/net/pvrdma/pvrdma_rm.h"
#include "hw/net/pvrdma/pvrdma-uapi.h"
#include "hw/net/pvrdma/pvrdma_kdbr.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"

typedef struct CompHandlerCtx {
    PVRDMADev *dev;
    u32 cq_handle;
    struct pvrdma_cqe cqe;
} CompHandlerCtx;

/*
 * 1. Put CQE on send CQ ring
 * 2. Put CQ number on dsr completion ring
 * 3. Interrupt host
 */
static int post_cqe(PVRDMADev *dev, u32 cq_handle, struct pvrdma_cqe *cqe)
{
    struct pvrdma_cqe *cqe1;
    struct pvrdma_cqne *cqne;
    RmCQ *cq = rm_get_cq(dev, cq_handle);

    if (!cq) {
        pr_dbg("Invalid cqn %d\n", cq_handle);
        return -EINVAL;
    }

    pr_dbg("cq->comp_type=%d\n", cq->comp_type);
    if (cq->comp_type == CCT_NONE) {
        return 0;
    }
    cq->comp_type = CCT_NONE;

    /* Step #1: Put CQE on CQ ring */
    pr_dbg("Writing CQE\n");
    cqe1 = ring_next_elem_write(&cq->cq);
    if (!cqe1) {
        return -EINVAL;
    }

    memcpy(cqe1, cqe, sizeof(*cqe));
    ring_write_inc(&cq->cq);

    /* Step #2: Put CQ number on dsr completion ring */
    pr_dbg("Writing CQNE\n");
    cqne = ring_next_elem_write(&dev->dsr_info.cq);
    if (!cqne) {
        return -EINVAL;
    }

    cqne->info = cq_handle;
    ring_write_inc(&dev->dsr_info.cq);

    post_interrupt(dev, INTR_VEC_CMD_COMPLETION_Q);

    return 0;
}

static void qp_ops_comp_handler(int status, unsigned int vendor_err, void *ctx)
{
    CompHandlerCtx *comp_ctx = (CompHandlerCtx *)ctx;

    pr_dbg("cq_handle=%d\n", comp_ctx->cq_handle);
    pr_dbg("wr_id=%lld\n", comp_ctx->cqe.wr_id);
    pr_dbg("status=%d\n", status);
    pr_dbg("vendor_err=0x%x\n", vendor_err);
    comp_ctx->cqe.status = status;
    comp_ctx->cqe.vendor_err = vendor_err;
    post_cqe(comp_ctx->dev, comp_ctx->cq_handle, &comp_ctx->cqe);
    free(ctx);
}

void qp_ops_fini(void)
{
}

int qp_ops_init(void)
{
    kdbr_register_tx_comp_handler(qp_ops_comp_handler);
    kdbr_register_rx_comp_handler(qp_ops_comp_handler);

    return 0;
}

int qp_send(PVRDMADev *dev, __u32 qp_handle)
{
    RmQP *qp;
    RmSqWqe *wqe;

    qp = rm_get_qp(dev, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    if (qp->qp_state < PVRDMA_QPS_RTS) {
        pr_dbg("Invalid QP state for send\n");
        return -EINVAL;
    }

    wqe = (struct RmSqWqe *)ring_next_elem_read(&qp->sq);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        pr_dbg("wr_id=%lld\n", wqe->hdr.wr_id);
        wqe->hdr.num_sge = MIN(wqe->hdr.num_sge,
                       qp->init_args.max_send_sge);

        /* Prepare CQE */
        comp_ctx = malloc(sizeof(CompHandlerCtx));
        comp_ctx->dev = dev;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cq_handle = qp->init_args.send_cq_handle;
        comp_ctx->cqe.opcode = wqe->hdr.opcode;
        /* TODO: Fill rest of the data */

        kdbr_send_wqe(dev->ports[qp->port_num].kdbr_port,
                      qp->kdbr_connection_id,
                      qp->init_args.qp_type == PVRDMA_QPT_RC, wqe, comp_ctx);

        ring_read_inc(&qp->sq);

        wqe = ring_next_elem_read(&qp->sq);
    }

    return 0;
}

int qp_recv(PVRDMADev *dev, __u32 qp_handle)
{
    RmQP *qp;
    RmRqWqe *wqe;

    qp = rm_get_qp(dev, qp_handle);
    if (!qp) {
        return -EINVAL;
    }

    if (qp->qp_state < PVRDMA_QPS_RTR) {
        pr_dbg("Invalid QP state for receive\n");
        return -EINVAL;
    }

    wqe = (struct RmRqWqe *)ring_next_elem_read(&qp->rq);
    while (wqe) {
        CompHandlerCtx *comp_ctx;

        pr_dbg("wr_id=%lld\n", wqe->hdr.wr_id);
        wqe->hdr.num_sge = MIN(wqe->hdr.num_sge,
                       qp->init_args.max_send_sge);

        /* Prepare CQE */
        comp_ctx = malloc(sizeof(CompHandlerCtx));
        comp_ctx->dev = dev;
        comp_ctx->cqe.qp = qp_handle;
        comp_ctx->cq_handle = qp->init_args.recv_cq_handle;
        comp_ctx->cqe.wr_id = wqe->hdr.wr_id;
        comp_ctx->cqe.qp = qp_handle;
        /* TODO: Fill rest of the data */

        kdbr_recv_wqe(dev->ports[qp->port_num].kdbr_port,
                      qp->kdbr_connection_id, wqe, comp_ctx);

        ring_read_inc(&qp->rq);

        wqe = ring_next_elem_read(&qp->rq);
    }

    return 0;
}
