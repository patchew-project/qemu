/*
 * QEMU NVM Express Controller
 *
 * Copyright (c) 2012, Intel Corporation
 *
 * Written by Keith Busch <keith.busch@intel.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

/**
 * Reference Specification: NVM Express 1.3d
 *
 *   https://nvmexpress.org/resources/specifications/
 */

/**
 * Usage: add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device nvme,serial=<serial>,id=<id[optional]>, \
 *              cmb_size_mb=<cmb_size_mb[optional]>, \
 *              num_queues=<N[optional]>, \
 *              mdts=<mdts[optional]>
 *      -device nvme-ns,drive=<drive_id>,bus=nvme0,nsid=1
 *
 * Note cmb_size_mb denotes size of CMB in MB. CMB is assumed to be at
 * offset 0 in BAR2 and supports only WDS, RDS and SQS for now.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/block/block.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/block-backend.h"

#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "nvme.h"
#include "nvme-ns.h"

#define NVME_SPEC_VER 0x00010300
#define NVME_MAX_QS PCI_MSIX_FLAGS_QSIZE
#define NVME_TEMPERATURE 0x143

#define NVME_GUEST_ERR(trace, fmt, ...) \
    do { \
        (trace_##trace)(__VA_ARGS__); \
        qemu_log_mask(LOG_GUEST_ERROR, #trace \
            " in %s: " fmt "\n", __func__, ## __VA_ARGS__); \
    } while (0)

static void nvme_process_sq(void *opaque);
static void nvme_aio_cb(void *opaque, int ret);

static inline void *nvme_addr_to_cmb(NvmeCtrl *n, hwaddr addr)
{
    return &n->cmbuf[addr - n->ctrl_mem.addr];
}

static inline bool nvme_addr_is_cmb(NvmeCtrl *n, hwaddr addr)
{
    hwaddr low = n->ctrl_mem.addr;
    hwaddr hi  = n->ctrl_mem.addr + int128_get64(n->ctrl_mem.size);

    return addr >= low && addr < hi;
}

static int nvme_addr_read(NvmeCtrl *n, hwaddr addr, void *buf, int size)
{
    hwaddr hi = addr + size;
    if (hi < addr) {
        return 1;
    }

    if (n->cmbsz && nvme_addr_is_cmb(n, addr) && nvme_addr_is_cmb(n, hi)) {
        memcpy(buf, (void *) &n->cmbuf[addr - n->ctrl_mem.addr], size);
        return 0;
    }

    return pci_dma_read(&n->parent_obj, addr, buf, size);
}

static uint16_t nvme_nsid_err(NvmeCtrl *n, uint32_t nsid)
{
    if (nsid && nsid < n->num_namespaces) {
        trace_nvme_dev_err_inactive_ns(nsid, n->num_namespaces);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trace_nvme_dev_err_invalid_ns(nsid, n->num_namespaces);
    return NVME_INVALID_NSID | NVME_DNR;
}

static int nvme_check_sqid(NvmeCtrl *n, uint16_t sqid)
{
    return sqid < n->params.num_queues && n->sq[sqid] != NULL ? 0 : -1;
}

static int nvme_check_cqid(NvmeCtrl *n, uint16_t cqid)
{
    return cqid < n->params.num_queues && n->cq[cqid] != NULL ? 0 : -1;
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

static uint8_t nvme_cq_full(NvmeCQueue *cq)
{
    return (cq->tail + 1) % cq->size == cq->head;
}

static uint8_t nvme_sq_empty(NvmeSQueue *sq)
{
    return sq->head == sq->tail;
}

static void nvme_irq_check(NvmeCtrl *n)
{
    if (msix_enabled(&(n->parent_obj))) {
        return;
    }
    if (~n->bar.intms & n->irq_status) {
        pci_irq_assert(&n->parent_obj);
    } else {
        pci_irq_deassert(&n->parent_obj);
    }
}

static void nvme_irq_assert(NvmeCtrl *n, NvmeCQueue *cq)
{
    if (cq->irq_enabled) {
        if (msix_enabled(&(n->parent_obj))) {
            trace_nvme_dev_irq_msix(cq->vector);
            msix_notify(&(n->parent_obj), cq->vector);
        } else {
            trace_nvme_dev_irq_pin();
            assert(cq->cqid < 64);
            n->irq_status |= 1 << cq->cqid;
            nvme_irq_check(n);
        }
    } else {
        trace_nvme_dev_irq_masked();
    }
}

static void nvme_irq_deassert(NvmeCtrl *n, NvmeCQueue *cq)
{
    if (cq->irq_enabled) {
        if (msix_enabled(&(n->parent_obj))) {
            return;
        } else {
            assert(cq->cqid < 64);
            n->irq_status &= ~(1 << cq->cqid);
            nvme_irq_check(n);
        }
    }
}

static uint16_t nvme_map_prp(NvmeCtrl *n, QEMUSGList *qsg, QEMUIOVector *iov,
    uint64_t prp1, uint64_t prp2, uint32_t len, NvmeRequest *req)
{
    hwaddr trans_len = n->page_size - (prp1 % n->page_size);
    trans_len = MIN(len, trans_len);
    int num_prps = (len >> n->page_bits) + 1;
    uint16_t status = NVME_SUCCESS;
    bool is_cmb = false;
    bool prp_list_in_cmb = false;

    trace_nvme_dev_map_prp(nvme_cid(req), req->cmd.opcode, trans_len, len,
        prp1, prp2, num_prps);

    if (unlikely(!prp1)) {
        trace_nvme_dev_err_invalid_prp();
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (nvme_addr_is_cmb(n, prp1)) {
        is_cmb = true;

        qemu_iovec_init(iov, num_prps);

        /*
         * PRPs do not cross page boundaries, so if the start address (here,
         * prp1) is within the CMB, it cannot cross outside the controller
         * memory buffer range. This is ensured by
         *
         *   len = n->page_size - (addr % n->page_size)
         *
         * Thus, we can directly add to the iovec without risking an out of
         * bounds access. This also holds for the remaining qemu_iovec_add
         * calls.
         */
        qemu_iovec_add(iov, nvme_addr_to_cmb(n, prp1), trans_len);
    } else {
        pci_dma_sglist_init(qsg, &n->parent_obj, num_prps);
        qemu_sglist_add(qsg, prp1, trans_len);
    }

    len -= trans_len;
    if (len) {
        if (unlikely(!prp2)) {
            trace_nvme_dev_err_invalid_prp2_missing();
            status = NVME_INVALID_FIELD | NVME_DNR;
            goto unmap;
        }

        if (len > n->page_size) {
            uint64_t prp_list[n->max_prp_ents];
            uint32_t nents, prp_trans;
            int i = 0;

            if (nvme_addr_is_cmb(n, prp2)) {
                prp_list_in_cmb = true;
            }

            nents = (len + n->page_size - 1) >> n->page_bits;
            prp_trans = MIN(n->max_prp_ents, nents) * sizeof(uint64_t);
            if (nvme_addr_read(n, prp2, (void *) prp_list, prp_trans)) {
                trace_nvme_dev_err_addr_read(prp2);
                status = NVME_DATA_TRANSFER_ERROR;
                goto unmap;
            }
            while (len != 0) {
                uint64_t prp_ent = le64_to_cpu(prp_list[i]);

                if (i == n->max_prp_ents - 1 && len > n->page_size) {
                    if (unlikely(!prp_ent || prp_ent & (n->page_size - 1))) {
                        trace_nvme_dev_err_invalid_prplist_ent(prp_ent);
                        status = NVME_INVALID_FIELD | NVME_DNR;
                        goto unmap;
                    }

                    if (prp_list_in_cmb != nvme_addr_is_cmb(n, prp_ent)) {
                        status = NVME_INVALID_USE_OF_CMB | NVME_DNR;
                        goto unmap;
                    }

                    i = 0;
                    nents = (len + n->page_size - 1) >> n->page_bits;
                    prp_trans = MIN(n->max_prp_ents, nents) * sizeof(uint64_t);
                    if (nvme_addr_read(n, prp_ent, (void *) prp_list, prp_trans)) {
                        trace_nvme_dev_err_addr_read(prp_ent);
                        status = NVME_DATA_TRANSFER_ERROR;
                        goto unmap;
                    }
                    prp_ent = le64_to_cpu(prp_list[i]);
                }

                if (unlikely(!prp_ent || prp_ent & (n->page_size - 1))) {
                    trace_nvme_dev_err_invalid_prplist_ent(prp_ent);
                    status = NVME_INVALID_FIELD | NVME_DNR;
                    goto unmap;
                }

                if (is_cmb != nvme_addr_is_cmb(n, prp_ent)) {
                    status = NVME_INVALID_USE_OF_CMB | NVME_DNR;
                    goto unmap;
                }

                trans_len = MIN(len, n->page_size);
                if (is_cmb) {
                    qemu_iovec_add(iov, nvme_addr_to_cmb(n, prp_ent),
                        trans_len);
                } else {
                    qemu_sglist_add(qsg, prp_ent, trans_len);
                }

                len -= trans_len;
                i++;
            }
        } else {
            if (is_cmb != nvme_addr_is_cmb(n, prp2)) {
                status = NVME_INVALID_USE_OF_CMB | NVME_DNR;
                goto unmap;
            }

            if (unlikely(prp2 & (n->page_size - 1))) {
                trace_nvme_dev_err_invalid_prp2_align(prp2);
                status = NVME_INVALID_FIELD | NVME_DNR;
                goto unmap;
            }

            if (is_cmb) {
                qemu_iovec_add(iov, nvme_addr_to_cmb(n, prp2), len);
            } else {
                qemu_sglist_add(qsg, prp2, len);
            }
        }
    }

    return NVME_SUCCESS;

unmap:
    if (is_cmb) {
        qemu_iovec_destroy(iov);
    } else {
        qemu_sglist_destroy(qsg);
    }

    return status;
}

static uint16_t nvme_map_to_cmb(NvmeCtrl *n, QEMUIOVector *iov, hwaddr addr,
    size_t len)
{
    hwaddr hi = addr + len;
    if (hi < addr) {
        return NVME_DATA_TRANSFER_ERROR;
    }

    if (!nvme_addr_is_cmb(n, addr) || !nvme_addr_is_cmb(n, hi)) {
        return NVME_DATA_TRANSFER_ERROR;
    }

    qemu_iovec_add(iov, nvme_addr_to_cmb(n, addr), len);

    return NVME_SUCCESS;
}

static uint16_t nvme_map_sgl_data(NvmeCtrl *n, QEMUSGList *qsg,
    QEMUIOVector *iov, NvmeSglDescriptor *segment, uint64_t nsgld,
    uint32_t *len, bool is_cmb, NvmeRequest *req)
{
    dma_addr_t addr, trans_len;
    uint16_t status;

    for (int i = 0; i < nsgld; i++) {
        if (NVME_SGL_TYPE(segment[i].type) != SGL_DESCR_TYPE_DATA_BLOCK) {
            trace_nvme_dev_err_invalid_sgl_descriptor(nvme_cid(req),
                NVME_SGL_TYPE(segment[i].type));
            return NVME_SGL_DESCRIPTOR_TYPE_INVALID | NVME_DNR;
        }

        if (*len == 0) {
            if (!NVME_CTRL_SGLS_EXCESS_LENGTH(n->id_ctrl.sgls)) {
                trace_nvme_dev_err_invalid_sgl_excess_length(nvme_cid(req));
                return NVME_DATA_SGL_LENGTH_INVALID | NVME_DNR;
            }

            break;
        }

        addr = le64_to_cpu(segment[i].addr);
        trans_len = MIN(*len, le64_to_cpu(segment[i].len));

        if (nvme_addr_is_cmb(n, addr)) {
            /*
             * All data and metadata, if any, associated with a particular
             * command shall be located in either the CMB or host memory. Thus,
             * if an address if found to be in the CMB and we have already
             * mapped data that is in host memory, the use is invalid.
             */
            if (!is_cmb && qsg->size) {
                return NVME_INVALID_USE_OF_CMB | NVME_DNR;
            }
        } else {
            /*
             * Similarly, if the address does not reference the CMB, but we
             * have already established that the request has data or metadata
             * in the CMB, the use is invalid.
             */
            if (is_cmb) {
                return NVME_INVALID_USE_OF_CMB | NVME_DNR;
            }
        }

        if (is_cmb) {
            status = nvme_map_to_cmb(n, iov, addr, trans_len);
            if (status) {
                return status;
            }
        } else {
            qemu_sglist_add(qsg, addr, trans_len);
        }

        *len -= trans_len;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_map_sgl(NvmeCtrl *n, QEMUSGList *qsg, QEMUIOVector *iov,
    NvmeSglDescriptor sgl, uint32_t len, NvmeRequest *req)
{
    const int MAX_NSGLD = 256;

    NvmeSglDescriptor segment[MAX_NSGLD];
    uint64_t nsgld;
    uint16_t status;
    bool is_cmb = false;
    bool sgl_in_cmb = false;
    hwaddr addr = le64_to_cpu(sgl.addr);

    trace_nvme_dev_map_sgl(nvme_cid(req), NVME_SGL_TYPE(sgl.type), req->nlb, len);

    if (nvme_addr_is_cmb(n, addr)) {
        is_cmb = true;

        qemu_iovec_init(iov, 1);
    } else {
        pci_dma_sglist_init(qsg, &n->parent_obj, 1);
    }

    /*
     * If the entire transfer can be described with a single data block it can
     * be mapped directly.
     */
    if (NVME_SGL_TYPE(sgl.type) == SGL_DESCR_TYPE_DATA_BLOCK) {
        status = nvme_map_sgl_data(n, qsg, iov, &sgl, 1, &len, is_cmb, req);
        if (status) {
            goto unmap;
        }

        goto out;
    }

    /*
     * If the segment is located in the CMB, the submission queue of the
     * request must also reside there.
     */
    if (nvme_addr_is_cmb(n, addr)) {
        if (!nvme_addr_is_cmb(n, req->sq->dma_addr)) {
            return NVME_INVALID_USE_OF_CMB | NVME_DNR;
        }

        sgl_in_cmb = true;
    }

    while (NVME_SGL_TYPE(sgl.type) == SGL_DESCR_TYPE_SEGMENT) {
        bool addr_is_cmb;

        nsgld = le64_to_cpu(sgl.len) / sizeof(NvmeSglDescriptor);

        /* read the segment in chunks of 256 descriptors (4k) */
        while (nsgld > MAX_NSGLD) {
            if (nvme_addr_read(n, addr, segment, sizeof(segment))) {
                trace_nvme_dev_err_addr_read(addr);
                status = NVME_DATA_TRANSFER_ERROR;
                goto unmap;
            }

            status = nvme_map_sgl_data(n, qsg, iov, segment, MAX_NSGLD, &len,
                is_cmb, req);
            if (status) {
                goto unmap;
            }

            nsgld -= MAX_NSGLD;
            addr += MAX_NSGLD * sizeof(NvmeSglDescriptor);
        }

        if (nvme_addr_read(n, addr, segment, nsgld * sizeof(NvmeSglDescriptor))) {
            trace_nvme_dev_err_addr_read(addr);
            status = NVME_DATA_TRANSFER_ERROR;
            goto unmap;
        }

        sgl = segment[nsgld - 1];
        addr = le64_to_cpu(sgl.addr);

        /* an SGL is allowed to end with a Data Block in a regular Segment */
        if (NVME_SGL_TYPE(sgl.type) == SGL_DESCR_TYPE_DATA_BLOCK) {
            status = nvme_map_sgl_data(n, qsg, iov, segment, nsgld, &len,
                is_cmb, req);
            if (status) {
                goto unmap;
            }

            goto out;
        }

        /* do not map last descriptor */
        status = nvme_map_sgl_data(n, qsg, iov, segment, nsgld - 1, &len,
            is_cmb, req);
        if (status) {
            goto unmap;
        }

        /*
         * If the next segment is in the CMB, make sure that the sgl was
         * already located there.
         */
        addr_is_cmb = nvme_addr_is_cmb(n, addr);
        if ((sgl_in_cmb && !addr_is_cmb) || (!sgl_in_cmb && addr_is_cmb)) {
            status = NVME_INVALID_USE_OF_CMB | NVME_DNR;
            goto unmap;
        }
    }

    /*
     * If the segment did not end with a Data Block or a Segment descriptor, it
     * must be a Last Segment descriptor.
     */
    if (NVME_SGL_TYPE(sgl.type) != SGL_DESCR_TYPE_LAST_SEGMENT) {
        trace_nvme_dev_err_invalid_sgl_descriptor(nvme_cid(req),
            NVME_SGL_TYPE(sgl.type));
        status = NVME_SGL_DESCRIPTOR_TYPE_INVALID | NVME_DNR;
        goto unmap;
    }

    nsgld = le64_to_cpu(sgl.len) / sizeof(NvmeSglDescriptor);

    while (nsgld > MAX_NSGLD) {
        if (nvme_addr_read(n, addr, segment, sizeof(segment))) {
            trace_nvme_dev_err_addr_read(addr);
            status = NVME_DATA_TRANSFER_ERROR;
            goto unmap;
        }

        status = nvme_map_sgl_data(n, qsg, iov, segment, MAX_NSGLD, &len,
            is_cmb, req);
        if (status) {
            goto unmap;
        }

        nsgld -= MAX_NSGLD;
        addr += MAX_NSGLD * sizeof(NvmeSglDescriptor);
    }

    if (nvme_addr_read(n, addr, segment, nsgld * sizeof(NvmeSglDescriptor))) {
        trace_nvme_dev_err_addr_read(addr);
        status = NVME_DATA_TRANSFER_ERROR;
        goto unmap;
    }

    status = nvme_map_sgl_data(n, qsg, iov, segment, nsgld, &len, is_cmb, req);
    if (status) {
        goto unmap;
    }

out:
    /* if there is any residual left in len, the SGL was too short */
    if (len) {
        status = NVME_DATA_SGL_LENGTH_INVALID | NVME_DNR;
        goto unmap;
    }

    return NVME_SUCCESS;

unmap:
    if (is_cmb) {
        qemu_iovec_destroy(iov);
    } else {
        qemu_sglist_destroy(qsg);
    }

    return status;
}

static uint16_t nvme_dma(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
    NvmeCmd *cmd, DMADirection dir, NvmeRequest *req)
{
    QEMUSGList qsg;
    QEMUIOVector iov;
    uint16_t status = NVME_SUCCESS;
    size_t bytes;

    switch (NVME_CMD_FLAGS_PSDT(cmd->flags)) {
    case PSDT_PRP:
        status = nvme_map_prp(n, &qsg, &iov, le64_to_cpu(cmd->dptr.prp.prp1),
            le64_to_cpu(cmd->dptr.prp.prp2), len, req);
        if (status) {
            return status;
        }

        break;

    case PSDT_SGL_MPTR_CONTIGUOUS:
    case PSDT_SGL_MPTR_SGL:
        if (!req->sq->sqid) {
            /* SGLs shall not be used for Admin commands in NVMe over PCIe */
            return NVME_INVALID_FIELD;
        }

        status = nvme_map_sgl(n, &qsg, &iov, cmd->dptr.sgl, len, req);
        if (status) {
            return status;
        }

        break;

    default:
        return NVME_INVALID_FIELD;
    }

    if (qsg.nsg > 0) {
        uint64_t residual;

        if (dir == DMA_DIRECTION_TO_DEVICE) {
            residual = dma_buf_write(ptr, len, &qsg);
        } else {
            residual = dma_buf_read(ptr, len, &qsg);
        }

        if (unlikely(residual)) {
            trace_nvme_dev_err_invalid_dma();
            status = NVME_INVALID_FIELD | NVME_DNR;
        }

        qemu_sglist_destroy(&qsg);

        return status;
    }

    if (dir == DMA_DIRECTION_TO_DEVICE) {
        bytes = qemu_iovec_to_buf(&iov, 0, ptr, len);
    } else {
        bytes = qemu_iovec_from_buf(&iov, 0, ptr, len);
    }

    if (unlikely(bytes != len)) {
        trace_nvme_dev_err_invalid_dma();
        status = NVME_INVALID_FIELD | NVME_DNR;
    }

    qemu_iovec_destroy(&iov);

    return status;
}

static uint16_t nvme_map(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    uint32_t len = req->nlb << nvme_ns_lbads(req->ns);
    uint64_t prp1, prp2;

    switch (NVME_CMD_FLAGS_PSDT(cmd->flags)) {
    case PSDT_PRP:
        prp1 = le64_to_cpu(cmd->dptr.prp.prp1);
        prp2 = le64_to_cpu(cmd->dptr.prp.prp2);

        return nvme_map_prp(n, &req->qsg, &req->iov, prp1, prp2, len, req);
    case PSDT_SGL_MPTR_CONTIGUOUS:
    case PSDT_SGL_MPTR_SGL:
        return nvme_map_sgl(n, &req->qsg, &req->iov, cmd->dptr.sgl, len, req);
    default:
        return NVME_INVALID_FIELD;
    }
}

static void nvme_aio_destroy(NvmeAIO *aio)
{
    g_free(aio);
}

static NvmeAIO *nvme_aio_new(BlockBackend *blk, int64_t offset, size_t len,
    QEMUSGList *qsg, QEMUIOVector *iov, NvmeRequest *req,
    NvmeAIOCompletionFunc *cb)
{
    NvmeAIO *aio = g_malloc0(sizeof(*aio));

    *aio = (NvmeAIO) {
        .blk = blk,
        .offset = offset,
        .len = len,
        .req = req,
        .qsg = qsg,
        .iov = iov,
        .cb = cb,
    };

    return aio;
}

static inline void nvme_req_register_aio(NvmeRequest *req, NvmeAIO *aio,
    NvmeAIOOp opc)
{
    aio->opc = opc;

    trace_nvme_dev_req_register_aio(nvme_cid(req), aio, blk_name(aio->blk),
        aio->offset, aio->len, nvme_aio_opc_str(aio), req);

    if (req) {
        QTAILQ_INSERT_TAIL(&req->aio_tailq, aio, tailq_entry);
    }
}

static void nvme_aio(NvmeAIO *aio)
{
    BlockBackend *blk = aio->blk;
    BlockAcctCookie *acct = &aio->acct;
    BlockAcctStats *stats = blk_get_stats(blk);

    bool is_write, dma;

    switch (aio->opc) {
    case NVME_AIO_OPC_NONE:
        break;

    case NVME_AIO_OPC_FLUSH:
        block_acct_start(stats, acct, 0, BLOCK_ACCT_FLUSH);
        aio->aiocb = blk_aio_flush(blk, nvme_aio_cb, aio);
        break;

    case NVME_AIO_OPC_WRITE_ZEROES:
        block_acct_start(stats, acct, aio->len, BLOCK_ACCT_WRITE);
        aio->aiocb = blk_aio_pwrite_zeroes(blk, aio->offset, aio->len,
            BDRV_REQ_MAY_UNMAP, nvme_aio_cb, aio);
        break;

    case NVME_AIO_OPC_READ:
    case NVME_AIO_OPC_WRITE:
        dma = aio->qsg != NULL;
        is_write = (aio->opc == NVME_AIO_OPC_WRITE);

        block_acct_start(stats, acct, aio->len,
            is_write ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ);

        if (dma) {
            aio->aiocb = is_write ?
                dma_blk_write(blk, aio->qsg, aio->offset,
                    BDRV_SECTOR_SIZE, nvme_aio_cb, aio) :
                dma_blk_read(blk, aio->qsg, aio->offset,
                    BDRV_SECTOR_SIZE, nvme_aio_cb, aio);

            return;
        }

        aio->aiocb = is_write ?
            blk_aio_pwritev(blk, aio->offset, aio->iov, 0,
                nvme_aio_cb, aio) :
            blk_aio_preadv(blk, aio->offset, aio->iov, 0,
                nvme_aio_cb, aio);

        break;
    }
}

static void nvme_rw_aio(BlockBackend *blk, uint64_t offset, NvmeRequest *req)
{
    NvmeAIO *aio;
    size_t len = req->qsg.nsg > 0 ? req->qsg.size : req->iov.size;

    aio = nvme_aio_new(blk, offset, len, &req->qsg, &req->iov, req, NULL);
    nvme_req_register_aio(req, aio, nvme_req_is_write(req) ?
        NVME_AIO_OPC_WRITE : NVME_AIO_OPC_READ);
    nvme_aio(aio);
}

static void nvme_post_cqes(void *opaque)
{
    NvmeCQueue *cq = opaque;
    NvmeCtrl *n = cq->ctrl;
    NvmeRequest *req, *next;
    int ret;

    QTAILQ_FOREACH_SAFE(req, &cq->req_list, entry, next) {
        NvmeSQueue *sq;
        hwaddr addr;

        if (nvme_cq_full(cq)) {
            break;
        }

        QTAILQ_REMOVE(&cq->req_list, req, entry);
        sq = req->sq;
        req->cqe.status = cpu_to_le16((req->status << 1) | cq->phase);
        req->cqe.sq_id = cpu_to_le16(sq->sqid);
        req->cqe.sq_head = cpu_to_le16(sq->head);
        addr = cq->dma_addr + cq->tail * n->cqe_size;
        ret = pci_dma_write(&n->parent_obj, addr, (void *)&req->cqe,
            sizeof(req->cqe));
        if (ret) {
            trace_nvme_dev_err_addr_write(addr);
            QTAILQ_INSERT_TAIL(&cq->req_list, req, entry);
            timer_mod(cq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                100 * SCALE_MS);
            break;
        }
        nvme_inc_cq_tail(cq);
        QTAILQ_INSERT_TAIL(&sq->req_list, req, entry);
    }
    if (cq->tail != cq->head) {
        nvme_irq_assert(n, cq);
    }
}

static void nvme_enqueue_req_completion(NvmeCQueue *cq, NvmeRequest *req)
{
    assert(cq->cqid == req->sq->cqid);
    trace_nvme_dev_enqueue_req_completion(nvme_cid(req), cq->cqid, req->status);

    if (req->qsg.nalloc) {
        qemu_sglist_destroy(&req->qsg);
    }

    if (req->iov.nalloc) {
        qemu_iovec_destroy(&req->iov);
    }

    QTAILQ_REMOVE(&req->sq->out_req_list, req, entry);
    QTAILQ_INSERT_TAIL(&cq->req_list, req, entry);
    timer_mod(cq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
}

static void nvme_process_aers(void *opaque)
{
    NvmeCtrl *n = opaque;
    NvmeAsyncEvent *event, *next;

    trace_nvme_dev_process_aers(n->aer_queued);

    QTAILQ_FOREACH_SAFE(event, &n->aer_queue, entry, next) {
        NvmeRequest *req;
        NvmeAerResult *result;

        /* can't post cqe if there is nothing to complete */
        if (!n->outstanding_aers) {
            trace_nvme_dev_no_outstanding_aers();
            break;
        }

        /* ignore if masked (cqe posted, but event not cleared) */
        if (n->aer_mask & (1 << event->result.event_type)) {
            trace_nvme_dev_aer_masked(event->result.event_type, n->aer_mask);
            continue;
        }

        QTAILQ_REMOVE(&n->aer_queue, event, entry);
        n->aer_queued--;

        n->aer_mask |= 1 << event->result.event_type;
        n->outstanding_aers--;

        req = n->aer_reqs[n->outstanding_aers];

        result = (NvmeAerResult *) &req->cqe.result;
        result->event_type = event->result.event_type;
        result->event_info = event->result.event_info;
        result->log_page = event->result.log_page;
        g_free(event);

        req->status = NVME_SUCCESS;

        trace_nvme_dev_aer_post_cqe(result->event_type, result->event_info,
            result->log_page);

        nvme_enqueue_req_completion(&n->admin_cq, req);
    }
}

static void nvme_enqueue_event(NvmeCtrl *n, uint8_t event_type,
    uint8_t event_info, uint8_t log_page)
{
    NvmeAsyncEvent *event;

    trace_nvme_dev_enqueue_event(event_type, event_info, log_page);

    if (n->aer_queued == n->params.aer_max_queued) {
        trace_nvme_dev_enqueue_event_noqueue(n->aer_queued);
        return;
    }

    event = g_new(NvmeAsyncEvent, 1);
    event->result = (NvmeAerResult) {
        .event_type = event_type,
        .event_info = event_info,
        .log_page   = log_page,
    };

    QTAILQ_INSERT_TAIL(&n->aer_queue, event, entry);
    n->aer_queued++;

    nvme_process_aers(n);
}

static void nvme_clear_events(NvmeCtrl *n, uint8_t event_type)
{
    n->aer_mask &= ~(1 << event_type);
    if (!QTAILQ_EMPTY(&n->aer_queue)) {
        nvme_process_aers(n);
    }
}

static inline uint16_t nvme_check_mdts(NvmeCtrl *n, size_t len,
    NvmeRequest *req)
{
    uint8_t mdts = n->params.mdts;

    if (mdts && len > n->page_size << mdts) {
        trace_nvme_dev_err_mdts(nvme_cid(req), n->page_size << mdts, len);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static inline uint16_t nvme_check_prinfo(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *) &req->cmd;
    NvmeNamespace *ns = req->ns;

    uint16_t ctrl = le16_to_cpu(rw->control);

    if ((ctrl & NVME_RW_PRINFO_PRACT) && !(ns->id_ns.dps & DPS_TYPE_MASK)) {
        trace_nvme_dev_err_prinfo(nvme_cid(req), ctrl);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static inline uint16_t nvme_check_bounds(NvmeCtrl *n, uint64_t slba,
    uint32_t nlb, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    uint64_t nsze = le64_to_cpu(ns->id_ns.nsze);

    if (unlikely((slba + nlb) > nsze)) {
        block_acct_invalid(blk_get_stats(ns->blk),
            nvme_req_is_write(req) ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ);
        trace_nvme_dev_err_invalid_lba_range(slba, nlb, nsze);
        return NVME_LBA_RANGE | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_check_rw(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    size_t len = req->nlb << nvme_ns_lbads(ns);
    uint16_t status;

    status = nvme_check_mdts(n, len, req);
    if (status) {
        return status;
    }

    status = nvme_check_prinfo(n, req);
    if (status) {
        return status;
    }

    status = nvme_check_bounds(n, req->slba, req->nlb, req);
    if (status) {
        return status;
    }

    return NVME_SUCCESS;
}

static void nvme_rw_cb(NvmeRequest *req, void *opaque)
{
    NvmeNamespace *ns = req->ns;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];

    trace_nvme_dev_rw_cb(nvme_cid(req), nvme_nsid(ns));

    nvme_enqueue_req_completion(cq, req);
}

static void nvme_aio_cb(void *opaque, int ret)
{
    NvmeAIO *aio = opaque;
    NvmeRequest *req = aio->req;

    BlockBackend *blk = aio->blk;
    BlockAcctCookie *acct = &aio->acct;
    BlockAcctStats *stats = blk_get_stats(blk);

    Error *local_err = NULL;

    trace_nvme_dev_aio_cb(nvme_cid(req), aio, blk_name(blk), aio->offset,
        nvme_aio_opc_str(aio), req);

    if (req) {
        QTAILQ_REMOVE(&req->aio_tailq, aio, tailq_entry);
    }

    if (!ret) {
        block_acct_done(stats, acct);

        if (aio->cb) {
            aio->cb(aio, aio->cb_arg);
        }
    } else {
        block_acct_failed(stats, acct);

        if (req) {
            uint16_t status;

            switch (aio->opc) {
            case NVME_AIO_OPC_READ:
                status = NVME_UNRECOVERED_READ;
                break;
            case NVME_AIO_OPC_WRITE:
            case NVME_AIO_OPC_WRITE_ZEROES:
                status = NVME_WRITE_FAULT;
                break;
            default:
                status = NVME_INTERNAL_DEV_ERROR;
                break;
            }

            trace_nvme_dev_err_aio(nvme_cid(req), aio, blk_name(blk),
                aio->offset, nvme_aio_opc_str(aio), req, status);

            error_setg_errno(&local_err, -ret, "aio failed");
            error_report_err(local_err);

            /*
             * An Internal Error trumps all other errors. For other errors,
             * only set the first error encountered. Any additional errors will
             * be recorded in the error information log page.
             */
            if (!req->status ||
                nvme_status_is_error(status, NVME_INTERNAL_DEV_ERROR)) {
                req->status = status;
            }
        }
    }

    if (req && QTAILQ_EMPTY(&req->aio_tailq)) {
        if (req->cb) {
            req->cb(req, req->cb_arg);
        } else {
            NvmeSQueue *sq = req->sq;
            NvmeCtrl *n = sq->ctrl;
            NvmeCQueue *cq = n->cq[sq->cqid];

            nvme_enqueue_req_completion(cq, req);
        }
    }

    nvme_aio_destroy(aio);
}

static uint16_t nvme_flush(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeAIO *aio = nvme_aio_new(ns->blk, 0x0, 0, NULL, NULL, req, NULL);

    nvme_req_register_aio(req, aio, NVME_AIO_OPC_FLUSH);
    nvme_aio(aio);

    return NVME_NO_COMPLETE;
}

static uint16_t nvme_write_zeros(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeAIO *aio;

    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *) cmd;

    int64_t offset;
    size_t count;
    uint16_t status;

    req->slba = le64_to_cpu(rw->slba);
    req->nlb  = le16_to_cpu(rw->nlb) + 1;

    trace_nvme_dev_write_zeros(nvme_cid(req), nvme_nsid(ns), req->slba,
        req->nlb);

    status = nvme_check_bounds(n, req->slba, req->nlb, req);
    if (unlikely(status)) {
        block_acct_invalid(blk_get_stats(ns->blk), BLOCK_ACCT_WRITE);
        return status;
    }

    offset = req->slba << nvme_ns_lbads(ns);
    count = req->nlb << nvme_ns_lbads(ns);

    aio = nvme_aio_new(ns->blk, offset, count, NULL, NULL, req, NULL);

    nvme_req_register_aio(req, aio, NVME_AIO_OPC_WRITE_ZEROES);
    nvme_aio(aio);

    return NVME_NO_COMPLETE;
}

static uint16_t nvme_rw(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *) cmd;
    NvmeNamespace *ns = req->ns;
    int status;

    enum BlockAcctType acct =
        nvme_req_is_write(req) ? BLOCK_ACCT_WRITE : BLOCK_ACCT_READ;

    req->nlb  = le16_to_cpu(rw->nlb) + 1;
    req->slba = le64_to_cpu(rw->slba);

    trace_nvme_dev_rw(nvme_cid(req), nvme_req_is_write(req) ? "write" : "read",
        nvme_nsid(ns), req->nlb, req->nlb << nvme_ns_lbads(ns),
        req->slba);

    status = nvme_check_rw(n, req);
    if (status) {
        block_acct_invalid(blk_get_stats(ns->blk), acct);
        return status;
    }

    status = nvme_map(n, cmd, req);
    if (status) {
        block_acct_invalid(blk_get_stats(ns->blk), acct);
        return status;
    }

    nvme_rw_aio(ns->blk, req->slba << nvme_ns_lbads(ns), req);
    nvme_req_set_cb(req, nvme_rw_cb, NULL);

    return NVME_NO_COMPLETE;
}

static uint16_t nvme_io_cmd(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    uint32_t nsid = le32_to_cpu(cmd->nsid);

    trace_nvme_dev_io_cmd(nvme_cid(req), nsid, le16_to_cpu(req->sq->sqid), cmd->opcode);

    req->ns = nvme_ns(n, nsid);

    if (unlikely(!req->ns)) {
        return nvme_nsid_err(n, nsid);
    }

    switch (cmd->opcode) {
    case NVME_CMD_FLUSH:
        return nvme_flush(n, cmd, req);
    case NVME_CMD_WRITE_ZEROS:
        return nvme_write_zeros(n, cmd, req);
    case NVME_CMD_WRITE:
    case NVME_CMD_READ:
        return nvme_rw(n, cmd, req);
    default:
        trace_nvme_dev_err_invalid_opc(cmd->opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static void nvme_free_sq(NvmeSQueue *sq, NvmeCtrl *n)
{
    n->sq[sq->sqid] = NULL;
    timer_del(sq->timer);
    timer_free(sq->timer);
    g_free(sq->io_req);
    if (sq->sqid) {
        g_free(sq);
    }
}

static uint16_t nvme_del_sq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)cmd;
    NvmeRequest *req, *next;
    NvmeSQueue *sq;
    NvmeCQueue *cq;
    NvmeAIO *aio;
    uint16_t qid = le16_to_cpu(c->qid);

    if (unlikely(!qid || nvme_check_sqid(n, qid))) {
        trace_nvme_dev_err_invalid_del_sq(qid);
        return NVME_INVALID_QID | NVME_DNR;
    }

    trace_nvme_dev_del_sq(qid);

    sq = n->sq[qid];
    while (!QTAILQ_EMPTY(&sq->out_req_list)) {
        req = QTAILQ_FIRST(&sq->out_req_list);
        while (!QTAILQ_EMPTY(&req->aio_tailq)) {
            aio = QTAILQ_FIRST(&req->aio_tailq);
            assert(aio->aiocb);
            blk_aio_cancel(aio->aiocb);
        }
    }
    if (!nvme_check_cqid(n, sq->cqid)) {
        cq = n->cq[sq->cqid];
        QTAILQ_REMOVE(&cq->sq_list, sq, entry);

        nvme_post_cqes(cq);
        QTAILQ_FOREACH_SAFE(req, &cq->req_list, entry, next) {
            if (req->sq == sq) {
                QTAILQ_REMOVE(&cq->req_list, req, entry);
                QTAILQ_INSERT_TAIL(&sq->req_list, req, entry);
            }
        }
    }

    nvme_free_sq(sq, n);
    return NVME_SUCCESS;
}

static void nvme_init_sq(NvmeSQueue *sq, NvmeCtrl *n, uint64_t dma_addr,
    uint16_t sqid, uint16_t cqid, uint16_t size)
{
    int i;
    NvmeCQueue *cq;

    sq->ctrl = n;
    sq->dma_addr = dma_addr;
    sq->sqid = sqid;
    sq->size = size;
    sq->cqid = cqid;
    sq->head = sq->tail = 0;
    sq->io_req = g_new0(NvmeRequest, sq->size);

    QTAILQ_INIT(&sq->req_list);
    QTAILQ_INIT(&sq->out_req_list);
    for (i = 0; i < sq->size; i++) {
        sq->io_req[i].sq = sq;
        QTAILQ_INIT(&(sq->io_req[i].aio_tailq));
        QTAILQ_INSERT_TAIL(&(sq->req_list), &sq->io_req[i], entry);
    }
    sq->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nvme_process_sq, sq);

    assert(n->cq[cqid]);
    cq = n->cq[cqid];
    QTAILQ_INSERT_TAIL(&(cq->sq_list), sq, entry);
    n->sq[sqid] = sq;
}

static uint16_t nvme_create_sq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeSQueue *sq;
    NvmeCreateSq *c = (NvmeCreateSq *)cmd;

    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t sqid = le16_to_cpu(c->sqid);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->sq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    trace_nvme_dev_create_sq(prp1, sqid, cqid, qsize, qflags);

    if (unlikely(!cqid || nvme_check_cqid(n, cqid))) {
        trace_nvme_dev_err_invalid_create_sq_cqid(cqid);
        return NVME_INVALID_CQID | NVME_DNR;
    }
    if (unlikely(!sqid || !nvme_check_sqid(n, sqid))) {
        trace_nvme_dev_err_invalid_create_sq_sqid(sqid);
        return NVME_INVALID_QID | NVME_DNR;
    }
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(n->bar.cap))) {
        trace_nvme_dev_err_invalid_create_sq_size(qsize);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (unlikely(!prp1 || prp1 & (n->page_size - 1))) {
        trace_nvme_dev_err_invalid_create_sq_addr(prp1);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (unlikely(!(NVME_SQ_FLAGS_PC(qflags)))) {
        trace_nvme_dev_err_invalid_create_sq_qflags(NVME_SQ_FLAGS_PC(qflags));
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    sq = g_malloc0(sizeof(*sq));
    nvme_init_sq(sq, n, prp1, sqid, cqid, qsize + 1);
    return NVME_SUCCESS;
}

static uint16_t nvme_smart_info(NvmeCtrl *n, NvmeCmd *cmd, uint8_t rae,
    uint32_t buf_len, uint64_t off, NvmeRequest *req)
{
    uint32_t nsid = le32_to_cpu(cmd->nsid);

    uint32_t trans_len;
    time_t current_ms;
    uint64_t units_read = 0, units_written = 0, read_commands = 0,
        write_commands = 0;
    NvmeSmartLog smart;

    if (nsid && nsid != 0xffffffff) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    for (int i = 1; i <= n->num_namespaces; i++) {
        NvmeNamespace *ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        BlockAcctStats *s = blk_get_stats(ns->blk);

        units_read += s->nr_bytes[BLOCK_ACCT_READ] >> BDRV_SECTOR_BITS;
        units_written += s->nr_bytes[BLOCK_ACCT_WRITE] >> BDRV_SECTOR_BITS;
        read_commands += s->nr_ops[BLOCK_ACCT_READ];
        write_commands += s->nr_ops[BLOCK_ACCT_WRITE];
    }

    if (off > sizeof(smart)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(sizeof(smart) - off, buf_len);

    memset(&smart, 0x0, sizeof(smart));

    smart.data_units_read[0] = cpu_to_le64(units_read / 1000);
    smart.data_units_written[0] = cpu_to_le64(units_written / 1000);
    smart.host_read_commands[0] = cpu_to_le64(read_commands);
    smart.host_write_commands[0] = cpu_to_le64(write_commands);

    smart.temperature[0] = n->temperature & 0xff;
    smart.temperature[1] = (n->temperature >> 8) & 0xff;

    if ((n->temperature > n->features.temp_thresh_hi) ||
        (n->temperature < n->features.temp_thresh_low)) {
        smart.critical_warning |= NVME_SMART_TEMPERATURE;
    }

    current_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    smart.power_on_hours[0] = cpu_to_le64(
        (((current_ms - n->starttime_ms) / 1000) / 60) / 60);

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_SMART);
    }

    return nvme_dma(n, (uint8_t *) &smart + off, trans_len, cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_fw_log_info(NvmeCtrl *n, NvmeCmd *cmd, uint32_t buf_len,
    uint64_t off, NvmeRequest *req)
{
    uint32_t trans_len;
    NvmeFwSlotInfoLog fw_log;

    if (off > sizeof(fw_log)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    memset(&fw_log, 0, sizeof(NvmeFwSlotInfoLog));

    trans_len = MIN(sizeof(fw_log) - off, buf_len);

    return nvme_dma(n, (uint8_t *) &fw_log + off, trans_len, cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_get_log(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint8_t  lid = dw10 & 0xff;
    uint8_t  lsp = (dw10 >> 8) & 0xf;
    uint8_t  rae = (dw10 >> 15) & 0x1;
    uint32_t numdl, numdu;
    uint64_t off, lpol, lpou;
    size_t   len;
    uint16_t status;

    numdl = (dw10 >> 16);
    numdu = (dw11 & 0xffff);
    lpol = dw12;
    lpou = dw13;

    len = (((numdu << 16) | numdl) + 1) << 2;
    off = (lpou << 32ULL) | lpol;

    if (off & 0x3) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trace_nvme_dev_get_log(nvme_cid(req), lid, lsp, rae, len, off);

    status = nvme_check_mdts(n, len, req);
    if (status) {
        return status;
    }

    switch (lid) {
    case NVME_LOG_ERROR_INFO:
        if (!rae) {
            nvme_clear_events(n, NVME_AER_TYPE_ERROR);
        }

        if (off) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        return NVME_SUCCESS;
    case NVME_LOG_SMART_INFO:
        return nvme_smart_info(n, cmd, rae, len, off, req);
    case NVME_LOG_FW_SLOT_INFO:
        return nvme_fw_log_info(n, cmd, len, off, req);
    default:
        trace_nvme_dev_err_invalid_log_page(nvme_cid(req), lid);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static void nvme_free_cq(NvmeCQueue *cq, NvmeCtrl *n)
{
    n->cq[cq->cqid] = NULL;
    timer_del(cq->timer);
    timer_free(cq->timer);
    msix_vector_unuse(&n->parent_obj, cq->vector);
    if (cq->cqid) {
        g_free(cq);
    }
}

static uint16_t nvme_del_cq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)cmd;
    NvmeCQueue *cq;
    uint16_t qid = le16_to_cpu(c->qid);

    if (unlikely(!qid || nvme_check_cqid(n, qid))) {
        trace_nvme_dev_err_invalid_del_cq_cqid(qid);
        return NVME_INVALID_CQID | NVME_DNR;
    }

    cq = n->cq[qid];
    if (unlikely(!QTAILQ_EMPTY(&cq->sq_list))) {
        trace_nvme_dev_err_invalid_del_cq_notempty(qid);
        return NVME_INVALID_QUEUE_DEL;
    }
    nvme_irq_deassert(n, cq);
    trace_nvme_dev_del_cq(qid);
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
    QTAILQ_INIT(&cq->req_list);
    QTAILQ_INIT(&cq->sq_list);
    msix_vector_use(&n->parent_obj, cq->vector);
    n->cq[cqid] = cq;
    cq->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nvme_post_cqes, cq);
}

static uint16_t nvme_create_cq(NvmeCtrl *n, NvmeCmd *cmd)
{
    NvmeCQueue *cq;
    NvmeCreateCq *c = (NvmeCreateCq *)cmd;
    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t vector = le16_to_cpu(c->irq_vector);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->cq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    trace_nvme_dev_create_cq(prp1, cqid, vector, qsize, qflags,
                         NVME_CQ_FLAGS_IEN(qflags) != 0);

    if (unlikely(!cqid || !nvme_check_cqid(n, cqid))) {
        trace_nvme_dev_err_invalid_create_cq_cqid(cqid);
        return NVME_INVALID_CQID | NVME_DNR;
    }
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(n->bar.cap))) {
        trace_nvme_dev_err_invalid_create_cq_size(qsize);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (unlikely(!prp1)) {
        trace_nvme_dev_err_invalid_create_cq_addr(prp1);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (unlikely(vector > n->params.num_queues)) {
        trace_nvme_dev_err_invalid_create_cq_vector(vector);
        return NVME_INVALID_IRQ_VECTOR | NVME_DNR;
    }
    if (unlikely(!(NVME_CQ_FLAGS_PC(qflags)))) {
        trace_nvme_dev_err_invalid_create_cq_qflags(NVME_CQ_FLAGS_PC(qflags));
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    cq = g_malloc0(sizeof(*cq));
    nvme_init_cq(cq, n, prp1, cqid, vector, qsize + 1,
        NVME_CQ_FLAGS_IEN(qflags));

    n->qs_created = true;
    return NVME_SUCCESS;
}

static uint16_t nvme_identify_ctrl(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    trace_nvme_dev_identify_ctrl();

    return nvme_dma(n, (uint8_t *) &n->id_ctrl, sizeof(n->id_ctrl), cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_ns(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeIdNs *id_ns, inactive = { 0 };
    uint32_t nsid = le32_to_cpu(cmd->nsid);
    NvmeNamespace *ns = nvme_ns(n, nsid);

    trace_nvme_dev_identify_ns(nsid);

    if (unlikely(!ns)) {
        uint16_t status = nvme_nsid_err(n, nsid);

        if (!nvme_status_is_error(status, NVME_INVALID_FIELD)) {
            return status;
        }

        id_ns = &inactive;
    } else {
        id_ns = &ns->id_ns;
    }

    return nvme_dma(n, (uint8_t *) id_ns, sizeof(NvmeIdNs), cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_ns_list(NvmeCtrl *n, NvmeCmd *cmd,
    NvmeRequest *req)
{
    static const int data_len = 4 * KiB;
    uint32_t min_nsid = le32_to_cpu(cmd->nsid);
    uint32_t *list;
    uint16_t ret;
    int i, j = 0;

    trace_nvme_dev_identify_ns_list(min_nsid);

    list = g_malloc0(data_len);
    for (i = 1; i <= n->num_namespaces; i++) {
        if (i <= min_nsid || !nvme_ns(n, i)) {
            continue;
        }
        list[j++] = cpu_to_le32(i);
        if (j == data_len / sizeof(uint32_t)) {
            break;
        }
    }
    ret = nvme_dma(n, (uint8_t *) list, data_len, cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
    g_free(list);
    return ret;
}

static uint16_t nvme_identify_ns_descr_list(NvmeCtrl *n, NvmeCmd *cmd,
    NvmeRequest *req)
{
    static const int len = 4096;

    struct ns_descr {
        uint8_t nidt;
        uint8_t nidl;
        uint8_t rsvd2[2];
        uint8_t nid[16];
    };

    uint32_t nsid = le32_to_cpu(cmd->nsid);

    struct ns_descr *list;
    uint16_t ret;

    trace_nvme_dev_identify_ns_descr_list(nsid);

    if (unlikely(!nvme_ns(n, nsid))) {
        return nvme_nsid_err(n, nsid);
    }

    /*
     * Because the NGUID and EUI64 fields are 0 in the Identify Namespace data
     * structure, a Namespace UUID (nidt = 0x3) must be reported in the
     * Namespace Identification Descriptor. Add a very basic Namespace UUID
     * here.
     */
    list = g_malloc0(len);
    list->nidt = 0x3;
    list->nidl = 0x10;
    *(uint32_t *) &list->nid[12] = cpu_to_be32(nsid);

    ret = nvme_dma(n, (uint8_t *) list, len, cmd, DMA_DIRECTION_FROM_DEVICE,
        req);
    g_free(list);
    return ret;
}

static uint16_t nvme_identify(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)cmd;

    switch (le32_to_cpu(c->cns)) {
    case 0x00:
        return nvme_identify_ns(n, cmd, req);
    case 0x01:
        return nvme_identify_ctrl(n, cmd, req);
    case 0x02:
        return nvme_identify_ns_list(n, cmd, req);
    case 0x03:
        return nvme_identify_ns_descr_list(n, cmd, req);
    default:
        trace_nvme_dev_err_invalid_identify_cns(le32_to_cpu(c->cns));
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static uint16_t nvme_abort(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    uint16_t sqid = le32_to_cpu(cmd->cdw10) & 0xffff;

    req->cqe.result = 1;
    if (nvme_check_sqid(n, sqid)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static inline void nvme_set_timestamp(NvmeCtrl *n, uint64_t ts)
{
    trace_nvme_dev_setfeat_timestamp(ts);

    n->host_timestamp = le64_to_cpu(ts);
    n->timestamp_set_qemu_clock_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
}

static inline uint64_t nvme_get_timestamp(const NvmeCtrl *n)
{
    uint64_t current_time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed_time = current_time - n->timestamp_set_qemu_clock_ms;

    union nvme_timestamp {
        struct {
            uint64_t timestamp:48;
            uint64_t sync:1;
            uint64_t origin:3;
            uint64_t rsvd1:12;
        };
        uint64_t all;
    };

    union nvme_timestamp ts;
    ts.all = 0;

    /*
     * If the sum of the Timestamp value set by the host and the elapsed
     * time exceeds 2^48, the value returned should be reduced modulo 2^48.
     */
    ts.timestamp = (n->host_timestamp + elapsed_time) & 0xffffffffffff;

    /* If the host timestamp is non-zero, set the timestamp origin */
    ts.origin = n->host_timestamp ? 0x01 : 0x00;

    trace_nvme_dev_getfeat_timestamp(ts.all);

    return cpu_to_le64(ts.all);
}

static uint16_t nvme_get_feature_timestamp(NvmeCtrl *n, NvmeCmd *cmd,
    NvmeRequest *req)
{
    uint64_t timestamp = nvme_get_timestamp(n);

    return nvme_dma(n, (uint8_t *)&timestamp, sizeof(timestamp), cmd,
        DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_get_feature(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t result;

    trace_nvme_dev_getfeat(nvme_cid(req), dw10);

    switch (dw10) {
    case NVME_ARBITRATION:
        result = cpu_to_le32(n->features.arbitration);
        break;
    case NVME_POWER_MANAGEMENT:
        result = cpu_to_le32(n->features.power_mgmt);
        break;
    case NVME_TEMPERATURE_THRESHOLD:
        result = 0;

        /*
         * The controller only implements the Composite Temperature sensor, so
         * return 0 for all other sensors.
         */
        if (NVME_TEMP_TMPSEL(dw10)) {
            break;
        }

        switch (NVME_TEMP_THSEL(dw10)) {
        case 0x0:
            result = cpu_to_le16(n->features.temp_thresh_hi);
            break;
        case 0x1:
            result = cpu_to_le16(n->features.temp_thresh_low);
            break;
        }

        break;
    case NVME_ERROR_RECOVERY:
        result = cpu_to_le32(n->features.err_rec);
        break;
    case NVME_VOLATILE_WRITE_CACHE:
        result = cpu_to_le32(n->features.volatile_wc);
        trace_nvme_dev_getfeat_vwcache(result ? "enabled" : "disabled");
        break;
    case NVME_NUMBER_OF_QUEUES:
        result = cpu_to_le32((n->params.num_queues - 2) |
            ((n->params.num_queues - 2) << 16));
        trace_nvme_dev_getfeat_numq(result);
        break;
    case NVME_TIMESTAMP:
        return nvme_get_feature_timestamp(n, cmd, req);
    case NVME_INTERRUPT_COALESCING:
        result = cpu_to_le32(n->features.int_coalescing);
        break;
    case NVME_INTERRUPT_VECTOR_CONF:
        if ((dw11 & 0xffff) > n->params.num_queues) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        result = cpu_to_le32(n->features.int_vector_config[dw11 & 0xffff]);
        break;
    case NVME_WRITE_ATOMICITY:
        result = cpu_to_le32(n->features.write_atomicity);
        break;
    case NVME_ASYNCHRONOUS_EVENT_CONF:
        result = cpu_to_le32(n->features.async_config);
        break;
    default:
        trace_nvme_dev_err_invalid_getfeat(dw10);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    req->cqe.result = result;
    return NVME_SUCCESS;
}

static uint16_t nvme_set_feature_timestamp(NvmeCtrl *n, NvmeCmd *cmd,
    NvmeRequest *req)
{
    uint16_t ret;
    uint64_t timestamp;

    ret = nvme_dma(n, (uint8_t *) &timestamp, sizeof(timestamp), cmd,
        DMA_DIRECTION_TO_DEVICE, req);
    if (ret != NVME_SUCCESS) {
        return ret;
    }

    nvme_set_timestamp(n, timestamp);

    return NVME_SUCCESS;
}

static uint16_t nvme_set_feature(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeNamespace *ns;

    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);

    trace_nvme_dev_setfeat(nvme_cid(req), dw10, dw11);

    switch (dw10) {
    case NVME_TEMPERATURE_THRESHOLD:
        if (NVME_TEMP_TMPSEL(dw11)) {
            break;
        }

        switch (NVME_TEMP_THSEL(dw11)) {
        case 0x0:
            n->features.temp_thresh_hi = NVME_TEMP_TMPTH(dw11);
            break;
        case 0x1:
            n->features.temp_thresh_low = NVME_TEMP_TMPTH(dw11);
            break;
        default:
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        if (((n->temperature > n->features.temp_thresh_hi) ||
            (n->temperature < n->features.temp_thresh_low)) &&
            NVME_AEC_SMART(n->features.async_config) & NVME_SMART_TEMPERATURE) {
            nvme_enqueue_event(n, NVME_AER_TYPE_SMART,
                NVME_AER_INFO_SMART_TEMP_THRESH, NVME_LOG_SMART_INFO);
        }

        break;
    case NVME_VOLATILE_WRITE_CACHE:
        n->features.volatile_wc = dw11;

        for (int i = 1; i <= n->num_namespaces; i++) {
            ns = nvme_ns(n, i);
            if (!ns) {
                continue;
            }

            blk_set_enable_write_cache(ns->blk, dw11 & 1);
        }

        break;

    case NVME_NUMBER_OF_QUEUES:
        if (n->qs_created) {
            return NVME_CMD_SEQ_ERROR | NVME_DNR;
        }

        if ((dw11 & 0xffff) == 0xffff || ((dw11 >> 16) & 0xffff) == 0xffff) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        trace_nvme_dev_setfeat_numq((dw11 & 0xFFFF) + 1,
            ((dw11 >> 16) & 0xFFFF) + 1, n->params.num_queues - 1,
            n->params.num_queues - 1);
        req->cqe.result = cpu_to_le32((n->params.num_queues - 2) |
            ((n->params.num_queues - 2) << 16));
        break;
    case NVME_TIMESTAMP:
        return nvme_set_feature_timestamp(n, cmd, req);
    case NVME_ASYNCHRONOUS_EVENT_CONF:
        n->features.async_config = dw11;
        break;
    case NVME_ARBITRATION:
    case NVME_POWER_MANAGEMENT:
    case NVME_ERROR_RECOVERY:
    case NVME_INTERRUPT_COALESCING:
    case NVME_INTERRUPT_VECTOR_CONF:
    case NVME_WRITE_ATOMICITY:
        return NVME_FEAT_NOT_CHANGABLE | NVME_DNR;
    default:
        trace_nvme_dev_err_invalid_setfeat(dw10);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_aer(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    trace_nvme_dev_aer(nvme_cid(req));

    if (n->outstanding_aers > n->params.aerl) {
        trace_nvme_dev_aer_aerl_exceeded();
        return NVME_AER_LIMIT_EXCEEDED;
    }

    n->aer_reqs[n->outstanding_aers] = req;
    n->outstanding_aers++;

    if (!QTAILQ_EMPTY(&n->aer_queue)) {
        nvme_process_aers(n);
    }

    return NVME_NO_COMPLETE;
}

static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    switch (cmd->opcode) {
    case NVME_ADM_CMD_DELETE_SQ:
        return nvme_del_sq(n, cmd);
    case NVME_ADM_CMD_CREATE_SQ:
        return nvme_create_sq(n, cmd);
    case NVME_ADM_CMD_GET_LOG_PAGE:
        return nvme_get_log(n, cmd, req);
    case NVME_ADM_CMD_DELETE_CQ:
        return nvme_del_cq(n, cmd);
    case NVME_ADM_CMD_CREATE_CQ:
        return nvme_create_cq(n, cmd);
    case NVME_ADM_CMD_IDENTIFY:
        return nvme_identify(n, cmd, req);
    case NVME_ADM_CMD_ABORT:
        return nvme_abort(n, cmd, req);
    case NVME_ADM_CMD_SET_FEATURES:
        return nvme_set_feature(n, cmd, req);
    case NVME_ADM_CMD_GET_FEATURES:
        return nvme_get_feature(n, cmd, req);
    case NVME_ADM_CMD_ASYNC_EV_REQ:
        return nvme_aer(n, cmd, req);
    default:
        trace_nvme_dev_err_invalid_admin_opc(cmd->opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static void nvme_process_sq(void *opaque)
{
    NvmeSQueue *sq = opaque;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];

    uint16_t status;
    hwaddr addr;
    NvmeCmd cmd;
    NvmeRequest *req;

    while (!(nvme_sq_empty(sq) || QTAILQ_EMPTY(&sq->req_list))) {
        addr = sq->dma_addr + sq->head * n->sqe_size;
        if (nvme_addr_read(n, addr, (void *)&cmd, sizeof(cmd))) {
            trace_nvme_dev_err_addr_read(addr);
            timer_mod(sq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                100 * SCALE_MS);
            break;
        }
        nvme_inc_sq_head(sq);

        req = QTAILQ_FIRST(&sq->req_list);
        QTAILQ_REMOVE(&sq->req_list, req, entry);
        QTAILQ_INSERT_TAIL(&sq->out_req_list, req, entry);

        nvme_req_clear(req);

        req->cqe.cid = cmd.cid;
        memcpy(&req->cmd, &cmd, sizeof(NvmeCmd));

        status = sq->sqid ? nvme_io_cmd(n, &cmd, req) :
            nvme_admin_cmd(n, &cmd, req);
        if (status != NVME_NO_COMPLETE) {
            req->status = status;
            nvme_enqueue_req_completion(cq, req);
        }
    }
}

static void nvme_clear_ctrl(NvmeCtrl *n)
{
    NvmeNamespace *ns;
    int i;

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        blk_drain(ns->blk);
    }

    for (i = 0; i < n->params.num_queues; i++) {
        if (n->sq[i] != NULL) {
            nvme_free_sq(n->sq[i], n);
        }
    }
    for (i = 0; i < n->params.num_queues; i++) {
        if (n->cq[i] != NULL) {
            nvme_free_cq(n->cq[i], n);
        }
    }

    while (!QTAILQ_EMPTY(&n->aer_queue)) {
        NvmeAsyncEvent *event = QTAILQ_FIRST(&n->aer_queue);
        QTAILQ_REMOVE(&n->aer_queue, event, entry);
        g_free(event);
    }

    n->aer_queued = 0;
    n->outstanding_aers = 0;
    n->qs_created = false;

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        blk_flush(ns->blk);
    }

    n->bar.cc = 0;
}

static int nvme_start_ctrl(NvmeCtrl *n)
{
    uint32_t page_bits = NVME_CC_MPS(n->bar.cc) + 12;
    uint32_t page_size = 1 << page_bits;

    if (unlikely(n->cq[0])) {
        trace_nvme_dev_err_startfail_cq();
        return -1;
    }
    if (unlikely(n->sq[0])) {
        trace_nvme_dev_err_startfail_sq();
        return -1;
    }
    if (unlikely(!n->bar.asq)) {
        trace_nvme_dev_err_startfail_nbarasq();
        return -1;
    }
    if (unlikely(!n->bar.acq)) {
        trace_nvme_dev_err_startfail_nbaracq();
        return -1;
    }
    if (unlikely(n->bar.asq & (page_size - 1))) {
        trace_nvme_dev_err_startfail_asq_misaligned(n->bar.asq);
        return -1;
    }
    if (unlikely(n->bar.acq & (page_size - 1))) {
        trace_nvme_dev_err_startfail_acq_misaligned(n->bar.acq);
        return -1;
    }
    if (unlikely(NVME_CC_MPS(n->bar.cc) <
                 NVME_CAP_MPSMIN(n->bar.cap))) {
        trace_nvme_dev_err_startfail_page_too_small(
                    NVME_CC_MPS(n->bar.cc),
                    NVME_CAP_MPSMIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_MPS(n->bar.cc) >
                 NVME_CAP_MPSMAX(n->bar.cap))) {
        trace_nvme_dev_err_startfail_page_too_large(
                    NVME_CC_MPS(n->bar.cc),
                    NVME_CAP_MPSMAX(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOCQES(n->bar.cc) <
                 NVME_CTRL_CQES_MIN(n->id_ctrl.cqes))) {
        trace_nvme_dev_err_startfail_cqent_too_small(
                    NVME_CC_IOCQES(n->bar.cc),
                    NVME_CTRL_CQES_MIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOCQES(n->bar.cc) >
                 NVME_CTRL_CQES_MAX(n->id_ctrl.cqes))) {
        trace_nvme_dev_err_startfail_cqent_too_large(
                    NVME_CC_IOCQES(n->bar.cc),
                    NVME_CTRL_CQES_MAX(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(n->bar.cc) <
                 NVME_CTRL_SQES_MIN(n->id_ctrl.sqes))) {
        trace_nvme_dev_err_startfail_sqent_too_small(
                    NVME_CC_IOSQES(n->bar.cc),
                    NVME_CTRL_SQES_MIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(n->bar.cc) >
                 NVME_CTRL_SQES_MAX(n->id_ctrl.sqes))) {
        trace_nvme_dev_err_startfail_sqent_too_large(
                    NVME_CC_IOSQES(n->bar.cc),
                    NVME_CTRL_SQES_MAX(n->bar.cap));
        return -1;
    }
    if (unlikely(!NVME_AQA_ASQS(n->bar.aqa))) {
        trace_nvme_dev_err_startfail_asqent_sz_zero();
        return -1;
    }
    if (unlikely(!NVME_AQA_ACQS(n->bar.aqa))) {
        trace_nvme_dev_err_startfail_acqent_sz_zero();
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

    nvme_set_timestamp(n, 0ULL);

    QTAILQ_INIT(&n->aer_queue);

    return 0;
}

static void nvme_write_bar(NvmeCtrl *n, hwaddr offset, uint64_t data,
    unsigned size)
{
    if (unlikely(offset & (sizeof(uint32_t) - 1))) {
        NVME_GUEST_ERR(nvme_dev_ub_mmiowr_misaligned32,
                       "MMIO write not 32-bit aligned,"
                       " offset=0x%"PRIx64"", offset);
        /* should be ignored, fall through for now */
    }

    if (unlikely(size < sizeof(uint32_t))) {
        NVME_GUEST_ERR(nvme_dev_ub_mmiowr_toosmall,
                       "MMIO write smaller than 32-bits,"
                       " offset=0x%"PRIx64", size=%u",
                       offset, size);
        /* should be ignored, fall through for now */
    }

    switch (offset) {
    case 0xc:   /* INTMS */
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
            NVME_GUEST_ERR(nvme_dev_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask set"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        n->bar.intms |= data & 0xffffffff;
        n->bar.intmc = n->bar.intms;
        trace_nvme_dev_mmio_intm_set(data & 0xffffffff,
                                 n->bar.intmc);
        nvme_irq_check(n);
        break;
    case 0x10:  /* INTMC */
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
            NVME_GUEST_ERR(nvme_dev_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask clr"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        n->bar.intms &= ~(data & 0xffffffff);
        n->bar.intmc = n->bar.intms;
        trace_nvme_dev_mmio_intm_clr(data & 0xffffffff,
                                 n->bar.intmc);
        nvme_irq_check(n);
        break;
    case 0x14:  /* CC */
        trace_nvme_dev_mmio_cfg(data & 0xffffffff);
        /* Windows first sends data, then sends enable bit */
        if (!NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc) &&
            !NVME_CC_SHN(data) && !NVME_CC_SHN(n->bar.cc))
        {
            n->bar.cc = data;
        }

        if (NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc)) {
            n->bar.cc = data;
            if (unlikely(nvme_start_ctrl(n))) {
                trace_nvme_dev_err_startfail();
                n->bar.csts = NVME_CSTS_FAILED;
            } else {
                trace_nvme_dev_mmio_start_success();
                n->bar.csts = NVME_CSTS_READY;
            }
        } else if (!NVME_CC_EN(data) && NVME_CC_EN(n->bar.cc)) {
            trace_nvme_dev_mmio_stopped();
            nvme_clear_ctrl(n);
            n->bar.csts &= ~NVME_CSTS_READY;
        }
        if (NVME_CC_SHN(data) && !(NVME_CC_SHN(n->bar.cc))) {
            trace_nvme_dev_mmio_shutdown_set();
            nvme_clear_ctrl(n);
            n->bar.cc = data;
            n->bar.csts |= NVME_CSTS_SHST_COMPLETE;
        } else if (!NVME_CC_SHN(data) && NVME_CC_SHN(n->bar.cc)) {
            trace_nvme_dev_mmio_shutdown_cleared();
            n->bar.csts &= ~NVME_CSTS_SHST_COMPLETE;
            n->bar.cc = data;
        }
        break;
    case 0x1C:  /* CSTS */
        if (data & (1 << 4)) {
            NVME_GUEST_ERR(nvme_dev_ub_mmiowr_ssreset_w1c_unsupported,
                           "attempted to W1C CSTS.NSSRO"
                           " but CAP.NSSRS is zero (not supported)");
        } else if (data != 0) {
            NVME_GUEST_ERR(nvme_dev_ub_mmiowr_ro_csts,
                           "attempted to set a read only bit"
                           " of controller status");
        }
        break;
    case 0x20:  /* NSSR */
        if (data == 0x4E564D65) {
            trace_nvme_dev_ub_mmiowr_ssreset_unsupported();
        } else {
            /* The spec says that writes of other values have no effect */
            return;
        }
        break;
    case 0x24:  /* AQA */
        n->bar.aqa = data & 0xffffffff;
        trace_nvme_dev_mmio_aqattr(data & 0xffffffff);
        break;
    case 0x28:  /* ASQ */
        n->bar.asq = data;
        trace_nvme_dev_mmio_asqaddr(data);
        break;
    case 0x2c:  /* ASQ hi */
        n->bar.asq |= data << 32;
        trace_nvme_dev_mmio_asqaddr_hi(data, n->bar.asq);
        break;
    case 0x30:  /* ACQ */
        trace_nvme_dev_mmio_acqaddr(data);
        n->bar.acq = data;
        break;
    case 0x34:  /* ACQ hi */
        n->bar.acq |= data << 32;
        trace_nvme_dev_mmio_acqaddr_hi(data, n->bar.acq);
        break;
    case 0x38:  /* CMBLOC */
        NVME_GUEST_ERR(nvme_dev_ub_mmiowr_cmbloc_reserved,
                       "invalid write to reserved CMBLOC"
                       " when CMBSZ is zero, ignored");
        return;
    case 0x3C:  /* CMBSZ */
        NVME_GUEST_ERR(nvme_dev_ub_mmiowr_cmbsz_readonly,
                       "invalid write to read only CMBSZ, ignored");
        return;
    default:
        NVME_GUEST_ERR(nvme_dev_ub_mmiowr_invalid,
                       "invalid MMIO write,"
                       " offset=0x%"PRIx64", data=%"PRIx64"",
                       offset, data);
        break;
    }
}

static uint64_t nvme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    uint8_t *ptr = (uint8_t *)&n->bar;
    uint64_t val = 0;

    if (unlikely(addr & (sizeof(uint32_t) - 1))) {
        NVME_GUEST_ERR(nvme_dev_ub_mmiord_misaligned32,
                       "MMIO read not 32-bit aligned,"
                       " offset=0x%"PRIx64"", addr);
        /* should RAZ, fall through for now */
    } else if (unlikely(size < sizeof(uint32_t))) {
        NVME_GUEST_ERR(nvme_dev_ub_mmiord_toosmall,
                       "MMIO read smaller than 32-bits,"
                       " offset=0x%"PRIx64"", addr);
        /* should RAZ, fall through for now */
    }

    if (addr < sizeof(n->bar)) {
        memcpy(&val, ptr + addr, size);
    } else {
        NVME_GUEST_ERR(nvme_dev_ub_mmiord_invalid_ofs,
                       "MMIO read beyond last register,"
                       " offset=0x%"PRIx64", returning 0", addr);
    }

    return val;
}

static void nvme_process_db(NvmeCtrl *n, hwaddr addr, int val)
{
    uint32_t qid;

    if (unlikely(addr & ((1 << 2) - 1))) {
        NVME_GUEST_ERR(nvme_dev_ub_db_wr_misaligned,
                       "doorbell write not 32-bit aligned,"
                       " offset=0x%"PRIx64", ignoring", addr);
        return;
    }

    if (((addr - 0x1000) >> 2) & 1) {
        /* Completion queue doorbell write */

        uint16_t new_head = val & 0xffff;
        int start_sqs;
        NvmeCQueue *cq;

        qid = (addr - (0x1000 + (1 << 2))) >> 3;
        if (unlikely(nvme_check_cqid(n, qid))) {
            NVME_GUEST_ERR(nvme_dev_ub_db_wr_invalid_cq,
                           "completion queue doorbell write"
                           " for nonexistent queue,"
                           " sqid=%"PRIu32", ignoring", qid);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                    NVME_AER_INFO_ERR_INVALID_DB_REGISTER,
                    NVME_LOG_ERROR_INFO);
            }

            return;
        }

        cq = n->cq[qid];
        if (unlikely(new_head >= cq->size)) {
            NVME_GUEST_ERR(nvme_dev_ub_db_wr_invalid_cqhead,
                           "completion queue doorbell write value"
                           " beyond queue size, sqid=%"PRIu32","
                           " new_head=%"PRIu16", ignoring",
                           qid, new_head);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                    NVME_AER_INFO_ERR_INVALID_DB_VALUE, NVME_LOG_ERROR_INFO);
            }

            return;
        }

        start_sqs = nvme_cq_full(cq) ? 1 : 0;
        cq->head = new_head;
        if (start_sqs) {
            NvmeSQueue *sq;
            QTAILQ_FOREACH(sq, &cq->sq_list, entry) {
                timer_mod(sq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
            }
            timer_mod(cq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
        }

        if (cq->tail == cq->head) {
            nvme_irq_deassert(n, cq);
        }
    } else {
        /* Submission queue doorbell write */

        uint16_t new_tail = val & 0xffff;
        NvmeSQueue *sq;

        qid = (addr - 0x1000) >> 3;
        if (unlikely(nvme_check_sqid(n, qid))) {
            NVME_GUEST_ERR(nvme_dev_ub_db_wr_invalid_sq,
                           "submission queue doorbell write"
                           " for nonexistent queue,"
                           " sqid=%"PRIu32", ignoring", qid);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                    NVME_AER_INFO_ERR_INVALID_DB_REGISTER,
                    NVME_LOG_ERROR_INFO);
            }

            return;
        }

        sq = n->sq[qid];
        if (unlikely(new_tail >= sq->size)) {
            NVME_GUEST_ERR(nvme_dev_ub_db_wr_invalid_sqtail,
                           "submission queue doorbell write value"
                           " beyond queue size, sqid=%"PRIu32","
                           " new_tail=%"PRIu16", ignoring",
                           qid, new_tail);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                    NVME_AER_INFO_ERR_INVALID_DB_VALUE, NVME_LOG_ERROR_INFO);
            }

            return;
        }

        sq->tail = new_tail;
        timer_mod(sq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
    }
}

static void nvme_mmio_write(void *opaque, hwaddr addr, uint64_t data,
    unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    if (addr < sizeof(n->bar)) {
        nvme_write_bar(n, addr, data, size);
    } else if (addr >= 0x1000) {
        nvme_process_db(n, addr, data);
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

static void nvme_cmb_write(void *opaque, hwaddr addr, uint64_t data,
    unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    stn_le_p(&n->cmbuf[addr], size, data);
}

static uint64_t nvme_cmb_read(void *opaque, hwaddr addr, unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    return ldn_le_p(&n->cmbuf[addr], size);
}

static const MemoryRegionOps nvme_cmb_ops = {
    .read = nvme_cmb_read,
    .write = nvme_cmb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static int nvme_check_constraints(NvmeCtrl *n, Error **errp)
{
    NvmeParams *params = &n->params;

    if (!n->namespace.blk && !n->parent_obj.qdev.id) {
        error_setg(errp, "nvme: invalid 'id' parameter");
        return 1;
    }

    if (!params->serial) {
        error_setg(errp, "nvme: serial not configured");
        return 1;
    }

    if ((params->num_queues < 1 || params->num_queues > NVME_MAX_QS)) {
        error_setg(errp, "nvme: invalid queue configuration");
        return 1;
    }

    return 0;
}

static void nvme_init_state(NvmeCtrl *n)
{
    n->num_namespaces = 0;
    n->reg_size = pow2ceil(0x1004 + 2 * (n->params.num_queues + 1) * 4);
    n->sq = g_new0(NvmeSQueue *, n->params.num_queues);
    n->cq = g_new0(NvmeCQueue *, n->params.num_queues);

    n->temperature = NVME_TEMPERATURE;
    n->features.temp_thresh_hi = le16_to_cpu(n->id_ctrl.wctemp);
    n->starttime_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);

    /*
     * There is no limit on the number of commands that the controller may
     * launch at one time from a particular Submission Queue.
     */
    n->features.arbitration = 0x7;

    n->features.int_vector_config = g_malloc0_n(n->params.num_queues,
        sizeof(*n->features.int_vector_config));

    /* disable coalescing (not supported) */
    for (int i = 0; i < n->params.num_queues; i++) {
        n->features.int_vector_config[i] = i | (1 << 16);
    }

    n->aer_reqs = g_new0(NvmeRequest *, n->params.aerl + 1);
}

static void nvme_init_cmb(NvmeCtrl *n, PCIDevice *pci_dev)
{
    NVME_CMBLOC_SET_BIR(n->bar.cmbloc, 2);
    NVME_CMBLOC_SET_OFST(n->bar.cmbloc, 0);

    NVME_CMBSZ_SET_SQS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_CQS(n->bar.cmbsz, 0);
    NVME_CMBSZ_SET_LISTS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_RDS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_WDS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_SZU(n->bar.cmbsz, 2);
    NVME_CMBSZ_SET_SZ(n->bar.cmbsz, n->params.cmb_size_mb);

    n->cmbloc = n->bar.cmbloc;
    n->cmbsz = n->bar.cmbsz;

    n->cmbuf = g_malloc0(NVME_CMBSZ_GETSIZE(n->bar.cmbsz));
    memory_region_init_io(&n->ctrl_mem, OBJECT(n), &nvme_cmb_ops, n,
                            "nvme-cmb", NVME_CMBSZ_GETSIZE(n->bar.cmbsz));
    pci_register_bar(pci_dev, NVME_CMBLOC_BIR(n->bar.cmbloc),
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64 |
        PCI_BASE_ADDRESS_MEM_PREFETCH, &n->ctrl_mem);
}

static void nvme_init_pci(NvmeCtrl *n, PCIDevice *pci_dev)
{
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x2);
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, 0x5845);
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS);
    pcie_endpoint_cap_init(pci_dev, 0x80);

    memory_region_init_io(&n->iomem, OBJECT(n), &nvme_mmio_ops, n, "nvme",
        n->reg_size);
    pci_register_bar(pci_dev, 0,
        PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
        &n->iomem);
    msix_init_exclusive_bar(pci_dev, n->params.num_queues, 4, NULL);

    if (n->params.cmb_size_mb) {
        nvme_init_cmb(n, pci_dev);
    }
}

static void nvme_init_ctrl(NvmeCtrl *n)
{
    NvmeIdCtrl *id = &n->id_ctrl;
    NvmeParams *params = &n->params;
    uint8_t *pci_conf = n->parent_obj.config;

    id->vid = cpu_to_le16(pci_get_word(pci_conf + PCI_VENDOR_ID));
    id->ssvid = cpu_to_le16(pci_get_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID));
    strpadcpy((char *)id->mn, sizeof(id->mn), "QEMU NVMe Ctrl", ' ');
    strpadcpy((char *)id->fr, sizeof(id->fr), "1.0", ' ');
    strpadcpy((char *)id->sn, sizeof(id->sn), params->serial, ' ');
    id->rab = 6;
    id->ieee[0] = 0x00;
    id->ieee[1] = 0x02;
    id->ieee[2] = 0xb3;
    id->mdts = params->mdts;
    id->ver = cpu_to_le32(NVME_SPEC_VER);
    id->oacs = cpu_to_le16(0);

    /*
     * Because the controller always completes the Abort command immediately,
     * there can never be more than one concurrently executing Abort command,
     * so this value is never used for anything. Note that there can easily be
     * many Abort commands in the queues, but they are not considered
     * "executing" until processed by nvme_abort.
     *
     * The specification recommends a value of 3 for Abort Command Limit (four
     * concurrently outstanding Abort commands), so lets use that though it is
     * inconsequential.
     */
    id->acl = 3;
    id->aerl = n->params.aerl;
    id->frmw = 7 << 1;
    id->lpa = 1 << 2;

    /* recommended default value (~70 C) */
    id->wctemp = cpu_to_le16(0x157);
    id->cctemp = cpu_to_le16(0x175);

    id->sqes = (0x6 << 4) | 0x6;
    id->cqes = (0x4 << 4) | 0x4;
    id->nn = cpu_to_le32(n->num_namespaces);
    id->oncs = cpu_to_le16(NVME_ONCS_WRITE_ZEROS | NVME_ONCS_TIMESTAMP);
    id->vwc = 1;
    id->sgls = cpu_to_le32(0x1);

    strcpy((char *) id->subnqn, "nqn.2019-08.org.qemu:");
    pstrcat((char *) id->subnqn, sizeof(id->subnqn), n->params.serial);

    id->psd[0].mp = cpu_to_le16(0x9c4);
    id->psd[0].enlat = cpu_to_le32(0x10);
    id->psd[0].exlat = cpu_to_le32(0x4);

    n->bar.cap = 0;
    NVME_CAP_SET_MQES(n->bar.cap, 0x7ff);
    NVME_CAP_SET_CQR(n->bar.cap, 1);
    NVME_CAP_SET_TO(n->bar.cap, 0xf);
    NVME_CAP_SET_CSS(n->bar.cap, 1);
    NVME_CAP_SET_MPSMAX(n->bar.cap, 4);

    n->bar.vs = NVME_SPEC_VER;
    n->bar.intmc = n->bar.intms = 0;
}

int nvme_register_namespace(NvmeCtrl *n, NvmeNamespace *ns, Error **errp)
{
    uint32_t nsid = nvme_nsid(ns);

    if (nsid == 0 || nsid > NVME_MAX_NAMESPACES) {
        error_setg(errp, "invalid nsid");
        return 1;
    }

    if (n->namespaces[nsid - 1]) {
        error_setg(errp, "nsid must be unique");
        return 1;
    }

    trace_nvme_dev_register_namespace(nsid);

    n->namespaces[nsid - 1] = ns;
    n->num_namespaces = MAX(n->num_namespaces, nsid);
    n->id_ctrl.nn = cpu_to_le32(n->num_namespaces);

    return 0;
}

static void nvme_realize(PCIDevice *pci_dev, Error **errp)
{
    NvmeCtrl *n = NVME(pci_dev);
    NvmeNamespace *ns;
    Error *local_err = NULL;

    if (nvme_check_constraints(n, &local_err)) {
        error_propagate_prepend(errp, local_err, "nvme_check_constraints: ");
        return;
    }

    qbus_create_inplace(&n->bus, sizeof(NvmeBus), TYPE_NVME_BUS,
        &pci_dev->qdev, n->parent_obj.qdev.id);

    nvme_init_state(n);
    nvme_init_pci(n, pci_dev);
    nvme_init_ctrl(n);

    /* setup a namespace if the controller drive property was given */
    if (n->namespace.blk) {
        ns = &n->namespace;
        ns->params.nsid = 1;

        if (nvme_ns_setup(n, ns, &local_err)) {
            error_propagate_prepend(errp, local_err, "nvme_ns_setup: ");
            return;
        }
    }
}

static void nvme_exit(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME(pci_dev);

    nvme_clear_ctrl(n);
    g_free(n->namespaces);
    g_free(n->cq);
    g_free(n->sq);
    g_free(n->aer_reqs);
    g_free(n->features.int_vector_config);

    if (n->params.cmb_size_mb) {
        g_free(n->cmbuf);
    }
    msix_uninit_exclusive_bar(pci_dev);
}

static Property nvme_props[] = {
    DEFINE_BLOCK_PROPERTIES_BASE(NvmeCtrl, conf), \
    DEFINE_PROP_DRIVE("drive", NvmeCtrl, namespace.blk), \
    DEFINE_NVME_PROPERTIES(NvmeCtrl, params),
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

    pc->realize = nvme_realize;
    pc->exit = nvme_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->vendor_id = PCI_VENDOR_ID_INTEL;
    pc->device_id = 0x5845;
    pc->revision = 2;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Non-Volatile Memory Express";
    dc->props = nvme_props;
    dc->vmsd = &nvme_vmstate;
}

static void nvme_instance_init(Object *obj)
{
    NvmeCtrl *s = NVME(obj);

    if (s->namespace.blk) {
        device_add_bootindex_property(obj, &s->conf.bootindex,
                                      "bootindex", "/namespace@1,0",
                                      DEVICE(obj), &error_abort);
    }
}

static const TypeInfo nvme_info = {
    .name          = TYPE_NVME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NvmeCtrl),
    .instance_init = nvme_instance_init,
    .class_init    = nvme_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static const TypeInfo nvme_bus_info = {
    .name = TYPE_NVME_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(NvmeBus),
};

static void nvme_register_types(void)
{
    type_register_static(&nvme_info);
    type_register_static(&nvme_bus_info);
}

type_init(nvme_register_types)
