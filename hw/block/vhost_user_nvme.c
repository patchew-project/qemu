/*
 * QEMU NVM Express Controller
 *
 * Copyright (c) 2017, Intel Corporation
 *
 * Author:
 * Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work was largely based on QEMU NVMe driver implementation by:
 * Keith Busch <keith.busch@intel.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

/**
 * Reference Specs: http://www.nvmexpress.org, 1.2, 1.1, 1.0e
 *
 *  http://www.nvmexpress.org/resources/
 */

#include "qemu/osdep.h"
#include "hw/block/block.h"
#include "hw/hw.h"
#include "sysemu/kvm.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"

#include "nvme.h"
#include "vhost_user_nvme.h"

static int vhost_user_nvme_add_kvm_msi_virq(NvmeCtrl *n, NvmeCQueue *cq)
{
    int virq;
    int vector_n;

    if (!msix_enabled(&(n->parent_obj))) {
        error_report("MSIX is mandatory for the device");
        return -1;
    }

    if (event_notifier_init(&cq->guest_notifier, 0)) {
        error_report("Initiated guest notifier failed");
        return -1;
    }

    vector_n = cq->vector;

    virq = kvm_irqchip_add_msi_route(kvm_state, vector_n, &n->parent_obj);
    if (virq < 0) {
        error_report("Route MSIX vector to KVM failed");
        event_notifier_cleanup(&cq->guest_notifier);
        return -1;
    }

    if (kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, &cq->guest_notifier,
                                           NULL, virq) < 0) {
        kvm_irqchip_release_virq(kvm_state, virq);
        event_notifier_cleanup(&cq->guest_notifier);
        error_report("Add MSIX vector to KVM failed");
        return -1;
    }

    cq->virq = virq;
    return 0;
}

static void vhost_user_nvme_remove_kvm_msi_virq(NvmeCQueue *cq)
{
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, &cq->guest_notifier,
                                          cq->virq);
    kvm_irqchip_release_virq(kvm_state, cq->virq);
    event_notifier_cleanup(&cq->guest_notifier);
    cq->virq = -1;
}

static int nvme_check_sqid(NvmeCtrl *n, uint16_t sqid)
{
    if (sqid < n->num_io_queues + 1) {
        return 0;
    }

    return 1;
}

static int nvme_check_cqid(NvmeCtrl *n, uint16_t cqid)
{
    if (cqid < n->num_io_queues + 1) {
        return 0;
    }

    return 1;
}

static void nvme_inc_cq_tail(NvmeCQueue *cq)
{
    cq->tail++;
    if (cq->tail >= cq->size) {
        cq->tail = 0;
        cq->phase = !cq->phase;
    }
}

static void nvme_inc_sq_head(NvmeSQueue *sq)
{
    sq->head = (sq->head + 1) % sq->size;
}

static uint8_t nvme_sq_empty(NvmeSQueue *sq)
{
    return sq->head == sq->tail;
}

static void nvme_isr_notify(NvmeCtrl *n, NvmeCQueue *cq)
{
    if (cq->irq_enabled) {
        if (msix_enabled(&(n->parent_obj))) {
            msix_notify(&(n->parent_obj), cq->vector);
        } else {
            pci_irq_pulse(&n->parent_obj);
        }
    }
}

static void nvme_free_sq(NvmeSQueue *sq, NvmeCtrl *n)
{
    n->sq[sq->sqid] = NULL;
    if (sq->sqid) {
        g_free(sq);
    }
}

static uint16_t nvme_del_sq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)cmd;
    NvmeSQueue *sq;
    NvmeCqe cqe;
    uint16_t qid = le16_to_cpu(c->qid);
    int ret;

    if (!qid || nvme_check_sqid(n, qid)) {
        error_report("nvme_del_sq: invalid qid %u", qid);
        return NVME_INVALID_QID | NVME_DNR;
    }

    sq = n->sq[qid];

    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_del_sq: delete sq failed");
        return -1;
    }

    nvme_free_sq(sq, n);
    return NVME_SUCCESS;
}

static void nvme_init_sq(NvmeSQueue *sq, NvmeCtrl *n, uint64_t dma_addr,
    uint16_t sqid, uint16_t cqid, uint16_t size)
{
    sq->ctrl = n;
    sq->dma_addr = dma_addr;
    sq->sqid = sqid;
    sq->size = size;
    sq->cqid = cqid;
    sq->head = sq->tail = 0;

    n->sq[sqid] = sq;
}

static uint16_t nvme_create_sq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeSQueue *sq;
    int ret;
    NvmeCqe cqe;
    NvmeCreateSq *c = (NvmeCreateSq *)cmd;

    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t sqid = le16_to_cpu(c->sqid);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->sq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    if (!cqid) {
        error_report("nvme_create_sq: invalid cqid %u", cqid);
        return NVME_INVALID_CQID | NVME_DNR;
    }
    if (!sqid || nvme_check_sqid(n, sqid)) {
        error_report("nvme_create_sq: invalid sqid");
        return NVME_INVALID_QID | NVME_DNR;
    }
    if (!qsize || qsize > NVME_CAP_MQES(n->bar.cap)) {
        error_report("nvme_create_sq: invalid qsize");
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (!prp1 || prp1 & (n->page_size - 1)) {
        error_report("nvme_create_sq: invalid prp1");
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (!(NVME_SQ_FLAGS_PC(qflags))) {
        error_report("nvme_create_sq: invalid flags");
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /* BIOS also create IO queue pair for same queue ID */
    if (n->sq[sqid] != NULL) {
        nvme_free_sq(n->sq[sqid], n);
    }

    sq = g_malloc0(sizeof(*sq));
    assert(sq != NULL);
    nvme_init_sq(sq, n, prp1, sqid, cqid, qsize + 1);
    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_create_sq: create sq failed");
        return -1;
    }
    return NVME_SUCCESS;
}

static void nvme_free_cq(NvmeCQueue *cq, NvmeCtrl *n)
{
    n->cq[cq->cqid] = NULL;
    msix_vector_unuse(&n->parent_obj, cq->vector);
    if (cq->cqid) {
        g_free(cq);
    }
}

static uint16_t nvme_del_cq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)cmd;
    NvmeCqe cqe;
    NvmeCQueue *cq;
    uint16_t qid = le16_to_cpu(c->qid);
    int ret;

    if (!qid || nvme_check_cqid(n, qid)) {
        error_report("nvme_del_cq: invalid qid %u", qid);
        return NVME_INVALID_CQID | NVME_DNR;
    }

    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_del_cq: delete cq failed");
        return -1;
    }

    cq = n->cq[qid];
    if (cq->irq_enabled) {
        vhost_user_nvme_remove_kvm_msi_virq(cq);
    }
    nvme_free_cq(cq, n);
    return NVME_SUCCESS;
}


static void nvme_init_cq(NvmeCQueue *cq, NvmeCtrl *n, uint64_t dma_addr,
    uint16_t cqid, uint16_t vector, uint16_t size, uint16_t irq_enabled)
{
    cq->ctrl = n;
    cq->cqid = cqid;
    cq->size = size;
    cq->dma_addr = dma_addr;
    cq->phase = 1;
    cq->irq_enabled = irq_enabled;
    cq->vector = vector;
    cq->head = cq->tail = 0;
    msix_vector_use(&n->parent_obj, cq->vector);
    n->cq[cqid] = cq;
}

static uint16_t nvme_create_cq(NvmeCtrl *n, NvmeCmd *cmd)
{
    int ret;
    NvmeCQueue *cq;
    NvmeCqe cqe;
    NvmeCreateCq *c = (NvmeCreateCq *)cmd;
    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t vector = le16_to_cpu(c->irq_vector);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->cq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    if (!cqid || nvme_check_cqid(n, cqid)) {
        error_report("nvme_create_cq: invalid cqid");
        return NVME_INVALID_CQID | NVME_DNR;
    }
    if (!qsize || qsize > NVME_CAP_MQES(n->bar.cap)) {
        error_report("nvme_create_cq: invalid qsize, qsize %u", qsize);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (!prp1) {
        error_report("nvme_create_cq: invalid prp1");
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (vector > n->num_io_queues + 1) {
        error_report("nvme_create_cq: invalid irq vector");
        return NVME_INVALID_IRQ_VECTOR | NVME_DNR;
    }
    if (!(NVME_CQ_FLAGS_PC(qflags))) {
        error_report("nvme_create_cq: invalid flags");
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /* BIOS also create IO queue pair for same queue ID */
    if (n->cq[cqid] != NULL) {
        nvme_free_cq(n->cq[cqid], n);
    }

    cq = g_malloc0(sizeof(*cq));
    assert(cq != NULL);
    nvme_init_cq(cq, n, prp1, cqid, vector, qsize + 1,
                 NVME_CQ_FLAGS_IEN(qflags));
    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_create_cq: create cq failed");
        return -1;
    }

    if (cq->irq_enabled) {
        ret = vhost_user_nvme_add_kvm_msi_virq(n, cq);
        if (ret < 0) {
            error_report("nvme_create_cq: add kvm msix virq failed");
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        ret = vhost_dev_nvme_set_guest_notifier(&n->dev, &cq->guest_notifier,
                                                cqid);
        if (ret < 0) {
            error_report("nvme_create_cq: set guest notifier failed");
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    }
    return NVME_SUCCESS;
}

static uint16_t nvme_identify_ctrl(NvmeCtrl *n, NvmeIdentify *c)
{
    uint64_t prp1 = le64_to_cpu(c->prp1);

    /* Only PRP1 used */
    pci_dma_write(&n->parent_obj, prp1, (void *)&n->id_ctrl,
                 sizeof(n->id_ctrl));
    return NVME_SUCCESS;
}

static uint16_t nvme_identify_ns(NvmeCtrl *n, NvmeIdentify *c)
{
    NvmeNamespace *ns;
    uint32_t nsid = le32_to_cpu(c->nsid);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    if (nsid == 0) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    /* Only PRP1 used */
    ns = &n->namespaces[nsid - 1];
    pci_dma_write(&n->parent_obj, prp1, (void *)ns, sizeof(*ns));
    return NVME_SUCCESS;
}

static uint16_t nvme_identify(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeIdentify *c = (NvmeIdentify *)cmd;

    switch (le32_to_cpu(c->cns)) {
    case 0x00:
        return nvme_identify_ns(n, c);
    case 0x01:
        return nvme_identify_ctrl(n, c);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static uint16_t nvme_get_feature(NvmeCtrl *n, NvmeCmd *cmd, NvmeCqe *cqe)
{
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t result;
    uint32_t dw0;
    int ret;

    switch (dw10 & 0xff) {
    case NVME_VOLATILE_WRITE_CACHE:
        result = 0;
        break;
    case NVME_NUMBER_OF_QUEUES:
        ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &dw0, sizeof(dw0));
        if (ret < 0) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        /* 0 based value for number of IO queues */
        if (n->num_io_queues > (dw0 & 0xffffu) + 1) {
            fprintf(stdout, "Adjust number of IO queues from %u to %u\n",
                    n->num_io_queues, (dw0 & 0xffffu) + 1);
                    n->num_io_queues = (dw0 & 0xffffu) + 1;
        }
        result = cpu_to_le32((n->num_io_queues - 1) |
                            ((n->num_io_queues - 1) << 16));
        break;
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    cqe->result = result;
    return NVME_SUCCESS;
}

static uint16_t nvme_set_feature(NvmeCtrl *n, NvmeCmd *cmd, NvmeCqe *cqe)
{
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw0;
    int ret;

    switch (dw10 & 0xff) {
    case NVME_NUMBER_OF_QUEUES:
        ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &dw0, sizeof(dw0));
        if (ret < 0) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        /* 0 based value for number of IO queues */
        if (n->num_io_queues > (dw0 & 0xffffu) + 1) {
            fprintf(stdout, "Adjust number of IO queues from %u to %u\n",
                    n->num_io_queues, (dw0 & 0xffffu) + 1);
                    n->num_io_queues = (dw0 & 0xffffu) + 1;
        }
        cqe->result = cpu_to_le32((n->num_io_queues - 1) |
                                 ((n->num_io_queues - 1) << 16));
        break;
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    return NVME_SUCCESS;
}

static uint16_t nvme_doorbell_buffer_config(NvmeCtrl *n, NvmeCmd *cmd)
{
    int ret;
    NvmeCmd cqe;

    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_doorbell_buffer_config: set failed");
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    n->dataplane_started = true;
    return NVME_SUCCESS;
}

static uint16_t nvme_abort_cmd(NvmeCtrl *n, NvmeCmd *cmd)
{
    int ret;
    NvmeCmd cqe;

    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, cmd, &cqe, sizeof(cqe));
    if (ret < 0) {
        error_report("nvme_abort_cmd: set failed");
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static const char *nvme_admin_str[256] = {
    [NVME_ADM_CMD_IDENTIFY] = "NVME_ADM_CMD_IDENTIFY",
    [NVME_ADM_CMD_CREATE_CQ] = "NVME_ADM_CMD_CREATE_CQ",
    [NVME_ADM_CMD_GET_LOG_PAGE] = "NVME_ADM_CMD_GET_LOG_PAGE",
    [NVME_ADM_CMD_CREATE_SQ] = "NVME_ADM_CMD_CREATE_SQ",
    [NVME_ADM_CMD_DELETE_CQ] = "NVME_ADM_CMD_DELETE_CQ",
    [NVME_ADM_CMD_DELETE_SQ] = "NVME_ADM_CMD_DELETE_SQ",
    [NVME_ADM_CMD_SET_FEATURES] = "NVME_ADM_CMD_SET_FEATURES",
    [NVME_ADM_CMD_GET_FEATURES] = "NVME_ADM_CMD_SET_FEATURES",
    [NVME_ADM_CMD_ABORT] = "NVME_ADM_CMD_ABORT",
    [NVME_ADM_CMD_DB_BUFFER_CFG] = "NVME_ADM_CMD_DB_BUFFER_CFG",
};

static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeCmd *cmd, NvmeCqe *cqe)
{
    fprintf(stdout, "QEMU Processing %s\n", nvme_admin_str[cmd->opcode] ?
            nvme_admin_str[cmd->opcode] : "Unsupported ADMIN Command");

    switch (cmd->opcode) {
    case NVME_ADM_CMD_DELETE_SQ:
        return nvme_del_sq(n, cmd);
    case NVME_ADM_CMD_CREATE_SQ:
        return nvme_create_sq(n, cmd);
    case NVME_ADM_CMD_DELETE_CQ:
        return nvme_del_cq(n, cmd);
    case NVME_ADM_CMD_CREATE_CQ:
        return nvme_create_cq(n, cmd);
    case NVME_ADM_CMD_IDENTIFY:
        return nvme_identify(n, cmd);
    case NVME_ADM_CMD_SET_FEATURES:
        return nvme_set_feature(n, cmd, cqe);
    case NVME_ADM_CMD_GET_FEATURES:
        return nvme_get_feature(n, cmd, cqe);
    case NVME_ADM_CMD_DB_BUFFER_CFG:
        return nvme_doorbell_buffer_config(n, cmd);
    case NVME_ADM_CMD_ABORT:
        return nvme_abort_cmd(n, cmd);
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static int nvme_start_ctrl(NvmeCtrl *n)
{
    uint32_t page_bits = NVME_CC_MPS(n->bar.cc) + 12;
    uint32_t page_size = 1 << page_bits;

    fprintf(stdout, "QEMU Start NVMe Controller ...\n");
    if (vhost_dev_nvme_start(&n->dev, NULL) < 0) {
        error_report("nvme_start_ctrl: vhost device start failed");
        return -1;
    }

    if (!n->bar.asq || !n->bar.acq ||
            n->bar.asq & (page_size - 1) || n->bar.acq & (page_size - 1) ||
            NVME_CC_MPS(n->bar.cc) < NVME_CAP_MPSMIN(n->bar.cap) ||
            NVME_CC_MPS(n->bar.cc) > NVME_CAP_MPSMAX(n->bar.cap) ||
            !NVME_AQA_ASQS(n->bar.aqa) || !NVME_AQA_ACQS(n->bar.aqa)) {
        error_report("nvme_start_ctrl: invalid bar configurations");
        return -1;
    }

    n->page_bits = page_bits;
    n->page_size = page_size;
    n->max_prp_ents = n->page_size / sizeof(uint64_t);
    n->cqe_size = 1 << NVME_CC_IOCQES(n->bar.cc);
    n->sqe_size = 1 << NVME_CC_IOSQES(n->bar.cc);
    nvme_init_cq(&n->admin_cq, n, n->bar.acq, 0, 0,
        NVME_AQA_ACQS(n->bar.aqa) + 1, 1);
    nvme_init_sq(&n->admin_sq, n, n->bar.asq, 0, 0,
        NVME_AQA_ASQS(n->bar.aqa) + 1);

    return 0;
}

static int nvme_clear_ctrl(NvmeCtrl *n)
{
    fprintf(stdout, "QEMU Stop NVMe Controller ...\n");
    if (vhost_dev_nvme_stop(&n->dev) < 0) {
        error_report("nvme_clear_ctrl: vhost device stop failed");
        return -1;
    }
    n->bar.cc = 0;
    n->dataplane_started = false;
    return 0;
}

static void nvme_write_bar(NvmeCtrl *n, hwaddr offset, uint64_t data,
                           unsigned size)
{
    switch (offset) {
    case 0xc:
        n->bar.intms |= data & 0xffffffff;
        n->bar.intmc = n->bar.intms;
        break;
    case 0x10:
        n->bar.intms &= ~(data & 0xffffffff);
        n->bar.intmc = n->bar.intms;
        break;
    case 0x14:
        /* Windows first sends data, then sends enable bit */
        if (!NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc) &&
            !NVME_CC_SHN(data) && !NVME_CC_SHN(n->bar.cc))
        {
            n->bar.cc = data;
        }

        if (NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc)) {
            n->bar.cc = data;
            if (nvme_start_ctrl(n)) {
                n->bar.csts = NVME_CSTS_FAILED;
            } else {
                n->bar.csts = NVME_CSTS_READY;
            }
        } else if (!NVME_CC_EN(data) && NVME_CC_EN(n->bar.cc)) {
            nvme_clear_ctrl(n);
            n->bar.csts &= ~NVME_CSTS_READY;
        }
        if (NVME_CC_SHN(data) && !(NVME_CC_SHN(n->bar.cc))) {
                nvme_clear_ctrl(n);
                n->bar.cc = data;
                n->bar.csts |= NVME_CSTS_SHST_COMPLETE;
        } else if (!NVME_CC_SHN(data) && NVME_CC_SHN(n->bar.cc)) {
                n->bar.csts &= ~NVME_CSTS_SHST_COMPLETE;
                n->bar.cc = data;
        }
        break;
    case 0x24:
        n->bar.aqa = data & 0xffffffff;
        break;
    case 0x28:
        n->bar.asq = data;
        break;
    case 0x2c:
        n->bar.asq |= data << 32;
        break;
    case 0x30:
        n->bar.acq = data;
        break;
    case 0x34:
        n->bar.acq |= data << 32;
        break;
    default:
        break;
    }
}

static uint64_t nvme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    uint8_t *ptr = (uint8_t *)&n->bar;
    uint64_t val = 0;

    if (addr < sizeof(n->bar)) {
        memcpy(&val, ptr + addr, size);
    }
    return val;
}

static void nvme_process_admin_cmd(NvmeSQueue *sq)
{
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];
    uint16_t status;
    hwaddr addr;
    NvmeCmd cmd;
    NvmeCqe cqe;

    while (!(nvme_sq_empty(sq))) {
        addr = sq->dma_addr + sq->head * n->sqe_size;
        pci_dma_read(&n->parent_obj, addr, (void *)&cmd, sizeof(cmd));
        nvme_inc_sq_head(sq);

        memset(&cqe, 0, sizeof(cqe));
        cqe.cid = cmd.cid;

        status = nvme_admin_cmd(n, &cmd, &cqe);
        cqe.status = cpu_to_le16(status << 1 | cq->phase);
        cqe.sq_id = cpu_to_le16(sq->sqid);
        cqe.sq_head = cpu_to_le16(sq->head);
        addr = cq->dma_addr + cq->tail * n->cqe_size;
        nvme_inc_cq_tail(cq);
        pci_dma_write(&n->parent_obj, addr, (void *)&cqe, sizeof(cqe));
        nvme_isr_notify(n, cq);
    }
}

static void nvme_process_admin_db(NvmeCtrl *n, hwaddr addr, int val)
{
    uint32_t qid;

    if (((addr - 0x1000) >> 2) & 1) {
        uint16_t new_head = val & 0xffff;
        NvmeCQueue *cq;

        qid = (addr - (0x1000 + (1 << 2))) >> 3;
        if (nvme_check_cqid(n, qid)) {
            return;
        }

        cq = n->cq[qid];
        if (new_head >= cq->size) {
            return;
        }

        cq->head = new_head;

        if (cq->tail != cq->head) {
            nvme_isr_notify(n, cq);
        }
    } else {
        uint16_t new_tail = val & 0xffff;
        NvmeSQueue *sq;

        qid = (addr - 0x1000) >> 3;
        if (nvme_check_sqid(n, qid)) {
            return;
        }

        sq = n->sq[qid];
        if (new_tail >= sq->size) {
            return;
        }

        sq->tail = new_tail;
        nvme_process_admin_cmd(sq);
    }
}

static void
nvme_process_io_db(NvmeCtrl *n, hwaddr addr, int val)
{
    uint16_t cq_head, sq_tail;
    uint32_t qid;

    /* Do nothing after the doorbell buffer config command*/
    if (n->dataplane_started) {
        return;
    }

    if (((addr - 0x1000) >> 2) & 1) {
        qid = (addr - (0x1000 + (1 << 2))) >> 3;
        cq_head = val & 0xffff;
        vhost_user_nvme_io_cmd_pass(&n->dev, qid,
                                    cq_head, false);
    } else {
        qid = (addr - 0x1000) >> 3;
        sq_tail = val & 0xffff;
        vhost_user_nvme_io_cmd_pass(&n->dev, qid,
                                    sq_tail, true);
    }

    return;
}

static void nvme_mmio_write(void *opaque, hwaddr addr, uint64_t data,
    unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    if (addr < sizeof(n->bar)) {
        nvme_write_bar(n, addr, data, size);
    } else if (addr >= 0x1000 && addr < 0x1008) {
        nvme_process_admin_db(n, addr, data);
    } else {
        nvme_process_io_db(n, addr, data);
    }
}

static const MemoryRegionOps nvme_mmio_ops = {
    .read = nvme_mmio_read,
    .write = nvme_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 8,
    },
};

static void nvme_cleanup(NvmeCtrl *n)
{
    g_free(n->sq);
    g_free(n->cq);
    g_free(n->namespaces);
}

static int nvme_init(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME_VHOST(pci_dev);
    NvmeIdCtrl *id = &n->id_ctrl;
    NvmeIdentify cmd;
    int ret, i;
    uint8_t *pci_conf;

    if (!n->chardev.chr) {
        error_report("vhost-user-nvme: missing chardev");
        return -1;
    }

    if (vhost_dev_nvme_init(&n->dev, (void *)&n->chardev,
                         VHOST_BACKEND_TYPE_USER, 0) < 0) {
        error_report("vhost-user-nvme: vhost_dev_init failed");
        return -1;
    }

    pci_conf = pci_dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_dev->config, 0x2);
    pci_config_set_class(pci_dev->config, PCI_CLASS_STORAGE_EXPRESS);
    pcie_endpoint_cap_init(&n->parent_obj, 0x80);

    n->reg_size = pow2ceil(0x1004 + 2 * (n->num_io_queues + 2) * 4);

    memory_region_init_io(&n->iomem, OBJECT(n), &nvme_mmio_ops, n,
                          "nvme", n->reg_size);
    pci_register_bar(&n->parent_obj, 0,
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
        &n->iomem);
    msix_init_exclusive_bar(&n->parent_obj, n->num_io_queues + 1, 4, NULL);

    /* Get PCI capabilities via socket */
    n->bar.cap = 0;
    ret = vhost_user_nvme_get_cap(&n->dev, &n->bar.cap);
    if (ret < 0) {
        error_report("vhost-user-nvme: get controller capabilities failed");
        return -1;
    }
    fprintf(stdout, "Emulated Controller Capabilities 0x%"PRIx64"\n",
            n->bar.cap);

    /* Get Identify Controller from backend process */
    cmd.opcode = NVME_ADM_CMD_IDENTIFY;
    cmd.cns = 0x1;
    ret = vhost_user_nvme_admin_cmd_raw(&n->dev, (NvmeCmd *)&cmd,
                                        id, sizeof(*id));
    if (ret < 0) {
        error_report("vhost-user-nvme: get identify controller failed");
        return -1;
    }

    /* TODO: Don't support Controller Memory Buffer and AER now */
    n->bar.vs = 0x00010000;
    n->bar.intmc = n->bar.intms = 0;

    n->namespaces = g_new0(NvmeNamespace, id->nn);
    n->sq = g_new0(NvmeSQueue *, n->num_io_queues + 1);
    n->cq = g_new0(NvmeCQueue *, n->num_io_queues + 1);
    assert(n->sq != NULL);
    assert(n->cq != NULL);

    for (i = 1; i <= id->nn; i++) {
        cmd.opcode = NVME_ADM_CMD_IDENTIFY;
        cmd.cns = 0x0;
        cmd.nsid = i;
        ret = vhost_user_nvme_admin_cmd_raw(&n->dev, (NvmeCmd *)&cmd,
                                            &n->namespaces[i - 1],
                                            sizeof(NvmeNamespace));
        if (ret < 0) {
            error_report("vhost-user-nvme: get ns %d failed", i);
            goto err;
        }
    }

    return 0;

err:
    nvme_cleanup(n);
    return -1;
}

static void nvme_exit(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME_VHOST(pci_dev);

    nvme_cleanup(n);
    msix_uninit_exclusive_bar(pci_dev);
}

static Property nvme_props[] = {
    DEFINE_PROP_UINT32("num_io_queues", NvmeCtrl, num_io_queues, 1),
    DEFINE_PROP_CHR("chardev", NvmeCtrl, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription nvme_vmstate = {
    .name = "nvme",
    .unmigratable = 1,
};

static void nvme_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->init = nvme_init;
    pc->exit = nvme_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->vendor_id = PCI_VENDOR_ID_INTEL;
    pc->device_id = 0x5845;
    pc->revision = 2;
    pc->is_express = 1;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Non-Volatile Memory Express";
    dc->props = nvme_props;
    dc->vmsd = &nvme_vmstate;
}

static void nvme_instance_init(Object *obj)
{
    NvmeCtrl *s = NVME_VHOST(obj);

    device_add_bootindex_property(obj, &s->bootindex,
                                  "bootindex", "/namespace@1,0",
                                  DEVICE(obj), &error_abort);
}

static const TypeInfo nvme_info = {
    .name          = "vhost-user-nvme",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NvmeCtrl),
    .class_init    = nvme_class_init,
    .instance_init = nvme_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void nvme_register_types(void)
{
    type_register_static(&nvme_info);
}

type_init(nvme_register_types)
