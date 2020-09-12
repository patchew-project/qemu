#ifndef HW_NVME_H
#define HW_NVME_H

#include "block/nvme.h"

#define NVME_DEFAULT_ZONE_SIZE   128 /* MiB */
#define NVME_DEFAULT_MAX_ZA_SIZE 128 /* KiB */

typedef struct NvmeParams {
    char     *serial;
    uint32_t num_queues; /* deprecated since 5.1 */
    uint32_t max_ioqpairs;
    uint16_t msix_qsize;
    uint32_t cmb_size_mb;
    uint8_t  aerl;
    uint32_t aer_max_queued;
    uint8_t  mdts;

    bool        zoned;
    bool        cross_zone_read;
    uint8_t     fill_pattern;
    uint32_t    zasl_kb;
    uint64_t    zone_size_mb;
    uint64_t    zone_capacity_mb;
} NvmeParams;

typedef struct NvmeAsyncEvent {
    QTAILQ_ENTRY(NvmeAsyncEvent) entry;
    NvmeAerResult result;
} NvmeAsyncEvent;

typedef struct NvmeRequest {
    struct NvmeSQueue       *sq;
    struct NvmeNamespace    *ns;
    BlockAIOCB              *aiocb;
    uint16_t                status;
    int64_t                 fill_ofs;
    NvmeCqe                 cqe;
    NvmeCmd                 cmd;
    BlockAcctCookie         acct;
    QEMUSGList              qsg;
    QEMUIOVector            iov;
    QTAILQ_ENTRY(NvmeRequest)entry;
} NvmeRequest;

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

typedef struct NvmeZone {
    NvmeZoneDescr   d;
    uint64_t        tstamp;
    uint32_t        next;
    uint32_t        prev;
    uint8_t         rsvd80[8];
} NvmeZone;

#define NVME_ZONE_LIST_NIL    UINT_MAX

typedef struct NvmeZoneList {
    uint32_t        head;
    uint32_t        tail;
    uint32_t        size;
    uint8_t         rsvd12[4];
} NvmeZoneList;

typedef struct NvmeNamespace {
    NvmeIdNs        id_ns;
    uint32_t        nsid;
    uint8_t         csi;
    bool            attached;
    QemuUUID        uuid;

    NvmeIdNsZoned   *id_ns_zoned;
    NvmeZone        *zone_array;
    NvmeZoneList    *exp_open_zones;
    NvmeZoneList    *imp_open_zones;
    NvmeZoneList    *closed_zones;
    NvmeZoneList    *full_zones;
} NvmeNamespace;

static inline NvmeLBAF *nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;
    return &id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t nvme_ns_lbads(NvmeNamespace *ns)
{
    return nvme_ns_lbaf(ns)->ds;
}

#define TYPE_NVME "nvme"
#define NVME(obj) \
        OBJECT_CHECK(NvmeCtrl, (obj), TYPE_NVME)

typedef struct NvmeFeatureVal {
    struct {
        uint16_t temp_thresh_hi;
        uint16_t temp_thresh_low;
    };
    uint32_t    async_config;
} NvmeFeatureVal;

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
    uint8_t     *cmbuf;
    uint32_t    irq_status;
    uint64_t    host_timestamp;                 /* Timestamp sent by the host */
    uint64_t    timestamp_set_qemu_clock_ms;    /* QEMU clock time */
    uint64_t    starttime_ms;
    uint16_t    temperature;

    HostMemoryBackend *pmrdev;

    uint8_t     aer_mask;
    NvmeRequest **aer_reqs;
    QTAILQ_HEAD(, NvmeAsyncEvent) aer_queue;
    int         aer_queued;

    int             zone_file_fd;
    uint32_t        num_zones;
    uint64_t        zone_size;
    uint64_t        zone_capacity;
    uint64_t        zone_array_size;
    uint32_t        zone_size_log2;
    uint32_t        zasl_bs;
    uint8_t         zasl;

    NvmeNamespace   *namespaces;
    NvmeSQueue      **sq;
    NvmeCQueue      **cq;
    NvmeSQueue      admin_sq;
    NvmeCQueue      admin_cq;
    NvmeIdCtrl      id_ctrl;
    NvmeFeatureVal  features;
} NvmeCtrl;

/* calculate the number of LBAs that the namespace can accomodate */
static inline uint64_t nvme_ns_nlbas(NvmeCtrl *n, NvmeNamespace *ns)
{
    return n->ns_size >> nvme_ns_lbads(ns);
}

static inline uint8_t nvme_get_zone_state(NvmeZone *zone)
{
    return zone->d.zs >> 4;
}

static inline void nvme_set_zone_state(NvmeZone *zone, enum NvmeZoneState state)
{
    zone->d.zs = state << 4;
}

static inline uint64_t nvme_zone_rd_boundary(NvmeCtrl *n, NvmeZone *zone)
{
    return zone->d.zslba + n->zone_size;
}

static inline uint64_t nvme_zone_wr_boundary(NvmeZone *zone)
{
    return zone->d.zslba + zone->d.zcap;
}

static inline bool nvme_wp_is_valid(NvmeZone *zone)
{
    uint8_t st = nvme_get_zone_state(zone);

    return st != NVME_ZONE_STATE_FULL &&
           st != NVME_ZONE_STATE_READ_ONLY &&
           st != NVME_ZONE_STATE_OFFLINE;
}

/*
 * Initialize a zone list head.
 */
static inline void nvme_init_zone_list(NvmeZoneList *zl)
{
    zl->head = NVME_ZONE_LIST_NIL;
    zl->tail = NVME_ZONE_LIST_NIL;
    zl->size = 0;
}

/*
 * Initialize the number of entries contained in a zone list.
 */
static inline uint32_t nvme_zone_list_size(NvmeZoneList *zl)
{
    return zl->size;
}

/*
 * Check if the zone is not currently included into any zone list.
 */
static inline bool nvme_zone_not_in_list(NvmeZone *zone)
{
    return (bool)(zone->prev == 0 && zone->next == 0);
}

/*
 * Return the zone at the head of zone list or NULL if the list is empty.
 */
static inline NvmeZone *nvme_peek_zone_head(NvmeNamespace *ns, NvmeZoneList *zl)
{
    if (zl->head == NVME_ZONE_LIST_NIL) {
        return NULL;
    }
    return &ns->zone_array[zl->head];
}

/*
 * Return the next zone in the list.
 */
static inline NvmeZone *nvme_next_zone_in_list(NvmeNamespace *ns, NvmeZone *z,
                                               NvmeZoneList *zl)
{
    assert(!nvme_zone_not_in_list(z));

    if (z->next == NVME_ZONE_LIST_NIL) {
        return NULL;
    }
    return &ns->zone_array[z->next];
}

static inline int nvme_ilog2(uint64_t i)
{
    int log = -1;

    while (i) {
        i >>= 1;
        log++;
    }
    return log;
}

#endif /* HW_NVME_H */
