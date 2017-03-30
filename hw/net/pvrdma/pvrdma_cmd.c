#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/net/pvrdma/pvrdma_utils.h"
#include "hw/net/pvrdma/pvrdma.h"
#include "hw/net/pvrdma/pvrdma_rm.h"
#include "hw/net/pvrdma/pvrdma_kdbr.h"

static int query_port(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_port *cmd = &req->query_port;
    struct pvrdma_cmd_query_port_resp *resp = &rsp->query_port_resp;
    __u32 max_port_gids, max_port_pkeys;

    pr_dbg("port=%d\n", cmd->port_num);

    if (rm_get_max_port_gids(&max_port_gids) != 0) {
        return -ENOMEM;
    }

    if (rm_get_max_port_pkeys(&max_port_pkeys) != 0) {
        return -ENOMEM;
    }

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_QUERY_PORT_RESP;
    resp->hdr.err = 0;

    resp->attrs.state = PVRDMA_PORT_ACTIVE;
    resp->attrs.max_mtu = PVRDMA_MTU_4096;
    resp->attrs.active_mtu = PVRDMA_MTU_4096;
    resp->attrs.gid_tbl_len = max_port_gids;
    resp->attrs.port_cap_flags = 0;
    resp->attrs.max_msg_sz = 1024;
    resp->attrs.bad_pkey_cntr = 0;
    resp->attrs.qkey_viol_cntr = 0;
    resp->attrs.pkey_tbl_len = max_port_pkeys;
    resp->attrs.lid = 0;
    resp->attrs.sm_lid = 0;
    resp->attrs.lmc = 0;
    resp->attrs.max_vl_num = 0;
    resp->attrs.sm_sl = 0;
    resp->attrs.subnet_timeout = 0;
    resp->attrs.init_type_reply = 0;
    resp->attrs.active_width = 1;
    resp->attrs.active_speed = 1;
    resp->attrs.phys_state = 1;

    return 0;
}

static int query_pkey(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_query_pkey *cmd = &req->query_pkey;
    struct pvrdma_cmd_query_pkey_resp *resp = &rsp->query_pkey_resp;

    pr_dbg("port=%d\n", cmd->port_num);
    pr_dbg("index=%d\n", cmd->index);

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_QUERY_PKEY_RESP;
    resp->hdr.err = 0;

    resp->pkey = 0x7FFF;
    pr_dbg("pkey=0x%x\n", resp->pkey);

    return 0;
}

static int create_pd(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_pd *cmd = &req->create_pd;
    struct pvrdma_cmd_create_pd_resp *resp = &rsp->create_pd_resp;

    pr_dbg("context=0x%x\n", cmd->ctx_handle ? cmd->ctx_handle : 0);

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_CREATE_PD_RESP;
    resp->hdr.err = rm_alloc_pd(dev, &resp->pd_handle, cmd->ctx_handle);

    pr_dbg("ret=%d\n", resp->hdr.err);
    return resp->hdr.err;
}

static int destroy_pd(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_pd *cmd = &req->destroy_pd;

    pr_dbg("pd_handle=%d\n", cmd->pd_handle);

    rm_dealloc_pd(dev, cmd->pd_handle);

    return 0;
}

static int create_mr(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_mr *cmd = &req->create_mr;
    struct pvrdma_cmd_create_mr_resp *resp = &rsp->create_mr_resp;

    pr_dbg("pd_handle=%d\n", cmd->pd_handle);
    pr_dbg("access_flags=0x%x\n", cmd->access_flags);
    pr_dbg("flags=0x%x\n", cmd->flags);

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_CREATE_MR_RESP;
    resp->hdr.err = rm_alloc_mr(dev, cmd, resp);

    pr_dbg("ret=%d\n", resp->hdr.err);
    return resp->hdr.err;
}

static int destroy_mr(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_mr *cmd = &req->destroy_mr;

    pr_dbg("mr_handle=%d\n", cmd->mr_handle);

    rm_dealloc_mr(dev, cmd->mr_handle);

    return 0;
}

static int create_cq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_cq *cmd = &req->create_cq;
    struct pvrdma_cmd_create_cq_resp *resp = &rsp->create_cq_resp;

    pr_dbg("pdir_dma=0x%llx\n", (long long unsigned int)cmd->pdir_dma);
    pr_dbg("context=0x%x\n", cmd->ctx_handle ? cmd->ctx_handle : 0);
    pr_dbg("cqe=%d\n", cmd->cqe);
    pr_dbg("nchunks=%d\n", cmd->nchunks);

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_CREATE_CQ_RESP;
    resp->hdr.err = rm_alloc_cq(dev, cmd, resp);

    pr_dbg("ret=%d\n", resp->hdr.err);
    return resp->hdr.err;
}

static int destroy_cq(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_cq *cmd = &req->destroy_cq;

    pr_dbg("cq_handle=%d\n", cmd->cq_handle);

    rm_dealloc_cq(dev, cmd->cq_handle);

    return 0;
}

static int create_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_create_qp *cmd = &req->create_qp;
    struct pvrdma_cmd_create_qp_resp *resp = &rsp->create_qp_resp;

    if (!dev->ports[0].kdbr_port) {
        pr_dbg("First QP, registering port 0\n");
        dev->ports[0].kdbr_port = kdbr_alloc_port(dev);
        if (!dev->ports[0].kdbr_port) {
            pr_dbg("Fail to register port\n");
            return -EIO;
        }
    }

    pr_dbg("pd_handle=%d\n", cmd->pd_handle);
    pr_dbg("pdir_dma=0x%llx\n", (long long unsigned int)cmd->pdir_dma);
    pr_dbg("total_chunks=%d\n", cmd->total_chunks);
    pr_dbg("send_chunks=%d\n", cmd->send_chunks);

    memset(resp, 0, sizeof(*resp));
    resp->hdr.response = cmd->hdr.response;
    resp->hdr.ack = PVRDMA_CMD_CREATE_QP_RESP;
    resp->hdr.err = rm_alloc_qp(dev, cmd, resp);

    pr_dbg("ret=%d\n", resp->hdr.err);
    return resp->hdr.err;
}

static int modify_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                     union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_modify_qp *cmd = &req->modify_qp;

    pr_dbg("qp_handle=%d\n", cmd->qp_handle);

    memset(rsp, 0, sizeof(*rsp));
    rsp->hdr.response = cmd->hdr.response;
    rsp->hdr.ack = PVRDMA_CMD_MODIFY_QP_RESP;
    rsp->hdr.err = rm_modify_qp(dev, cmd->qp_handle, cmd);

    pr_dbg("ret=%d\n", rsp->hdr.err);
    return rsp->hdr.err;
}

static int destroy_qp(PVRDMADev *dev, union pvrdma_cmd_req *req,
                      union pvrdma_cmd_resp *rsp)
{
    struct pvrdma_cmd_destroy_qp *cmd = &req->destroy_qp;

    pr_dbg("qp_handle=%d\n", cmd->qp_handle);

    rm_dealloc_qp(dev, cmd->qp_handle);

    return 0;
}

static int create_bind(PVRDMADev *dev, union pvrdma_cmd_req *req,
                       union pvrdma_cmd_resp *rsp)
{
    int rc;
    struct pvrdma_cmd_create_bind *cmd = &req->create_bind;
    u32 max_port_gids;
#ifdef DEBUG
    __be64 *subnet = (__be64 *)&cmd->new_gid[0];
    __be64 *if_id = (__be64 *)&cmd->new_gid[8];
#endif

    pr_dbg("index=%d\n", cmd->index);

    rc = rm_get_max_port_gids(&max_port_gids);
    if (rc) {
        return -EIO;
    }

    if (cmd->index > max_port_gids) {
        return -EINVAL;
    }

    pr_dbg("gid[%d]=0x%llx,0x%llx\n", cmd->index, *subnet, *if_id);

    /* Driver forces to one port only */
    memcpy(dev->ports[0].gid_tbl[cmd->index].raw, &cmd->new_gid,
           sizeof(cmd->new_gid));

    return 0;
}

static int destroy_bind(PVRDMADev *dev, union pvrdma_cmd_req *req,
                        union pvrdma_cmd_resp *rsp)
{
    /*  TODO: Check the usage of this table */

    struct pvrdma_cmd_destroy_bind *cmd = &req->destroy_bind;

    pr_dbg("clear index %d\n", cmd->index);

    memset(dev->ports[0].gid_tbl[cmd->index].raw, 0,
           sizeof(dev->ports[0].gid_tbl[cmd->index].raw));

    return 0;
}

struct cmd_handler {
    __u32 cmd;
    int (*exec)(PVRDMADev *dev, union pvrdma_cmd_req *req,
            union pvrdma_cmd_resp *rsp);
};

static struct cmd_handler cmd_handlers[] = {
    {PVRDMA_CMD_QUERY_PORT, query_port},
    {PVRDMA_CMD_QUERY_PKEY, query_pkey},
    {PVRDMA_CMD_CREATE_PD, create_pd},
    {PVRDMA_CMD_DESTROY_PD, destroy_pd},
    {PVRDMA_CMD_CREATE_MR, create_mr},
    {PVRDMA_CMD_DESTROY_MR, destroy_mr},
    {PVRDMA_CMD_CREATE_CQ, create_cq},
    {PVRDMA_CMD_RESIZE_CQ, NULL},
    {PVRDMA_CMD_DESTROY_CQ, destroy_cq},
    {PVRDMA_CMD_CREATE_QP, create_qp},
    {PVRDMA_CMD_MODIFY_QP, modify_qp},
    {PVRDMA_CMD_QUERY_QP, NULL},
    {PVRDMA_CMD_DESTROY_QP, destroy_qp},
    {PVRDMA_CMD_CREATE_UC, NULL},
    {PVRDMA_CMD_DESTROY_UC, NULL},
    {PVRDMA_CMD_CREATE_BIND, create_bind},
    {PVRDMA_CMD_DESTROY_BIND, destroy_bind},
};

int execute_command(PVRDMADev *dev)
{
    int err = 0xFFFF;
    DSRInfo *dsr_info;

    dsr_info = &dev->dsr_info;

    pr_dbg("cmd=%d\n", dsr_info->req->hdr.cmd);
    if (dsr_info->req->hdr.cmd >= sizeof(cmd_handlers) /
                      sizeof(struct cmd_handler)) {
        pr_err("Unsupported command\n");
        goto out;
    }

    if (!cmd_handlers[dsr_info->req->hdr.cmd].exec) {
        pr_err("Unsupported command (not implemented yet)\n");
        goto out;
    }

    err = cmd_handlers[dsr_info->req->hdr.cmd].exec(dev, dsr_info->req,
                            dsr_info->rsp);
out:
    set_reg_val(dev, PVRDMA_REG_ERR, err);
    post_interrupt(dev, INTR_VEC_CMD_RING);

    return (err == 0) ? 0 : -EINVAL;
}
