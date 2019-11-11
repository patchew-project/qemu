#ifndef HW_NVME_H
#define HW_NVME_H

#include "block/nvme.h"

#define DEFINE_NVME_PROPERTIES(_state, _props) \
    DEFINE_PROP_STRING("serial", _state, _props.serial), \
    DEFINE_PROP_UINT32("cmb_size_mb", _state, _props.cmb_size_mb, 0), \
    DEFINE_PROP_UINT32("num_queues", _state, _props.num_queues, 64), \
    DEFINE_PROP_UINT8("elpe", _state, _props.elpe, 24), \
    DEFINE_PROP_UINT8("aerl", _state, _props.aerl, 3), \
    DEFINE_PROP_UINT8("mdts", _state, _props.mdts, 7)

typedef struct NvmeParams {
    char     *serial;
    uint32_t num_queues;
    uint32_t cmb_size_mb;
    uint8_t  elpe;
    uint8_t  aerl;
    uint8_t  mdts;
} NvmeParams;

typedef struct NvmeAsyncEvent {
    QTAILQ_ENTRY(NvmeAsyncEvent) entry;
    NvmeAerResult result;
} NvmeAsyncEvent;

typedef enum NvmeAIOOp {
    NVME_AIO_OPC_NONE         = 0x0,
    NVME_AIO_OPC_FLUSH        = 0x1,
    NVME_AIO_OPC_READ         = 0x2,
    NVME_AIO_OPC_WRITE        = 0x3,
    NVME_AIO_OPC_WRITE_ZEROES = 0x4,
} NvmeAIOOp;

typedef struct NvmeRequest NvmeRequest;
typedef struct NvmeAIO NvmeAIO;
typedef void NvmeAIOCompletionFunc(NvmeAIO *aio, void *opaque);

struct NvmeAIO {
    NvmeRequest *req;

    NvmeAIOOp       opc;
    int64_t         offset;
    BlockBackend    *blk;
    BlockAIOCB      *aiocb;
    BlockAcctCookie acct;

    NvmeAIOCompletionFunc *cb;
    void                  *cb_arg;

    QEMUSGList   *qsg;
    QEMUIOVector iov;

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

#define NVME_REQ_TRANSFER_DMA  0x1
#define NVME_REQ_TRANSFER_CMB  0x2
#define NVME_REQ_TRANSFER_MASK 0x3

typedef struct NvmeSQueue    NvmeSQueue;
typedef struct NvmeNamespace NvmeNamespace;
typedef void NvmeRequestCompletionFunc(NvmeRequest *req, void *opaque);

struct NvmeRequest {
    NvmeSQueue    *sq;
    NvmeNamespace *ns;
    NvmeCqe       cqe;
    NvmeCmd       cmd;

    uint64_t slba;
    uint32_t nlb;
    uint16_t status;
    uint16_t cid;
    int      flags;

    NvmeRequestCompletionFunc *cb;
    void                      *cb_arg;

    QEMUSGList qsg;

    QTAILQ_HEAD(, NvmeAIO)    aio_tailq;
    QTAILQ_ENTRY(NvmeRequest) entry;
};

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

static inline uint16_t nvme_cid(NvmeRequest *req)
{
    if (req) {
        return req->cid;
    }

    return 0xffff;
}

static inline bool nvme_req_is_cmb(NvmeRequest *req)
{
    return (req->flags & NVME_REQ_TRANSFER_MASK) == NVME_REQ_TRANSFER_CMB;
}

static void nvme_req_set_cmb(NvmeRequest *req)
{
    req->flags = NVME_REQ_TRANSFER_CMB;
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

typedef struct NvmeCtrl NvmeCtrl;

struct NvmeSQueue {
    NvmeCtrl    *ctrl;
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
};

typedef struct NvmeCQueue NvmeCQueue;

struct NvmeCQueue {
    NvmeCtrl    *ctrl;
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
};

struct NvmeNamespace {
    NvmeIdNs        id_ns;
};

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
    uint64_t    starttime_ms;
    uint16_t    temperature;
    uint8_t     elp_index;
    uint64_t    error_count;
    uint32_t    qs_created;

    QEMUTimer   *aer_timer;
    uint8_t     aer_mask;
    uint8_t     aer_mask_queued;
    NvmeRequest **aer_reqs;
    QTAILQ_HEAD(, NvmeAsyncEvent) aer_queue;

    NvmeNamespace   *namespaces;
    NvmeSQueue      **sq;
    NvmeCQueue      **cq;
    NvmeSQueue      admin_sq;
    NvmeCQueue      admin_cq;
    NvmeFeatureVal  features;
    NvmeErrorLog    *elpes;
    NvmeIdCtrl      id_ctrl;
} NvmeCtrl;

static inline NvmeCtrl *nvme_ctrl(NvmeRequest *req)
{
    return req->sq->ctrl;
}

static inline bool nvme_is_error(uint16_t status, uint16_t err)
{
    /* strip DNR and MORE */
    return (status & 0xfff) == err;
}

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

static inline uint64_t nvme_ns_nlbas(NvmeCtrl *n, NvmeNamespace *ns)
{
    return n->ns_size >> nvme_ns_lbads(ns);
}

#endif /* HW_NVME_H */
