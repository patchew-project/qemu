/*
 * QEMU OpenChannel 2.0 Controller
 *
 * Copyright (c) 2019 CNEX Labs, Inc.
 *
 * Thank you to the following people for their contributions to the original
 * qemu-nvme (github.com/OpenChannelSSD/qemu-nvme) implementation.
 *
 *   Matias Bjørling <mb@lightnvm.io>
 *   Javier González <javier@javigon.com>
 *   Simon Andreas Frimann Lund <ocssd@safl.dk>
 *   Hans Holmberg <hans@owltronix.com>
 *   Jesper Devantier <contact@pseudonymous.me>
 *   Young Tack Jin <youngtack.jin@circuitblvd.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

#ifndef HW_NVME_OCSSD_H
#define HW_NVME_OCSSD_H

#include "block/ocssd.h"
#include "hw/block/nvme.h"

#define TYPE_OCSSD "ocssd"
#define OCSSD(obj) \
        OBJECT_CHECK(OcssdCtrl, (obj), TYPE_OCSSD)

#define OCSSD_MAX_CHUNK_NOTIFICATIONS 64

#define DEFINE_OCSSD_PROPERTIES(_state, _props) \
    DEFINE_PROP_UINT32("mccap", _state, _props.mccap, UINT32_MAX), \
    DEFINE_PROP_UINT32("ws_min", _state, _props.ws_min, UINT32_MAX), \
    DEFINE_PROP_UINT32("ws_opt", _state, _props.ws_opt, UINT32_MAX), \
    DEFINE_PROP_UINT32("mw_cunits", _state, _props.mw_cunits, UINT32_MAX), \
    DEFINE_PROP_UINT8("wit", _state, _props.wit, UINT8_MAX), \
    DEFINE_PROP_BOOL("early_reset", _state, _props.early_reset, true), \
    DEFINE_PROP_STRING("resetfail", _state, _props.resetfail_fname), \
    DEFINE_PROP_STRING("writefail", _state, _props.writefail_fname), \
    DEFINE_PROP_STRING("chunkinfo", _state, _props.chunkinfo_fname)

typedef struct OcssdParams {
    /* qemu configurable device characteristics */
    uint32_t mccap;
    uint32_t ws_min;
    uint32_t ws_opt;
    uint32_t mw_cunits;
    uint8_t  wit;
    bool     early_reset;

    char *chunkinfo_fname;
    char *resetfail_fname;
    char *writefail_fname;
} OcssdParams;

#define OCSSD_CMD_MAX_LBAS 64

typedef struct OcssdAddrF {
    uint64_t grp_mask;
    uint64_t pu_mask;
    uint64_t chk_mask;
    uint64_t sec_mask;
    uint8_t  grp_offset;
    uint8_t  pu_offset;
    uint8_t  chk_offset;
    uint8_t  sec_offset;
} OcssdAddrF;

typedef struct OcssdChunkAcctDescriptor {
    uint32_t pe_cycles;
} OcssdChunkAcctDescriptor;

typedef struct OcssdChunkAcct {
    uint64_t blk_offset;
    uint64_t size;

    OcssdChunkAcctDescriptor *descr;
} OcssdChunkAcct;

typedef struct OcssdChunkInfo {
    uint64_t blk_offset;
    uint64_t size;

    OcssdChunkDescriptor *descr;
} OcssdChunkInfo;

typedef struct OcssdNamespace {
    NvmeNamespace *ns;

    OcssdIdentity id;
    OcssdAddrF    addrf;

    /* reset and write fail error probabilities indexed by namespace */
    uint8_t *resetfail;
    uint8_t *writefail;

    /* derived values (convenience) */
    uint32_t chks_per_grp;
    uint32_t chks_total;
    uint32_t secs_per_chk;
    uint32_t secs_per_pu;
    uint32_t secs_per_grp;
    uint32_t secs_total;

    /* wear index tracking */
    uint8_t  wear_index_avg;
    uint64_t wear_index_total;

    OcssdChunkInfo info;
    OcssdChunkAcct acct;
} OcssdNamespace;

typedef struct OcssdCtrl {
    NvmeCtrl nvme;

    OcssdFormatHeader hdr;
    OcssdParams       params;
    OcssdNamespace    *namespaces;
    OcssdFeatureVal   features;

    uint64_t notifications_count;
    uint16_t notifications_index;
    uint16_t notifications_max;
    OcssdChunkNotification notifications[OCSSD_MAX_CHUNK_NOTIFICATIONS];
} OcssdCtrl;

static inline void ocssd_ns_optimal_addrf(OcssdAddrF *addrf, OcssdIdLBAF *lbaf)
{
    addrf->sec_offset = 0;
    addrf->chk_offset = lbaf->sec_len;
    addrf->pu_offset  = lbaf->sec_len + lbaf->chk_len;
    addrf->grp_offset = lbaf->sec_len + lbaf->chk_len + lbaf->pu_len;

    addrf->grp_mask = ((1 << lbaf->grp_len) - 1) << addrf->grp_offset;
    addrf->pu_mask  = ((1 << lbaf->pu_len)  - 1) << addrf->pu_offset;
    addrf->chk_mask = ((1 << lbaf->chk_len) - 1) << addrf->chk_offset;
    addrf->sec_mask = ((1 << lbaf->sec_len) - 1) << addrf->sec_offset;
}

#endif /* HW_NVME_OCSSD_H */
