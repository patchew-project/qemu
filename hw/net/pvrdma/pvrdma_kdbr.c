#include <qemu/osdep.h>
#include <hw/pci/pci.h>

#include <sys/ioctl.h>

#include <hw/net/pvrdma/pvrdma.h>
#include <hw/net/pvrdma/pvrdma_ib_verbs.h>
#include <hw/net/pvrdma/pvrdma_rm.h>
#include <hw/net/pvrdma/pvrdma_kdbr.h>
#include <hw/net/pvrdma/pvrdma_utils.h>
#include <hw/net/pvrdma/kdbr.h>

int kdbr_fd = -1;

#define MAX_CONSEQ_CQES_READ 10

typedef struct KdbrCtx {
    struct kdbr_req req;
    void *up_ctx;
    bool is_tx_req;
} KdbrCtx;

static void (*tx_comp_handler)(int status, unsigned int vendor_err,
                               void *ctx) = 0;
static void (*rx_comp_handler)(int status, unsigned int vendor_err,
                               void *ctx) = 0;

static void kdbr_err_to_pvrdma_err(int kdbr_status, unsigned int *status,
                                   unsigned int *vendor_err)
{
    if (kdbr_status == 0) {
        *status = IB_WC_SUCCESS;
        *vendor_err = 0;
        return;
    }

    *vendor_err = kdbr_status;
    switch (kdbr_status) {
    case KDBR_ERR_CODE_EMPTY_VEC:
        *status = IB_WC_LOC_LEN_ERR;
        break;
    case KDBR_ERR_CODE_NO_MORE_RECV_BUF:
        *status = IB_WC_REM_OP_ERR;
        break;
    case KDBR_ERR_CODE_RECV_BUF_PROT:
        *status = IB_WC_REM_ACCESS_ERR;
        break;
    case KDBR_ERR_CODE_INV_ADDR:
        *status = IB_WC_LOC_ACCESS_ERR;
        break;
    case KDBR_ERR_CODE_INV_CONN_ID:
        *status = IB_WC_LOC_PROT_ERR;
        break;
    case KDBR_ERR_CODE_NO_PEER:
        *status = IB_WC_LOC_QP_OP_ERR;
        break;
    default:
        *status = IB_WC_GENERAL_ERR;
        break;
    }
}

static void *comp_handler_thread(void *arg)
{
    KdbrPort *port = (KdbrPort *)arg;
    struct kdbr_completion comp[MAX_CONSEQ_CQES_READ];
    int i, j, rc;
    KdbrCtx *sctx;
    unsigned int status, vendor_err;

    while (port->comp_thread.run) {
        rc = read(port->fd, &comp, sizeof(comp));
        if (unlikely(rc % sizeof(struct kdbr_completion))) {
            pr_err("Got unsupported message size (%d) from kdbr\n", rc);
            continue;
        }
        pr_dbg("Processing %ld CQEs from kdbr\n",
               rc / sizeof(struct kdbr_completion));

        for (i = 0; i < rc / sizeof(struct kdbr_completion); i++) {
            pr_dbg("comp.req_id=%ld\n", comp[i].req_id);
            pr_dbg("comp.status=%d\n", comp[i].status);

            sctx = rm_get_wqe_ctx(PVRDMA_DEV(port->dev), comp[i].req_id);
            if (!sctx) {
                pr_err("Fail to find ctx for req %ld\n", comp[i].req_id);
                continue;
            }
            pr_dbg("Processing %s CQE\n", sctx->is_tx_req ? "send" : "recv");

            for (j = 0; j < sctx->req.vlen; j++) {
                pr_dbg("payload=%s\n", (char *)sctx->req.vec[j].iov_base);
                pvrdma_pci_dma_unmap(port->dev, sctx->req.vec[j].iov_base,
                                     sctx->req.vec[j].iov_len);
            }

            kdbr_err_to_pvrdma_err(comp[i].status, &status, &vendor_err);
            pr_dbg("status=%d\n", status);
            pr_dbg("vendor_err=0x%x\n", vendor_err);

            if (sctx->is_tx_req) {
                tx_comp_handler(status, vendor_err, sctx->up_ctx);
            } else {
                rx_comp_handler(status, vendor_err, sctx->up_ctx);
            }

            rm_dealloc_wqe_ctx(PVRDMA_DEV(port->dev), comp[i].req_id);
            free(sctx);
        }
    }

    pr_dbg("Going down\n");

    return NULL;
}

KdbrPort *kdbr_alloc_port(PVRDMADev *dev)
{
    int rc;
    KdbrPort *port;
    char name[80] = {0};
    struct kdbr_reg reg;

    port = malloc(sizeof(KdbrPort));
    if (!port) {
        pr_dbg("Fail to allocate memory for port object\n");
        return NULL;
    }

    port->dev = PCI_DEVICE(dev);

    pr_dbg("net=0x%llx\n", dev->ports[0].gid_tbl[0].global.subnet_prefix);
    pr_dbg("guid=0x%llx\n", dev->ports[0].gid_tbl[0].global.interface_id);
    reg.gid.net_id = dev->ports[0].gid_tbl[0].global.subnet_prefix;
    reg.gid.id = dev->ports[0].gid_tbl[0].global.interface_id;
    rc = ioctl(kdbr_fd, KDBR_REGISTER_PORT, &reg);
    if (rc < 0) {
        pr_err("Fail to allocate port\n");
        goto err_free_port;
    }

    port->num = reg.port;

    sprintf(name, KDBR_FILE_NAME "%d", port->num);
    port->fd = open(name, O_RDWR);
    if (port->fd < 0) {
        pr_err("Fail to open file %s\n", name);
        goto err_unregister_device;
    }

    sprintf(name, "pvrdma_comp_%d", port->num);
    port->comp_thread.run = true;
    qemu_thread_create(&port->comp_thread.thread, name, comp_handler_thread,
                       port, QEMU_THREAD_DETACHED);

    pr_info("Port %d (fd %d) allocated\n", port->num, port->fd);

    return port;

err_unregister_device:
    ioctl(kdbr_fd, KDBR_UNREGISTER_PORT, &port->num);

err_free_port:
    free(port);

    return NULL;
}

void kdbr_free_port(KdbrPort *port)
{
    int rc;

    if (!port) {
        return;
    }

    rc = write(port->fd, (char *)0, 1);
    port->comp_thread.run = false;
    close(port->fd);

    rc = ioctl(kdbr_fd, KDBR_UNREGISTER_PORT, &port->num);
    if (rc < 0) {
        pr_err("Fail to allocate port\n");
    }

    free(port);
}

unsigned long kdbr_open_connection(KdbrPort *port, u32 qpn,
                                   union pvrdma_gid dgid, u32 dqpn, bool rc_qp)
{
    int rc;
    struct kdbr_connection connection = {0};

    connection.queue_id = qpn;
    connection.peer.rgid.net_id = dgid.global.subnet_prefix;
    connection.peer.rgid.id = dgid.global.interface_id;
    connection.peer.rqueue = dqpn;
    connection.ack_type = rc_qp ? KDBR_ACK_DELAYED : KDBR_ACK_IMMEDIATE;

    rc = ioctl(port->fd, KDBR_PORT_OPEN_CONN, &connection);
    if (rc <= 0) {
        pr_err("Fail to open kdbr connection on port %d fd %d err %d\n",
               port->num, port->fd, rc);
        return 0;
    }

    return (unsigned long)rc;
}

void kdbr_close_connection(KdbrPort *port, unsigned long connection_id)
{
    int rc;

    rc = ioctl(port->fd, KDBR_PORT_CLOSE_CONN, &connection_id);
    if (rc < 0) {
        pr_err("Fail to close kdbr connection on port %d\n",
               port->num);
    }
}

void kdbr_register_tx_comp_handler(void (*comp_handler)(int status,
                                   unsigned int vendor_err, void *ctx))
{
    tx_comp_handler = comp_handler;
}

void kdbr_register_rx_comp_handler(void (*comp_handler)(int status,
                                   unsigned int vendor_err, void *ctx))
{
    rx_comp_handler = comp_handler;
}

void kdbr_send_wqe(KdbrPort *port, unsigned long connection_id, bool rc_qp,
                   struct RmSqWqe *wqe, void *ctx)
{
    KdbrCtx *sctx;
    int rc;
    int i;

    pr_dbg("kdbr_port=%d\n", port->num);
    pr_dbg("kdbr_connection_id=%ld\n", connection_id);
    pr_dbg("wqe->hdr.num_sge=%d\n", wqe->hdr.num_sge);

    /* Last minute validation - verify that kdbr supports num_sge */
    /* TODO: Make sure this will not happen! */
    if (wqe->hdr.num_sge > KDBR_MAX_IOVEC_LEN) {
        pr_err("Error: requested %d SGEs where kdbr supports %d\n",
               wqe->hdr.num_sge, KDBR_MAX_IOVEC_LEN);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_TOO_MANY_SGES, ctx);
        return;
    }

    sctx = malloc(sizeof(*sctx));
    if (!sctx) {
        pr_err("Fail to allocate kdbr request ctx\n");
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
    }

    memset(&sctx->req, 0, sizeof(sctx->req));
    sctx->req.flags = KDBR_REQ_SIGNATURE | KDBR_REQ_POST_SEND;
    sctx->req.connection_id = connection_id;

    sctx->up_ctx = ctx;
    sctx->is_tx_req = 1;

    rc = rm_alloc_wqe_ctx(PVRDMA_DEV(port->dev), &sctx->req.req_id, sctx);
    if (rc != 0) {
        pr_err("Fail to allocate request ID\n");
        free(sctx);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        return;
    }
    sctx->req.vlen = wqe->hdr.num_sge;

    for (i = 0; i < wqe->hdr.num_sge; i++) {
        struct pvrdma_sge *sge;

        sge = &wqe->sge[i];

        pr_dbg("addr=0x%llx\n", sge->addr);
        pr_dbg("length=%d\n", sge->length);
        pr_dbg("lkey=0x%x\n", sge->lkey);

        sctx->req.vec[i].iov_base = pvrdma_pci_dma_map(port->dev, sge->addr,
                                                       sge->length);
        sctx->req.vec[i].iov_len = sge->length;
    }

    if (!rc_qp) {
        sctx->req.peer.rqueue = wqe->hdr.wr.ud.remote_qpn;
        sctx->req.peer.rgid.net_id = *((unsigned long *)
                        &wqe->hdr.wr.ud.av.dgid[0]);
        sctx->req.peer.rgid.id = *((unsigned long *)
                        &wqe->hdr.wr.ud.av.dgid[8]);
    }

    rc = write(port->fd, &sctx->req, sizeof(sctx->req));
    if (rc < 0) {
        pr_err("Fail (%d, %d) to post send WQE to port %d, conn_id %ld\n", rc,
               errno, port->num, connection_id);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_FAIL_KDBR, ctx);
        return;
    }
}

void kdbr_recv_wqe(KdbrPort *port, unsigned long connection_id,
                   struct RmRqWqe *wqe, void *ctx)
{
    KdbrCtx *sctx;
    int rc;
    int i;

    pr_dbg("kdbr_port=%d\n", port->num);
    pr_dbg("kdbr_connection_id=%ld\n", connection_id);
    pr_dbg("wqe->hdr.num_sge=%d\n", wqe->hdr.num_sge);

    /* Last minute validation - verify that kdbr supports num_sge */
    if (wqe->hdr.num_sge > KDBR_MAX_IOVEC_LEN) {
        pr_err("Error: requested %d SGEs where kdbr supports %d\n",
               wqe->hdr.num_sge, KDBR_MAX_IOVEC_LEN);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_TOO_MANY_SGES, ctx);
        return;
    }

    sctx = malloc(sizeof(*sctx));
    if (!sctx) {
        pr_err("Fail to allocate kdbr request ctx\n");
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
    }

    memset(&sctx->req, 0, sizeof(sctx->req));
    sctx->req.flags = KDBR_REQ_SIGNATURE | KDBR_REQ_POST_RECV;
    sctx->req.connection_id = connection_id;

    sctx->up_ctx = ctx;
    sctx->is_tx_req = 0;

    pr_dbg("sctx=%p\n", sctx);
    rc = rm_alloc_wqe_ctx(PVRDMA_DEV(port->dev), &sctx->req.req_id, sctx);
    if (rc != 0) {
        pr_err("Fail to allocate request ID\n");
        free(sctx);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_NOMEM, ctx);
        return;
    }

    sctx->req.vlen = wqe->hdr.num_sge;

    for (i = 0; i < wqe->hdr.num_sge; i++) {
        struct pvrdma_sge *sge;

        sge = &wqe->sge[i];

        pr_dbg("addr=0x%llx\n", sge->addr);
        pr_dbg("length=%d\n", sge->length);
        pr_dbg("lkey=0x%x\n", sge->lkey);

        sctx->req.vec[i].iov_base = pvrdma_pci_dma_map(port->dev, sge->addr,
                                                       sge->length);
        sctx->req.vec[i].iov_len = sge->length;
    }

    rc = write(port->fd, &sctx->req, sizeof(sctx->req));
    if (rc < 0) {
        pr_err("Fail (%d, %d) to post recv WQE to port %d, conn_id %ld\n", rc,
               errno, port->num, connection_id);
        tx_comp_handler(IB_WC_GENERAL_ERR, VENDOR_ERR_FAIL_KDBR, ctx);
        return;
    }
}

static void dummy_comp_handler(int status, unsigned int vendor_err, void *ctx)
{
    pr_err("No completion handler is registered\n");
}

int kdbr_init(void)
{
    kdbr_register_tx_comp_handler(dummy_comp_handler);
    kdbr_register_rx_comp_handler(dummy_comp_handler);

    kdbr_fd = open(KDBR_FILE_NAME, 0);
    if (kdbr_fd < 0) {
        pr_dbg("Can't connect to kdbr, rc=%d\n", kdbr_fd);
        return -EIO;
    }

    return 0;
}

void kdbr_fini(void)
{
    close(kdbr_fd);
}
