#ifndef HW_NVME_H
#define HW_NVME_H

#include "block/nvme.h"

#define DEFINE_NVME_PROPERTIES(_state, _props) \
    DEFINE_PROP_STRING("serial", _state, _props.serial), \
    DEFINE_PROP_UINT32("cmb_size_mb", _state, _props.cmb_size_mb, 0), \
    DEFINE_PROP_UINT32("num_queues", _state, _props.num_queues, 64), \
    DEFINE_PROP_UINT8("aerl", _state, _props.aerl, 3), \
    DEFINE_PROP_UINT32("aer_max_queued", _state, _props.aer_max_queued, 64), \
    DEFINE_PROP_UINT8("mdts", _state, _props.mdts, 7)

typedef struct NvmeParams {
    char     *serial;
    uint32_t num_queues;
    uint32_t cmb_size_mb;
    uint8_t  aerl;
    uint32_t aer_max_queued;
    uint8_t  mdts;
} NvmeParams;

typedef struct NvmeAsyncEvent {
    QTAILQ_ENTRY(NvmeAsyncEvent) entry;
    NvmeAerResult result;
} NvmeAsyncEvent;

typedef struct NvmeRequest NvmeRequest;
typedef void NvmeRequestCompletionFunc(NvmeRequest *req, void *opaque);

struct NvmeRequest {
    struct NvmeSQueue    *sq;
    struct NvmeNamespace *ns;

    NvmeCqe  cqe;
    NvmeCmd  cmd;
    uint16_t status;

    uint64_t slba;
    uint32_t nlb;

    QEMUSGList   qsg;
    QEMUIOVector iov;

    NvmeRequestCompletionFunc *cb;
    void                      *cb_arg;

    QTAILQ_HEAD(, NvmeAIO)    aio_tailq;
    QTAILQ_ENTRY(NvmeRequest) entry;
};

static inline void nvme_req_clear(NvmeRequest *req)
{
    req->ns = NULL;
    memset(&req->cqe, 0, sizeof(req->cqe));
    req->status = NVME_SUCCESS;
    req->slba = req->nlb = 0x0;
    req->cb = req->cb_arg = NULL;
}

static inline void nvme_req_set_cb(NvmeRequest *req,
    NvmeRequestCompletionFunc *cb, void *cb_arg)
{
    req->cb = cb;
    req->cb_arg = cb_arg;
}

static inline void nvme_req_clear_cb(NvmeRequest *req)
{
    req->cb = req->cb_arg = NULL;
}

typedef struct NvmeSQueue {
    struct NvmeCtrl *ctrl;
    uint16_t    sqid;
    uint16_t    cqid;
    uint32_t    head;
    uint32_t    tail;
    uint32_t    size;
    uint64_t    dma_addr;
    QEMUTimer   *timer;
    NvmeRequest *io_req;
    QTAILQ_HEAD(, NvmeRequest) req_list;
    QTAILQ_HEAD(, NvmeRequest) out_req_list;
    QTAILQ_ENTRY(NvmeSQueue) entry;
} NvmeSQueue;

typedef struct NvmeCQueue {
    struct NvmeCtrl *ctrl;
    uint8_t     phase;
    uint16_t    cqid;
    uint16_t    irq_enabled;
    uint32_t    head;
    uint32_t    tail;
    uint32_t    vector;
    uint32_t    size;
    uint64_t    dma_addr;
    QEMUTimer   *timer;
    QTAILQ_HEAD(, NvmeSQueue) sq_list;
    QTAILQ_HEAD(, NvmeRequest) req_list;
} NvmeCQueue;

typedef struct NvmeNamespace {
    NvmeIdNs        id_ns;
} NvmeNamespace;

static inline NvmeLBAF nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;
    return id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t nvme_ns_lbads(NvmeNamespace *ns)
{
    return nvme_ns_lbaf(ns).ds;
}

static inline size_t nvme_ns_lbads_bytes(NvmeNamespace *ns)
{
    return 1 << nvme_ns_lbads(ns);
}

typedef enum NvmeAIOOp {
    NVME_AIO_OPC_NONE         = 0x0,
    NVME_AIO_OPC_FLUSH        = 0x1,
    NVME_AIO_OPC_READ         = 0x2,
    NVME_AIO_OPC_WRITE        = 0x3,
    NVME_AIO_OPC_WRITE_ZEROES = 0x4,
} NvmeAIOOp;

typedef struct NvmeAIO NvmeAIO;
typedef void NvmeAIOCompletionFunc(NvmeAIO *aio, void *opaque);

struct NvmeAIO {
    NvmeRequest *req;

    NvmeAIOOp       opc;
    int64_t         offset;
    size_t          len;
    BlockBackend    *blk;
    BlockAIOCB      *aiocb;
    BlockAcctCookie acct;

    NvmeAIOCompletionFunc *cb;
    void                  *cb_arg;

    QEMUSGList   *qsg;
    QEMUIOVector *iov;

    QTAILQ_ENTRY(NvmeAIO) tailq_entry;
};

static inline const char *nvme_aio_opc_str(NvmeAIO *aio)
{
    switch (aio->opc) {
    case NVME_AIO_OPC_NONE:         return "NVME_AIO_OP_NONE";
    case NVME_AIO_OPC_FLUSH:        return "NVME_AIO_OP_FLUSH";
    case NVME_AIO_OPC_READ:         return "NVME_AIO_OP_READ";
    case NVME_AIO_OPC_WRITE:        return "NVME_AIO_OP_WRITE";
    case NVME_AIO_OPC_WRITE_ZEROES: return "NVME_AIO_OP_WRITE_ZEROES";
    default:                        return "NVME_AIO_OP_UNKNOWN";
    }
}

static inline bool nvme_req_is_write(NvmeRequest *req)
{
    switch (req->cmd.opcode) {
    case NVME_CMD_WRITE:
    case NVME_CMD_WRITE_UNCOR:
    case NVME_CMD_WRITE_ZEROS:
        return true;
    default:
        return false;
    }
}

#define TYPE_NVME "nvme"
#define NVME(obj) \
        OBJECT_CHECK(NvmeCtrl, (obj), TYPE_NVME)

typedef struct NvmeCtrl {
    PCIDevice    parent_obj;
    MemoryRegion iomem;
    MemoryRegion ctrl_mem;
    NvmeBar      bar;
    BlockConf    conf;
    NvmeParams   params;

    bool        qs_created;
    uint32_t    page_size;
    uint16_t    page_bits;
    uint16_t    max_prp_ents;
    uint16_t    cqe_size;
    uint16_t    sqe_size;
    uint32_t    reg_size;
    uint32_t    num_namespaces;
    uint32_t    max_q_ents;
    uint64_t    ns_size;
    uint8_t     outstanding_aers;
    uint32_t    cmbsz;
    uint32_t    cmbloc;
    uint8_t     *cmbuf;
    uint64_t    irq_status;
    uint64_t    host_timestamp;                 /* Timestamp sent by the host */
    uint64_t    timestamp_set_qemu_clock_ms;    /* QEMU clock time */
    uint16_t    temperature;
    uint64_t    starttime_ms;

    uint8_t     aer_mask;
    NvmeRequest **aer_reqs;
    QTAILQ_HEAD(, NvmeAsyncEvent) aer_queue;
    int         aer_queued;

    NvmeNamespace   *namespaces;
    NvmeSQueue      **sq;
    NvmeCQueue      **cq;
    NvmeSQueue      admin_sq;
    NvmeCQueue      admin_cq;
    NvmeIdCtrl      id_ctrl;
    NvmeFeatureVal  features;
} NvmeCtrl;

static inline uint64_t nvme_ns_nlbas(NvmeCtrl *n, NvmeNamespace *ns)
{
    return n->ns_size >> nvme_ns_lbads(ns);
}

static inline uint16_t nvme_cid(NvmeRequest *req)
{
    if (req) {
        return le16_to_cpu(req->cmd.cid);
    }

    return 0xffff;
}

static inline bool nvme_status_is_error(uint16_t status, uint16_t err)
{
    /* strip DNR and MORE */
    return (status & 0xfff) == err;
}

static inline NvmeCtrl *nvme_ctrl(NvmeRequest *req)
{
    return req->sq->ctrl;
}

#endif /* HW_NVME_H */
