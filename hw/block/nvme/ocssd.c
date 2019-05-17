/*
 * QEMU OpenChannel 2.0 Controller
 *
 * Copyright (c) 2019 CNEX Labs, Inc.
 *
 * Written by Klaus Birkelund Abildgaard Jensen <klaus@birkelund.eu>
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

/*
 * This device emulates an OpenChannel 2.0 compliant NVMe controller.
 *
 * Reference docs: http://lightnvm.io/docs/OCSSD-2_0-20180129.pdf
 *
 *
 * Usage
 * -----
 *
 * The device must have a backing file to store its data. An initialized OCSSD
 * backing file must be created using qemu-img:
 *
 *   qemu-img create -f ocssd -o num_grp=2,num_pu=4,num_chk=60 ocssd.img
 *
 * Besides the geometry options (num_{grp,pu,chk,sec}), qemu-img also supports
 * options related to write characteristics (ws_min, ws_opt and mw_cunits) and
 * lbads and ms sizes. These options can also be overwritten as parameters to
 * the device. Issue
 *
 *   qemu-img create -f ocssd -o help
 *
 * to see the full list of supported options.
 *
 * To add the OCSSD NVMe device, extend the QEMU arguments with something like
 *
 *   -blockdev ocssd,node-name=ocssd01,file.driver=file,file.filename=ocssd.img
 *   -device nvme,drive=ocssd01,serial=deadbeef,id=ocssd01
 *
 * All of the standard nvme device options are supported, except 'ms', which is
 * configured at image creation time.
 *
 * Additional advanced -device options.
 *
 *   mccap=<int>        : Media and Controller Capabilities (MCCAP). OR'ed
 *                        value of the following:
 *                          multiple resets                 0x2
 *                          early resets (non-standard)     0x4
 *   ws_min=<int>       : Mininum write size for device in sectors.
 *   ws_opt=<int>       : Optimal write size for device in sectors.
 *   mw_cunits=<int>    : Cache minimum write size units. If DULBE is enabled,
 *                        an error will be reported if reads are within this
 *                        window.
 *   wit=<int>          : Wear-level index delta threshold.
 *   chunkinfo=<file>   : Overwrite chunk states from file.
 *   resetfail=<file>   : Reset fail injection configuration file.
 *   writefail=<file>   : Write fail injection configuration file.
 *   early_reset        : Allow early resets (reset open chunks).
 *
 * NOTE: mccap, ws_min, ws_opt, mw_cunits and wit defaults to whatever was
 * defined at image creation time.
 *
 * The emulated device maintains a Chunk Info Log Page on the backing block
 * device. When the device is brought up any state will be restored. The
 * restored chunk states may be overwritten using the `chunkinfo` parameter. An
 * example chunk state file follows (note the use of the '*' wildcard to match
 * all groups, punits or chunks).
 *
 *     # "reset" all chunks
 *     ns=1 group=* punit=* chunk=* state=FREE type=SEQUENTIAL pe_cycles=0
 *
 *     # first chunk on all luns has type random
 *     ns=1 group=* punit=* chunk=0 type=RANDOM
 *
 *     # add an open chunk
 *     ns=1 group=0 punit=0 chunk=1 state=OPEN type=SEQ wp=0x800
 *
 *     # add a closed chunk
 *     ns=1 group=0 punit=0 chunk=2 state=CLOSED type=SEQ wp=0x1000
 *
 *     # add an offline chunk
 *     ns=1 group=0 punit=0 chunk=3 state=OFFLINE type=SEQ
 *
 *
 * The `resetfail` and `writefail` QEMU parameters can be used to do
 * probabilistic error injection. The parameters points to text files and they
 * also support the '*' wildcard.
 *
 * Write error injection is done per sector.
 *
 *     # always fail writes for this sector
 *     ns=1 group=0 punit=3 chunk=0 sectr=53 prob=100
 *
 *
 * Reset error injection is done per chunk, so exclude the `sec` parameter.
 *
 *     # always fail resets for this chunk
 *     ns=1 group=0 punit=3 chunk=0 prob=100
 *
 *
 * You probably want to make sure the following options are enabled in the
 * kernel you are going to use.
 *
 *     CONFIG_BLK_DEV_INTEGRITY=y
 *     CONFIG_HOTPLUG_PCI_PCIE=y
 *     CONFIG_HOTPLUG_PCI_ACPI=y
 *
 *
 * It is assumed that when using vector write requests, then the LBAs for
 * different chunks are laid out contiguously and sorted with increasing
 * addresses.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"

#include "trace.h"
#include "ocssd.h"

/* #define OCSSD_CTRL_DEBUG */

#ifdef OCSSD_CTRL_DEBUG
#define _dprintf(fmt, ...) \
    do { \
        fprintf(stderr, "ocssd: " fmt, ## __VA_ARGS__); \
    } while (0)

static inline void _dprint_lba(OcssdCtrl *o, OcssdNamespace *ons, uint64_t lba)
{
    OcssdAddrF *addrf = &ons->addrf;

    uint8_t group, punit;
    uint16_t chunk;
    uint32_t sectr;

    group = _group(addrf, lba);
    punit = _punit(addrf, lba);
    chunk = _chunk(addrf, lba);
    sectr = _sectr(addrf, lba);

    _dprintf("lba 0x%016"PRIx64" group %"PRIu8" punit %"PRIu8" chunk %"PRIu16
        " sectr %"PRIu32"\n", lba, group, punit, chunk, sectr);
}

static inline void _dprint_vector_rw(OcssdCtrl *o, NvmeRequest *req)
{
    OcssdNamespace *ons = _ons(o, req->ns->id);
    _dprintf("vector %s request: cid %d nlb %d\n",
        req->is_write ? "write" : "read", req->cqe.cid, req->nlb);
    _dprintf("lba list:\n");
    for (uint16_t i = 0; i < req->nlb; i++) {
        _dprint_lba(o, ons, _vlba(req, i));
    }
}
#else
static void _dprint_lba(OcssdCtrl *o, OcssdNamespace *ons, uint64_t lba) {}
static void _dprint_vector_rw(OcssdCtrl *o, NvmeRequest *req) {}
#endif


static inline bool _is_write(NvmeRequest *req)
{
    return req->cmd.opcode == OCSSD_CMD_VECT_WRITE || nvme_rw_is_write(req);
}

static inline bool _is_vector_request(NvmeRequest *req)
{
    switch (req->cmd.opcode) {
    case OCSSD_CMD_VECT_RESET:
    case OCSSD_CMD_VECT_WRITE:
    case OCSSD_CMD_VECT_READ:
    case OCSSD_CMD_VECT_COPY:
        return true;
    }

    return false;
}

static inline OcssdNamespace *_ons(OcssdCtrl *o, uint32_t nsid)
{
    if (unlikely(nsid == 0 || nsid > o->nvme.params.num_ns)) {
        return NULL;
    }

    return &o->namespaces[nsid - 1];
}

static inline uint64_t _sectr(OcssdAddrF *addrf, uint64_t lba)
{
    return (lba & addrf->sec_mask) >> addrf->sec_offset;
}

static inline uint64_t _chunk(OcssdAddrF *addrf, uint64_t lba)
{
    return (lba & addrf->chk_mask) >> addrf->chk_offset;
}

static inline uint64_t _punit(OcssdAddrF *addrf, uint64_t lba)
{
    return (lba & addrf->pu_mask) >> addrf->pu_offset;
}

static inline uint64_t _group(OcssdAddrF *addrf, uint64_t lba)
{
    return (lba & addrf->grp_mask) >> addrf->grp_offset;
}

static inline uint64_t _make_lba(OcssdAddrF *addrf, uint16_t group,
    uint16_t punit, uint32_t chunk, uint32_t sectr)
{
    return sectr << addrf->sec_offset
        | chunk << addrf->chk_offset
        | punit << addrf->pu_offset
        | group << addrf->grp_offset;
}

static inline int _valid(OcssdCtrl *o, OcssdNamespace *ons, uint64_t lba)
{
    OcssdIdGeo *geo = &ons->id.geo;
    OcssdAddrF *addrf = &ons->addrf;

    return _sectr(addrf, lba) < geo->clba &&
        _chunk(addrf, lba) < geo->num_chk &&
        _punit(addrf, lba) < geo->num_pu &&
        _group(addrf, lba) < geo->num_grp;
}

static inline uint64_t _sectr_idx(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba)
{
    OcssdAddrF *addrf = &ons->addrf;

    return _sectr(addrf, lba) +
        _chunk(addrf, lba) * ons->secs_per_chk +
        _punit(addrf, lba) * ons->secs_per_pu +
        _group(addrf, lba) * ons->secs_per_grp;
}

static inline uint64_t _chk_idx(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba)
{
    OcssdIdGeo *geo = &ons->id.geo;
    OcssdAddrF *addrf = &ons->addrf;

    return _chunk(addrf, lba) +
        _punit(addrf, lba) * geo->num_chk +
        _group(addrf, lba) * ons->chks_per_grp;
}

static inline uint64_t _vlba(NvmeRequest *req, uint16_t n)
{
    return req->nlb > 1 ? ((uint64_t *) req->slba)[n] : req->slba;
}

static inline void _sglist_to_iov(NvmeCtrl *n, QEMUSGList *qsg,
    QEMUIOVector *iov)
{
    for (int i = 0; i < qsg->nsg; i++) {
        qemu_iovec_add(iov, (void *) qsg->sg[i].base, qsg->sg[i].len);
    }
}

/*
 * _sglist_copy_from copies `len` bytes from the `idx`'th scatter gather entry
 * at `offset` in the `to` QEMUSGList into the `to` QEMUSGList. `idx` and
 * `offset` are updated to mark the position in `to` at which the function
 * reached `len` bytes.
 */
static void _sglist_copy_from(QEMUSGList *to, QEMUSGList *from, int *idx,
    size_t *offset, size_t len)
{
    dma_addr_t curr_addr, curr_len;

    while (len) {
        curr_addr = from->sg[*idx].base + *offset;
        curr_len = from->sg[*idx].len - *offset;

        curr_len = MIN(curr_len, len);

        if (to) {
            qemu_sglist_add(to, curr_addr, curr_len);
        }

        *offset += curr_len;
        len -= curr_len;

        if (*offset == from->sg[*idx].len) {
            *offset = 0;
            (*idx)++;
        }
    }
}

static inline bool _wi_outside_threshold(OcssdNamespace *ons,
    OcssdChunkDescriptor *chk)
{
    return chk->wear_index < ons->wear_index_avg - ons->id.wit ||
        chk->wear_index > ons->wear_index_avg + ons->id.wit;
}

static inline uint8_t _calc_wi(OcssdCtrl *o, uint32_t pe_cycles)
{
    return (pe_cycles * 255) / o->hdr.pe_cycles;
}

static OcssdChunkDescriptor *_get_chunk(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba)
{
    if (!_valid(o, ons, lba)) {
        return NULL;
    }

    return &ons->info.descr[_chk_idx(o, ons, lba)];
}

static OcssdChunkAcctDescriptor *_get_chunk_acct(OcssdCtrl *o,
    OcssdNamespace *ons, uint64_t lba)
{
    if (!_valid(o, ons, lba)) {
        return NULL;
    }

    return &ons->acct.descr[_chk_idx(o, ons, lba)];
}

static void _get_lba_list(OcssdCtrl *o, hwaddr addr, uint64_t **lbal,
    NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    uint32_t len = req->nlb * sizeof(uint64_t);

    if (req->nlb > 1) {
        *lbal = g_malloc_n(req->nlb, sizeof(uint64_t));
        nvme_addr_read(n, addr, *lbal, len);
    } else {
        *lbal = (uint64_t *) addr;
    }
}

static inline OcssdChunkState _str_to_chunk_state(char *s)
{
    if (!strcmp(s, "FREE")) {
        return OCSSD_CHUNK_FREE;
    }

    if (!strcmp(s, "OFFLINE")) {
        return OCSSD_CHUNK_OFFLINE;
    }

    if (!strcmp(s, "OPEN")) {
        return OCSSD_CHUNK_OPEN;
    }

    if (!strcmp(s, "CLOSED")) {
        return OCSSD_CHUNK_CLOSED;
    }

    return -1;
}

static inline OcssdChunkType _str_to_chunk_type(char *s)
{
    if (!strcmp(s, "SEQ") || !strcmp(s, "SEQUENTIAL")) {
        return OCSSD_CHUNK_TYPE_SEQUENTIAL;
    }

    if (!strcmp(s, "RAN") || !strcmp(s, "RANDOM")) {
        return OCSSD_CHUNK_TYPE_RANDOM;
    }

    return -1;
}

static int _parse_string(const char *s, const char *k, char **v)
{
    char *p = strstr(s, k);
    if (!p) {
        return 0;
    }

    return sscanf(p + strlen(k), "%ms", v);
}

static int _parse_uint8(const char *s, const char *k, uint8_t *v)
{
    char *p = strstr(s, k);
    if (!p) {
        return 0;
    }

    return sscanf(p + strlen(k), "0x%"SCNx8, v) ||
        sscanf(p + strlen(k), "%"SCNu8, v);
}

static int _parse_uint16(const char *s, const char *k, uint16_t *v)
{
    char *p = strstr(s, k);
    if (!p) {
        return 0;
    }

    return sscanf(p + strlen(k), "0x%"SCNx16, v) ||
        sscanf(p + strlen(k), "%"SCNu16, v);
}

static int _parse_uint32(const char *s, const char *k, uint32_t *v)
{
    char *p = strstr(s, k);
    if (!p) {
        return 0;
    }

    return sscanf(p + strlen(k), "0x%"SCNx32, v) ||
        sscanf(p + strlen(k), "%"SCNu32, v);
}

static int _parse_uint64(const char *s, const char *k, uint64_t *v)
{
    char *p = strstr(s, k);
    if (!p) {
        return 0;
    }

    return sscanf(p + strlen(k), "0x%"SCNx64, v) ||
        sscanf(p + strlen(k), "%"SCNu64, v);
}

static bool _parse_wildcard(const char *s, const char *k)
{
    char *v;
    bool found = false;
    if (!_parse_string(s, k, &v)) {
        return false;
    }

    if (strcmp(v, "*") == 0) {
        found = true;
    }

    free(v);

    return found;
}

static int _parse_lba_part_uint16(const char *s, const char *k,
    uint16_t *bgn, uint16_t *end, uint16_t end_defval)
{
    if (!bgn || !end) {
        return 1;
    }

    if (_parse_wildcard(s, k)) {
        *bgn = 0;
        *end = end_defval;

        return 1;
    }

    if (!_parse_uint16(s, k, bgn)) {
        return 0;
    }

    *end = *bgn + 1;

    return 1;
}

static int _parse_lba_part_uint32(const char *s, const char *k,
    uint32_t *bgn, uint32_t *end, uint32_t end_defval)
{
    if (!bgn || !end) {
        return 1;
    }

    if (_parse_wildcard(s, k)) {
        *bgn = 0;
        *end = end_defval;

        return 1;
    }

    if (!_parse_uint32(s, k, bgn)) {
        return 0;
    }

    *end = *bgn + 1;

    return 1;
}

static int _parse_lba_parts(OcssdIdGeo *geo, const char *s,
    uint16_t *grp_bgn, uint16_t *grp_end, uint16_t *pu_bgn,
    uint16_t *pu_end, uint32_t *chk_bgn, uint32_t *chk_end,
    uint32_t *sec_bgn, uint32_t *sec_end, Error **errp)
{
    if (!_parse_lba_part_uint16(s, "group=", grp_bgn, grp_end, geo->num_grp)) {
        error_setg(errp, "could not parse group");
        return 0;
    }

    if (!_parse_lba_part_uint16(s, "punit=", pu_bgn, pu_end, geo->num_pu)) {
        error_setg(errp, "could not parse punit");
        return 0;
    }

    if (!_parse_lba_part_uint32(s, "chunk=", chk_bgn, chk_end, geo->num_chk)) {
        error_setg(errp, "could not parse chunk");
        return 0;
    }

    if (!_parse_lba_part_uint32(s, "sectr=", sec_bgn, sec_end, geo->clba)) {
        error_setg(errp, "could not parse sectr");
        return 0;
    }

    return 1;
}

static int _parse_and_update_reset_error_injection(OcssdCtrl *o, const char *s,
    Error **errp)
{
    OcssdNamespace *ons;
    OcssdIdGeo *geo;
    uint16_t group, group_end, punit, punit_end;
    uint32_t nsid, chunk, chunk_end;
    uint64_t idx;
    uint8_t prob;
    Error *local_err = NULL;

    size_t slen = strlen(s);
    if (slen == 1 || (slen > 1 && s[0] == '#')) {
        return 0;
    }

    if (!_parse_uint32(s, "ns=", &nsid)) {
        error_setg(errp, "could not parse namespace id");
        return 1;
    }

    ons = &o->namespaces[nsid - 1];
    geo = &ons->id.geo;

    if (!_parse_lba_parts(geo, s, &group, &group_end, &punit, &punit_end,
        &chunk, &chunk_end, NULL, NULL, &local_err)) {
        error_propagate_prepend(errp, local_err, "could not parse chunk slba");
        return 1;
    }

    if (!_parse_uint8(s, "prob=", &prob)) {
        error_setg(errp, "could not parse probability");
        return 1;
    }

    if (prob > 100) {
        error_setg(errp, "invalid probability");
        return 1;
    }

    for (uint16_t g = group; g < group_end; g++) {
        for (uint16_t p = punit; p < punit_end; p++) {
            for (uint32_t c = chunk; c < chunk_end; c++) {
                idx = _chk_idx(o, ons, _make_lba(&ons->addrf, g, p, c, 0));
                ons->resetfail[idx] = prob;
            }
        }
    }

    return 0;
}

static int _parse_and_update_write_error_injection(OcssdCtrl *o, const char *s,
    Error **errp)
{
    OcssdNamespace *ons;
    OcssdIdGeo *geo;
    uint16_t group, group_end, punit, punit_end;
    uint32_t nsid, chunk, chunk_end, sectr, sectr_end;
    uint64_t sectr_idx;
    uint8_t prob;
    Error *local_err = NULL;

    size_t slen = strlen(s);
    if (slen == 1 || (slen > 1 && s[0] == '#')) {
        return 0;
    }

    if (!_parse_uint32(s, "ns=", &nsid)) {
        error_setg(errp, "could not parse namespace id");
        return 1;
    }

    ons = &o->namespaces[nsid - 1];
    geo = &ons->id.geo;

    if (!_parse_lba_parts(geo, s, &group, &group_end, &punit, &punit_end,
        &chunk, &chunk_end, &sectr, &sectr_end, &local_err)) {
        error_propagate_prepend(errp, local_err, "could not parse lba");
        return 1;
    }

    if (!_parse_uint8(s, "prob=", &prob)) {
        error_setg(errp, "could not parse probability");
        return 1;
    }

    if (prob > 100) {
        error_setg(errp, "invalid probability");
        return 1;
    }

    for (uint16_t g = group; g < group_end; g++) {
        for (uint16_t p = punit; p < punit_end; p++) {
            for (uint32_t c = chunk; c < chunk_end; c++) {
                for (uint32_t s = sectr; s < sectr_end; s++) {
                    sectr_idx = _sectr_idx(o, ons, _make_lba(
                        &ons->addrf, g, p, c, s));
                    ons->writefail[sectr_idx] = prob;
                }
            }
        }
    }

    return 0;
}

static int _parse_and_update_chunk_info(OcssdCtrl *o, const char *s,
    Error **errp)
{
    char *v;
    OcssdChunkDescriptor *chk;
    OcssdChunkAcctDescriptor *chk_acct;
    OcssdNamespace *ons;
    OcssdIdGeo *geo;
    uint16_t group, group_end, punit, punit_end;
    uint32_t nsid, chunk, chunk_end, pe_cycles;
    uint64_t cnlb, wp, slba;
    int state = 0, type = 0;
    bool cnlb_parsed = false, wp_parsed = false, pe_cycles_parsed = false;
    bool state_parsed = false, type_parsed = false;
    Error *local_err = NULL;

    size_t slen = strlen(s);
    if (slen == 1 || (slen > 1 && s[0] == '#')) {
        return 0;
    }

    if (!_parse_uint32(s, "ns=", &nsid)) {
        error_setg(errp, "could not parse namespace id");
        return 1;
    }

    ons = &o->namespaces[nsid - 1];
    geo = &ons->id.geo;

    if (!_parse_lba_parts(geo, s, &group, &group_end, &punit, &punit_end,
        &chunk, &chunk_end, NULL, NULL, &local_err)) {
        error_propagate_prepend(errp, local_err, "could not parse chunk slba");
        return 1;
    }

    if (_parse_string(s, "state=", &v)) {
        state_parsed = true;
        state = _str_to_chunk_state(v);
        free(v);

        if (state < 0) {
            error_setg(errp, "invalid chunk state");
            return 1;
        }
    }

    if (_parse_string(s, "type=", &v)) {
        type_parsed = true;
        type = _str_to_chunk_type(v);
        free(v);

        if (type < 0) {
            error_setg(errp, "invalid chunk type");
            return 1;
        }
    }

    if (_parse_uint64(s, "cnlb=", &cnlb)) {
        cnlb_parsed = true;
    }

    if (_parse_uint64(s, "wp=", &wp)) {
        wp_parsed = true;
    }

    if (_parse_uint32(s, "pe_cycles=", &pe_cycles)) {
        pe_cycles = true;
    }

    if (state_parsed) {
        if (state == OCSSD_CHUNK_OFFLINE && wp_parsed) {
            error_setg(errp, "invalid wp; state is offline");
            return 1;
        }
    }

    if (type_parsed) {
        if (type == OCSSD_CHUNK_TYPE_RANDOM && wp_parsed) {
            error_setg(errp, "invalid wp; type has random write capability");
            return 1;
        }
    }

    for (uint16_t g = group; g < group_end; g++) {
        for (uint16_t p = punit; p < punit_end; p++) {
            for (uint32_t c = chunk; c < chunk_end; c++) {
                slba = _make_lba(&ons->addrf, g, p, c, 0);
                chk = _get_chunk(o, ons, slba);
                chk_acct = _get_chunk_acct(o, ons, slba);
                if (!chk) {
                    error_setg(errp, "invalid lba");
                    return 1;
                }

                if (state_parsed) {
                    /*
                     * Reset the wear index and pe_cycles to zero if the
                     * persisted state is OFFLINE and we move to another state.
                     * If the number of pe_cycles is also changed, it will be
                     * updated subsequently.
                     */
                    if (chk->state == OCSSD_CHUNK_OFFLINE &&
                        state != OCSSD_CHUNK_OFFLINE) {
                        chk->wear_index = 0;
                        chk_acct->pe_cycles = 0;
                    }

                    if (state == OCSSD_CHUNK_OFFLINE) {
                        chk->wp = UINT64_MAX;
                    }

                    if (state == OCSSD_CHUNK_FREE) {
                        chk->wp = 0;
                    }

                    chk->state = state;
                }

                if (type_parsed) {
                    chk->type = type;
                    if (chk->type == OCSSD_CHUNK_TYPE_RANDOM) {
                        chk->wp = UINT64_MAX;
                    }
                }

                if (cnlb_parsed) {
                    chk->cnlb = cnlb;
                    if (chk->cnlb > ons->id.geo.clba) {
                        error_setg(errp, "invalid chunk cnlb");
                        return 1;
                    }

                    if (chk->cnlb != ons->id.geo.clba) {
                        chk->type |= OCSSD_CHUNK_TYPE_SHRINKED;
                    }
                }

                if (wp_parsed) {
                    chk->wp = wp;
                    if (chk->wp > chk->cnlb) {
                        error_setg(errp, "invalid chunk wp");
                        return 1;
                    }
                }

                if (pe_cycles_parsed) {
                    if (pe_cycles > o->hdr.pe_cycles) {
                        error_setg(errp, "invalid number of pe_cycles");
                        return 1;
                    }

                    chk->wear_index = _calc_wi(o, pe_cycles);
                    chk_acct->pe_cycles = pe_cycles;
                }
            }
        }
    }

    return 0;
}

static int ocssd_load_write_error_injection_from_file(OcssdCtrl *o,
    const char *fname, Error **errp)
{
    ssize_t n;
    size_t len = 0;
    int line_num = 0;
    char *line;
    Error *local_err = NULL;
    FILE *fp;

    fp = fopen(fname, "r");
    if (!fp) {
        error_setg_errno(errp, errno,
            "could not open write error injection file (%s): ", fname);
        return 1;
    }

    while ((n = getline(&line, &len, fp)) != -1) {
        line_num++;
        if (_parse_and_update_write_error_injection(o, line, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not parse write error injection (line %d): ", line_num);
            return 1;
        }
    }

    fclose(fp);

    return 0;
}

static int ocssd_load_reset_error_injection_from_file(OcssdCtrl *o,
    const char *fname, Error **errp)
{
    ssize_t n;
    size_t len = 0;
    int line_num = 0;
    char *line;
    Error *local_err = NULL;
    FILE *fp;

    fp = fopen(fname, "r");
    if (!fp) {
        error_setg_errno(errp, errno,
            "could not open reset error injection file (%s): ", fname);
        return 1;
    }

    while ((n = getline(&line, &len, fp)) != -1) {
        line_num++;
        if (_parse_and_update_reset_error_injection(o, line, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not parse reset error injection (line %d): ", line_num);
            return 1;
        }
    }

    fclose(fp);

    return 0;
}

static int ocssd_load_chunk_info_from_file(OcssdCtrl *o, const char *fname,
    Error **errp)
{
    ssize_t n;
    size_t len = 0;
    int line_num = 0;
    char *line;
    Error *local_err = NULL;
    FILE *fp;

    fp = fopen(fname, "r");
    if (!fp) {
        error_setg_errno(errp, errno, "could not open chunk info file");
        return 1;
    }

    while ((n = getline(&line, &len, fp)) != -1) {
        line_num++;
        if (_parse_and_update_chunk_info(o, line, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not parse chunk info (line %d): ", line_num);
            return 1;
        }
    }

    fclose(fp);

    return 0;
}

static void ocssd_ns_commit_chunk_acct(OcssdCtrl *o, OcssdNamespace *ons,
    NvmeRequest *req, OcssdChunkDescriptor *chk,
    OcssdChunkAcctDescriptor *chk_acct)
{
    NvmeCtrl *n = &o->nvme;
    NvmeBlockBackendRequest *blk_req = nvme_blk_req_get(n, req, NULL);

    blk_req->blk_offset = ons->acct.blk_offset;

    qemu_iovec_init(&blk_req->iov, 1);
    if (chk) {
        qemu_iovec_add(&blk_req->iov, chk_acct,
            sizeof(OcssdChunkAcctDescriptor));
        blk_req->blk_offset += _chk_idx(o, ons, chk->slba) *
            sizeof(OcssdChunkAcctDescriptor);
    } else {
        qemu_iovec_add(&blk_req->iov, ons->acct.descr, ons->acct.size);
    }

    QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

    block_acct_start(blk_get_stats(n->conf.blk), &blk_req->acct,
        blk_req->iov.size, BLOCK_ACCT_WRITE);

    blk_req->aiocb = blk_aio_pwritev(n->conf.blk, blk_req->blk_offset,
        &blk_req->iov, 0, nvme_rw_cb, blk_req);
}

static void ocssd_ns_commit_chunk_state(OcssdCtrl *o, OcssdNamespace *ons,
    NvmeRequest *req, OcssdChunkDescriptor *chk)
{
    NvmeCtrl *n = &o->nvme;
    NvmeBlockBackendRequest *blk_req = nvme_blk_req_get(n, req, NULL);

    blk_req->blk_offset = ons->info.blk_offset;

    qemu_iovec_init(&blk_req->iov, 1);
    if (chk) {
        qemu_iovec_add(&blk_req->iov, chk, sizeof(OcssdChunkDescriptor));
        blk_req->blk_offset += _chk_idx(o, ons, chk->slba) *
            sizeof(OcssdChunkDescriptor);
    } else {
        qemu_iovec_add(&blk_req->iov, ons->info.descr, ons->info.size);
    }

    QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

    block_acct_start(blk_get_stats(n->conf.blk), &blk_req->acct,
        blk_req->iov.size, BLOCK_ACCT_WRITE);

    blk_req->aiocb = blk_aio_pwritev(n->conf.blk, blk_req->blk_offset,
        &blk_req->iov, 0, nvme_rw_cb, blk_req);
}

static int ocssd_ns_load_chunk_acct(OcssdCtrl *o, OcssdNamespace *ons)
{
    BlockBackend *blk = o->nvme.conf.blk;
    return blk_pread(blk, ons->acct.blk_offset, ons->acct.descr,
        ons->acct.size);
}

static int ocssd_ns_load_chunk_info(OcssdCtrl *o, OcssdNamespace *ons)
{
    BlockBackend *blk = o->nvme.conf.blk;
    return blk_pread(blk, ons->info.blk_offset, ons->info.descr,
        ons->info.size);
}

static uint16_t ocssd_do_get_chunk_info(OcssdCtrl *o, NvmeCmd *cmd,
    uint32_t buf_len, uint64_t off, NvmeRequest *req)
{
    uint8_t *log_page;
    uint32_t log_len, trans_len;

    OcssdNamespace *ons = _ons(o, le32_to_cpu(cmd->nsid));
    if (!ons) {
        trace_ocssd_err(req->cqe.cid, "chunk info requires nsid",
            NVME_INVALID_FIELD | NVME_DNR);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    log_len = ons->chks_total * sizeof(OcssdChunkDescriptor);

    if (off > log_len) {
        trace_ocssd_err(req->cqe.cid, "invalid log page offset",
            NVME_INVALID_FIELD | NVME_DNR);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_len - off, buf_len);
    log_page = (uint8_t *) ons->info.descr + off;

    return nvme_dma_read(&o->nvme, log_page, trans_len, cmd, req);
}

static uint16_t ocssd_do_get_chunk_notification(OcssdCtrl *o, NvmeCmd *cmd,
    uint32_t buf_len, uint64_t off, uint8_t rae, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;

    uint8_t *log_page;
    uint32_t log_len, trans_len;

    log_len = OCSSD_MAX_CHUNK_NOTIFICATIONS * sizeof(OcssdChunkNotification);

    if (off > log_len) {
        trace_ocssd_err(req->cqe.cid, "invalid log page offset",
            NVME_INVALID_FIELD | NVME_DNR);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_len - off, buf_len);
    log_page = (uint8_t *) &o->notifications[off];

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_VENDOR_SPECIFIC);
    }

    return nvme_dma_read(&o->nvme, log_page, trans_len, cmd, req);
}

static void ocssd_add_chunk_notification(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba, uint16_t state, uint8_t mask, uint16_t nlb)
{
    NvmeNamespace *ns = ons->ns;
    OcssdChunkNotification *notice;

    notice = &o->notifications[o->notifications_index];
    notice->nc = cpu_to_le64(++(o->notifications_count));
    notice->lba = cpu_to_le64(lba);
    notice->nsid = cpu_to_le32(ns->id);
    notice->state = cpu_to_le16(state);
    notice->mask = mask;
    notice->nlb = cpu_to_le16(nlb);

    o->notifications_index = (o->notifications_index + 1) %
        OCSSD_MAX_CHUNK_NOTIFICATIONS;
}

static uint16_t ocssd_rw_check_chunk_read(OcssdCtrl *o, NvmeCmd *cmd,
    NvmeRequest *req, uint64_t lba)
{
    NvmeNamespace *ns = req->ns;
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];
    OcssdAddrF *addrf = &ons->addrf;
    OcssdIdWrt *wrt = &ons->id.wrt;

    OcssdChunkDescriptor *chk;
    uint64_t sectr, mw_cunits, wp;
    uint8_t state;

    chk = _get_chunk(o, ons, lba);
    if (!chk) {
        trace_ocssd_err_invalid_chunk(req->cqe.cid,
            lba & ~ons->addrf.sec_mask);
        return NVME_DULB;
    }

    sectr = _sectr(addrf, lba);
    mw_cunits = wrt->mw_cunits;
    wp = chk->wp;
    state = chk->state;

    if (chk->type == OCSSD_CHUNK_TYPE_RANDOM) {
        /*
         * For OCSSD_CHUNK_TYPE_RANDOM it is sufficient to ensure that the
         * chunk is OPEN and that we are reading a valid address.
         */
        if (state != OCSSD_CHUNK_OPEN || sectr >= chk->cnlb) {
            trace_ocssd_err_invalid_chunk_state(req->cqe.cid,
                lba & ~(ons->addrf.sec_mask), chk->state);
            return NVME_DULB;
        }

        return NVME_SUCCESS;
    }

    if (state == OCSSD_CHUNK_CLOSED && sectr < wp) {
        return NVME_SUCCESS;
    }

    if (state == OCSSD_CHUNK_OPEN) {
        if (wp < mw_cunits) {
            return NVME_DULB;
        }

        if (sectr < (wp - mw_cunits)) {
            return NVME_SUCCESS;
        }
    }

    return NVME_DULB;
}

static uint16_t ocssd_rw_check_chunk_write(OcssdCtrl *o, NvmeCmd *cmd,
    uint64_t lba, uint32_t ws, NvmeRequest *req)
{
    OcssdChunkDescriptor *chk;
    NvmeNamespace *ns = req->ns;
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];

    OcssdIdWrt *wrt = &ons->id.wrt;

    chk = _get_chunk(o, ons, lba);
    if (!chk) {
        trace_ocssd_err_invalid_chunk(req->cqe.cid,
            lba & ~ons->addrf.sec_mask);
        return NVME_WRITE_FAULT | NVME_DNR;
    }

    uint32_t start_sectr = lba & ons->addrf.sec_mask;
    uint32_t end_sectr = start_sectr + ws;

    /* check if we are at all allowed to write to the chunk */
    if (chk->state == OCSSD_CHUNK_OFFLINE ||
        chk->state == OCSSD_CHUNK_CLOSED) {
        trace_ocssd_err_invalid_chunk_state(req->cqe.cid,
            lba & ~(ons->addrf.sec_mask), chk->state);
        return NVME_WRITE_FAULT | NVME_DNR;
    }

    if (end_sectr > chk->cnlb) {
        trace_ocssd_err_out_of_bounds(req->cqe.cid, end_sectr, chk->cnlb);
        return NVME_WRITE_FAULT | NVME_DNR;
    }


    if (chk->type == OCSSD_CHUNK_TYPE_RANDOM) {
        return NVME_SUCCESS;
    }

    if (ws < wrt->ws_min || (ws % wrt->ws_min) != 0) {
        trace_ocssd_err_write_constraints(req->cqe.cid, ws, wrt->ws_min);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /* check that the write begins at the current wp */
    if (start_sectr != chk->wp) {
        trace_ocssd_err_out_of_order(req->cqe.cid, start_sectr, chk->wp);
        return OCSSD_OUT_OF_ORDER_WRITE | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static uint16_t ocssd_rw_check_vector_read_req(OcssdCtrl *o, NvmeCmd *cmd,
    NvmeRequest *req, uint64_t *dulbe)
{
    uint16_t status;

    assert(dulbe);

    for (int i = 0; i < req->nlb; i++) {
        status = ocssd_rw_check_chunk_read(o, cmd, req, _vlba(req, i));

        if (status) {
            if (nvme_is_error(status, NVME_DULB)) {
                *dulbe |= (1 << i);
                continue;
            }

            return status;
        }
    }

    return NVME_SUCCESS;
}

static uint16_t ocssd_rw_check_vector_write_req(OcssdCtrl *o, NvmeCmd *cmd,
    NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];
    OcssdAddrF *addrf = &ons->addrf;

    uint64_t prev_lba = _vlba(req, 0);
    uint64_t prev_chk_idx = _chk_idx(o, ons, prev_lba);
    uint32_t sectr = _sectr(addrf, prev_lba);
    uint16_t ws = 1, status;

    for (uint16_t i = 1; i < req->nlb; i++) {
        uint64_t lba = _vlba(req, i);
        uint64_t chk_idx = _chk_idx(o, ons, lba);

        /*
         * It is assumed that LBAs for different chunks are laid out
         * contiguously and sorted with increasing addresses.
         */
        if (prev_chk_idx != chk_idx) {
            status = ocssd_rw_check_chunk_write(o, cmd, prev_lba, ws, req);
            if (status) {
                req->cqe.res64 = cpu_to_le64((1 << req->nlb) - 1);
                return status;
            }

            prev_lba = lba;
            prev_chk_idx = chk_idx;
            sectr = _sectr(addrf, prev_lba);
            ws = 1;

            continue;
        }

        if (++sectr != _sectr(addrf, lba)) {
            return OCSSD_OUT_OF_ORDER_WRITE | NVME_DNR;
        }

        ws++;
    }

    return ocssd_rw_check_chunk_write(o, cmd, prev_lba, ws, req);
}

static uint16_t ocssd_rw_check_scalar_req(OcssdCtrl *o, NvmeCmd *cmd,
    NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    uint16_t status;

    status = nvme_rw_check_req(n, cmd, req);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "nvme_rw_check_req", status);
        return status;
    }

    if (req->is_write) {
        return ocssd_rw_check_chunk_write(o, cmd, req->slba, req->nlb, req);
    }

    for (uint16_t i = 0; i < req->nlb; i++) {
        status = ocssd_rw_check_chunk_read(o, cmd, req, req->slba + i);
        if (nvme_is_error(status, NVME_DULB)) {
            if (NVME_ERR_REC_DULBE(n->features.err_rec)) {
                return NVME_DULB | NVME_DNR;
            }

            break;
        }

        return status;
    }

    return NVME_SUCCESS;
}

static uint16_t ocssd_rw_check_vector_req(OcssdCtrl *o, NvmeCmd *cmd,
    NvmeRequest *req, uint64_t *dulbe)
{
    NvmeCtrl *n = &o->nvme;
    uint16_t status;

    status = nvme_rw_check_req(n, cmd, req);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "nvme_rw_check_req", status);
        return status;
    }

    if (req->is_write) {
        return ocssd_rw_check_vector_write_req(o, cmd, req);
    }

    return ocssd_rw_check_vector_read_req(o, cmd, req, dulbe);
}

static uint16_t ocssd_blk_setup_scalar(NvmeCtrl *n, NvmeNamespace *ns,
    QEMUSGList *qsg, uint64_t blk_offset, uint32_t unit_len, NvmeRequest *req)
{
    OcssdCtrl *o = OCSSD(n);
    OcssdNamespace *ons = _ons(o, req->ns->id);

    NvmeBlockBackendRequest *blk_req = nvme_blk_req_get(n, req, qsg);
    if (!blk_req) {
        NVME_GUEST_ERR(nvme_err_internal_dev_error, "nvme_blk_req_get: %s",
            "could not allocate memory");
        return NVME_INTERNAL_DEV_ERROR;
    }

    blk_req->slba = req->slba;
    blk_req->nlb = req->nlb;
    blk_req->blk_offset = blk_offset + _sectr_idx(o, ons, req->slba) *
        unit_len;

    QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

    return NVME_SUCCESS;
}

static uint16_t ocssd_blk_setup_vector(NvmeCtrl *n, NvmeNamespace *ns,
    QEMUSGList *qsg, uint64_t blk_offset, uint32_t unit_len, NvmeRequest *req)
{
    OcssdCtrl *o = OCSSD(n);
    OcssdNamespace *ons = _ons(o, req->ns->id);

    size_t curr_byte = 0;
    uint64_t lba, chk_idx, prev_chk_idx;
    int curr_sge = 0;

    NvmeBlockBackendRequest *blk_req = nvme_blk_req_get(n, req, NULL);
    pci_dma_sglist_init(&blk_req->qsg, &n->parent_obj, 1);

    /*
     * Similar to ocssd_rw_check_vector_write_req, it is assumed that LBAs for
     * different chunks are laid out contiguously and sorted with increasing
     * addresses. Thus, split request into multiple NvmeBlockBackendRequest for
     * each chunk involved unconditionally, even if the last sector of chunk N
     * has address K and the first address of chunk N+1 has address K+1 and
     * would be contiguous on the block backend. The invariant that a single
     * NvmeBlockBackendRequest corresponds to at most one chunk is used in
     * e.g. write error injection.
     */

    lba = _vlba(req, 0);
    prev_chk_idx = _chk_idx(o, ons, lba);

    blk_req->blk_offset = blk_offset + _sectr_idx(o, ons, lba) * unit_len;
    blk_req->slba = lba;
    blk_req->nlb = 1;

    QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

    for (uint16_t i = 1; i < req->nlb; i++) {
        lba = _vlba(req, i);
        chk_idx = _chk_idx(o, ons, lba);

        if (prev_chk_idx != chk_idx) {
            _sglist_copy_from(&blk_req->qsg, qsg, &curr_sge, &curr_byte,
                blk_req->nlb * unit_len);

            blk_req = nvme_blk_req_get(n, req, NULL);
            if (!blk_req) {
                NVME_GUEST_ERR(nvme_err_internal_dev_error,
                    "nvme_blk_req_get: %s", "could not allocate memory");
                return NVME_INTERNAL_DEV_ERROR;
            }

            pci_dma_sglist_init(&blk_req->qsg, &n->parent_obj, 1);

            blk_req->blk_offset = blk_offset + _sectr_idx(o, ons, lba) *
                unit_len;
            blk_req->slba = lba;

            QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

            prev_chk_idx = chk_idx;
        }

        blk_req->nlb++;
    }

    _sglist_copy_from(&blk_req->qsg, qsg, &curr_sge, &curr_byte,
        blk_req->nlb * unit_len);

    return NVME_SUCCESS;
}

static uint16_t ocssd_do_chunk_reset(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba, hwaddr mptr, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    OcssdChunkDescriptor *chk;
    OcssdChunkAcctDescriptor *chk_acct;
    uint8_t p;

    chk = _get_chunk(o, ons, lba);
    if (!chk) {
        trace_ocssd_err_invalid_chunk(req->cqe.cid,
            lba & ~ons->addrf.sec_mask);
        return OCSSD_INVALID_RESET | NVME_DNR;
    }

    if (chk->state & OCSSD_CHUNK_RESETABLE) {
        switch (chk->state) {
        case OCSSD_CHUNK_FREE:
            trace_ocssd_notice_double_reset(req->cqe.cid, lba);

            if (!(ons->id.mccap & OCSSD_IDENTITY_MCCAP_MULTIPLE_RESETS)) {
                return OCSSD_INVALID_RESET | NVME_DNR;
            }

            break;

        case OCSSD_CHUNK_OPEN:
            trace_ocssd_notice_early_reset(req->cqe.cid, lba, chk->wp);
            if (!(ons->id.mccap & OCSSD_IDENTITY_MCCAP_EARLY_RESET)) {
                return OCSSD_INVALID_RESET | NVME_DNR;
            }

            break;
        }

        if (ons->resetfail) {
            p = ons->resetfail[_chk_idx(o, ons, lba)];

            if (p == 100 || (rand() % 100) < p) {
                chk->state = OCSSD_CHUNK_OFFLINE;
                chk->wp = UINT64_MAX;
                trace_ocssd_inject_reset_err(req->cqe.cid, p, lba);
                return OCSSD_INVALID_RESET | NVME_DNR;
            }
        }

        chk->state = OCSSD_CHUNK_FREE;

        if (chk->type == OCSSD_CHUNK_TYPE_SEQUENTIAL) {
            chk->wp = 0;

            chk_acct = _get_chunk_acct(o, ons, lba);

            if (chk_acct->pe_cycles < o->hdr.pe_cycles) {
                chk_acct->pe_cycles++;

                ons->wear_index_total++;
                ons->wear_index_avg = ons->wear_index_total / ons->chks_total;

                chk->wear_index = _calc_wi(o, chk_acct->pe_cycles);

                if (_wi_outside_threshold(ons, chk)) {
                    ocssd_add_chunk_notification(o, ons, chk->slba,
                        OCSSD_CHUNK_NOTIFICATION_STATE_WLI,
                        OCSSD_CHUNK_NOTIFICATION_MASK_CHUNK, 0);

                    nvme_enqueue_event(n, NVME_AER_TYPE_VENDOR_SPECIFIC, 0x0,
                        OCSSD_CHUNK_NOTIFICATION);
                }
            }

            if (chk->wear_index == 255) {
                chk->state = OCSSD_CHUNK_OFFLINE;
            }

            ocssd_ns_commit_chunk_acct(o, ons, req, chk, chk_acct);
        }

        if (mptr) {
            nvme_addr_write(n, mptr, chk, sizeof(*chk));
        }

        ocssd_ns_commit_chunk_state(o, ons, req, chk);

        return NVME_SUCCESS;
    }

    trace_ocssd_err_offline_chunk(req->cqe.cid, lba);

    return OCSSD_OFFLINE_CHUNK | NVME_DNR;
}

static uint16_t ocssd_do_advance_wp(OcssdCtrl *o, OcssdNamespace *ons,
    uint64_t lba, uint16_t nlb, NvmeRequest *req)
{
    OcssdChunkDescriptor *chk;

    trace_ocssd_advance_wp(req->cqe.cid, lba, nlb);
    _dprint_lba(o, ons, lba);

    chk = _get_chunk(o, ons, lba);
    if (!chk) {
        NVME_GUEST_ERR(ocssd_err_invalid_chunk,
            "invalid chunk; cid %d slba 0x%lx", req->cqe.cid,
            lba & ~ons->addrf.sec_mask);
        return NVME_INTERNAL_DEV_ERROR | NVME_DNR;
    }

    if (chk->state == OCSSD_CHUNK_FREE) {
        chk->state = OCSSD_CHUNK_OPEN;
    }

    if (chk->type == OCSSD_CHUNK_TYPE_RANDOM) {
        goto commit;
    }

    if (chk->state != OCSSD_CHUNK_OPEN) {
        NVME_GUEST_ERR(ocssd_err_invalid_chunk_state,
            "invalid chunk state; cid %d slba 0x%lx state 0x%x",
            req->cqe.cid, lba, chk->state);
        return NVME_INTERNAL_DEV_ERROR | NVME_DNR;
    }

    chk->wp += nlb;
    if (chk->wp == chk->cnlb) {
        chk->state = OCSSD_CHUNK_CLOSED;
    }

commit:
    ocssd_ns_commit_chunk_state(o, ons, req, chk);

    return NVME_SUCCESS;
}

static void ocssd_dsm_cb(void *opaque, int ret)
{
    NvmeBlockBackendRequest *blk_req = opaque;
    NvmeRequest *req = blk_req->req;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];
    NvmeNamespace *ns = req->ns;

    OcssdCtrl *o = OCSSD(n);
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];

    uint16_t status;

    QTAILQ_REMOVE(&req->blk_req_tailq, blk_req, tailq_entry);

    if (!ret) {
        status = ocssd_do_chunk_reset(o, ons, blk_req->slba, 0x0, req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_do_chunk_reset", status);
            req->status = status;
            goto out;
        }
    } else {
        NVME_GUEST_ERR(nvme_err_internal_dev_error, "block request failed: %s",
            strerror(-ret));
        req->status = NVME_INTERNAL_DEV_ERROR;
    }

out:
    if (QTAILQ_EMPTY(&req->blk_req_tailq)) {
        nvme_enqueue_req_completion(cq, req);
    }

    nvme_blk_req_put(n, blk_req);
}


static uint16_t ocssd_dsm(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    NvmeNamespace *ns = req->ns;
    NvmeDsmCmd *dsm = (NvmeDsmCmd *) cmd;

    OcssdNamespace *ons = &o->namespaces[ns->id - 1];

    uint16_t status;

    if (dsm->attributes & NVME_DSMGMT_AD) {
        NvmeBlockBackendRequest *blk_req;
        OcssdChunkDescriptor *chk;

        uint16_t nr = (dsm->nr & 0xff) + 1;
        uint8_t lbads = nvme_ns_lbads(ns);

        NvmeDsmRange range[nr];

        status = nvme_dma_write(n, (uint8_t *) range, sizeof(range), cmd, req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "nvme_dma_write", status);
            return status;
        }

        for (int i = 0; i < nr; i++) {
            chk = _get_chunk(o, ons, range[i].slba);

            if (!chk) {
                trace_ocssd_err_invalid_chunk(req->cqe.cid,
                    range[i].slba & ~ons->addrf.sec_mask);
                return OCSSD_INVALID_RESET | NVME_DNR;
            }

            if (range[i].nlb != chk->cnlb) {
                trace_ocssd_err(req->cqe.cid, "invalid reset size",
                    NVME_LBA_RANGE);
                return NVME_LBA_RANGE | NVME_DNR;
            }

            blk_req = nvme_blk_req_get(n, req, NULL);
            if (!blk_req) {
                NVME_GUEST_ERR(nvme_err_internal_dev_error,
                    "nvme_blk_req_get: %s", "could not allocate memory");
                return NVME_INTERNAL_DEV_ERROR;
            }

            blk_req->slba = range[i].slba;

            QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

            blk_req->aiocb = blk_aio_pdiscard(n->conf.blk,
                ns->blk_offset + _sectr_idx(o, ons, range[i].slba),
                range[i].nlb << lbads,
                ocssd_dsm_cb, blk_req);
        }

        return NVME_NO_COMPLETE;
    }

    return NVME_SUCCESS;
}

static void ocssd_reset_cb(void *opaque, int ret)
{
    NvmeBlockBackendRequest *blk_req = opaque;
    NvmeRequest *req = blk_req->req;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];
    NvmeNamespace *ns = req->ns;

    OcssdCtrl *o = OCSSD(n);
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];

    hwaddr mptr;
    uint16_t status;

    QTAILQ_REMOVE(&req->blk_req_tailq, blk_req, tailq_entry);

    if (!ret) {
        /*
         * blk_req->nlb has been hijacked to store the index that this entry
         * held in the LBA list, so use that to calculate the MPTR offset.
         */
        mptr = req->mptr ? req->mptr +
            blk_req->nlb * sizeof(OcssdChunkDescriptor) : 0x0;
        status = ocssd_do_chunk_reset(o, ons, blk_req->slba, mptr, req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_do_chunk_reset", status);
            req->status = status;
            goto out;
        }
    } else {
        NVME_GUEST_ERR(nvme_err_internal_dev_error, "block request failed: %s",
            strerror(-ret));
        req->status = NVME_INTERNAL_DEV_ERROR;
    }

out:
    if (QTAILQ_EMPTY(&req->blk_req_tailq)) {
        nvme_enqueue_req_completion(cq, req);
    }

    nvme_blk_req_put(n, blk_req);
}

static uint16_t ocssd_reset(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    OcssdRwCmd *rst = (OcssdRwCmd *) cmd;
    OcssdNamespace *ons = _ons(o, req->ns->id);
    hwaddr lbal_addr = le64_to_cpu(rst->lbal);
    uint16_t nlb = le16_to_cpu(rst->nlb) + 1;
    uint8_t lbads = nvme_ns_lbads(req->ns);
    uint16_t status = NVME_NO_COMPLETE;
    uint64_t *lbal;

    trace_ocssd_reset(req->cqe.cid, nlb);

    req->nlb = nlb;
    req->mptr = le64_to_cpu(cmd->mptr);

    _get_lba_list(o, lbal_addr, &lbal, req);
    req->slba = (uint64_t) lbal;

    /*
     * The resetting of multiple chunks is done asynchronously, so hijack
     * blk_req->nlb to store the LBAL index which is required for the callback
     * to know the index in MPTR at which to store the updated chunk
     * descriptor.
     */
    for (int i = 0; i < nlb; i++) {
        OcssdChunkDescriptor *chk;
        NvmeBlockBackendRequest *blk_req = nvme_blk_req_get(n, req, NULL);
        if (!blk_req) {
            NVME_GUEST_ERR(nvme_err_internal_dev_error, "nvme_blk_req_get: %s",
                "could not allocate memory");
            status = NVME_INTERNAL_DEV_ERROR | NVME_DNR;
            goto out;
        }

        blk_req->slba = _vlba(req, i);
        blk_req->nlb = i;

        chk = _get_chunk(o, ons, blk_req->slba);
        if (!chk) {
            trace_ocssd_err_invalid_chunk(req->cqe.cid,
                blk_req->slba & ~ons->addrf.sec_mask);
            status = OCSSD_INVALID_RESET | NVME_DNR;
            goto out;
        }

        QTAILQ_INSERT_TAIL(&req->blk_req_tailq, blk_req, tailq_entry);

        blk_req->aiocb = blk_aio_pdiscard(n->conf.blk,
            req->ns->blk_offset + (_sectr_idx(o, ons, blk_req->slba) << lbads),
            chk->cnlb << lbads, ocssd_reset_cb, blk_req);
    }

out:
    if (req->nlb > 1) {
        g_free((uint64_t *) req->slba);
    }

    return status;
}

static uint16_t ocssd_maybe_write_error_inject(OcssdCtrl *o,
    NvmeBlockBackendRequest *blk_req)
{
    NvmeRequest *req = blk_req->req;
    NvmeNamespace *ns = req->ns;
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];
    OcssdChunkDescriptor *chk;
    uint8_t p;
    uint64_t cidx, slba = blk_req->slba;

    if (!ons->writefail || !req->is_write) {
        return NVME_SUCCESS;
    }

    for (uint16_t i = 0; i < blk_req->nlb; i++) {
        p = ons->writefail[_sectr_idx(o, ons, slba + i)];

        if (p && (p == 100 || (rand() % 100) < p)) {
            trace_ocssd_inject_write_err(req->cqe.cid, p, slba + i);

            chk = _get_chunk(o, ons, slba);
            if (!chk) {
                NVME_GUEST_ERR(ocssd_err_invalid_chunk,
                    "invalid chunk; cid %d addr 0x%lx", req->cqe.cid,
                    slba & ~ons->addrf.sec_mask);
                return NVME_INTERNAL_DEV_ERROR | NVME_DNR;
            }

            cidx = _chk_idx(o, ons, slba + i);
            chk->state = OCSSD_CHUNK_CLOSED;

            ocssd_ns_commit_chunk_state(o, ons, req, chk);
            ons->resetfail[cidx] = 100;

            if (_is_vector_request(req)) {
                for (uint16_t j = 0; j < req->nlb; j++) {
                    if (cidx == _chk_idx(o, ons, slba)) {
                        bitmap_set(&req->cqe.res64, j, 1);
                    }
                }
            }

            return OCSSD_CHUNK_EARLY_CLOSE | NVME_DNR;
        }
    }

    return NVME_SUCCESS;
}

static void ocssd_rwc_aio_complete(OcssdCtrl *o,
    NvmeBlockBackendRequest *blk_req, int ret)
{
    NvmeCtrl *n = &o->nvme;
    NvmeRequest *req = blk_req->req;
    NvmeNamespace *ns = req->ns;
    OcssdNamespace *ons = &o->namespaces[ns->id - 1];
    uint16_t status;

    if (!ret) {
        block_acct_done(blk_get_stats(n->conf.blk), &blk_req->acct);

        if (req->is_write && blk_req->blk_offset >= ns->blk_offset &&
            blk_req->blk_offset < ns->blk_offset_md) {

            /*
             * We know that each NvmeBlockBackendRequest corresponds to a write
             * to at most one chunk (one contiguous write). This way, we can
             * allow a write to a single chunk to fail (while leaving the write
             * pointer intact), but allow writes to other chunks to proceed.
             */
            status = ocssd_maybe_write_error_inject(o, blk_req);
            if (!status) {
                status = ocssd_do_advance_wp(o, ons, blk_req->slba,
                    blk_req->nlb, req);
            }

            /*
             * An internal device error trumps all other errors, but there is
             * no way of triaging other errors, so only set an error if one has
             * not already been set.
             */
            if (status) {
                if (nvme_is_error(status, NVME_INTERNAL_DEV_ERROR)) {
                    NVME_GUEST_ERR(nvme_err_internal_dev_error, "%s",
                        "internal device error");
                    req->status = status;
                }

                if (!req->status) {
                    req->status = status;
                }
            }
        }
    } else {
        block_acct_failed(blk_get_stats(n->conf.blk), &blk_req->acct);
        NVME_GUEST_ERR(nvme_err_internal_dev_error, "block request failed: %s",
            strerror(-ret));
        req->status = NVME_INTERNAL_DEV_ERROR | NVME_DNR;
    }
}

static void ocssd_copy_out_cb(void *opaque, int ret)
{
    NvmeBlockBackendRequest *blk_req = opaque;
    NvmeRequest *req = blk_req->req;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];

    OcssdCtrl *o = OCSSD(n);
    hwaddr addr;

    trace_ocssd_copy_out_cb(req->cqe.cid, req->ns->id);

    QTAILQ_REMOVE(&req->blk_req_tailq, blk_req, tailq_entry);

    ocssd_rwc_aio_complete(o, blk_req, ret);
    nvme_blk_req_put(n, blk_req);

    if (QTAILQ_EMPTY(&req->blk_req_tailq)) {
        /* free the bounce buffers */
        addr = req->cmd.cdw12;
        addr = (addr << 32) | req->cmd.cdw13;
        g_free((void *) addr);
        g_free((void *) req->cmd.mptr);

        nvme_enqueue_req_completion(cq, req);
    }
}

static void ocssd_copy_in_cb(void *opaque, int ret)
{
    NvmeBlockBackendRequest *blk_req = opaque;
    NvmeRequest *req = blk_req->req;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];
    NvmeNamespace *ns = req->ns;

    OcssdCtrl *o = OCSSD(n);
    OcssdCopyCmd *cpy = (OcssdCopyCmd *) &req->cmd;

    hwaddr addr = le64_to_cpu(cpy->dlbal);
    uint64_t *dlbal;
    size_t unit_len = nvme_ns_lbads_bytes(ns);
    size_t unit_len_meta = nvme_ns_ms(ns);
    uint16_t status;

    QEMUSGList qsg;

    QTAILQ_REMOVE(&req->blk_req_tailq, blk_req, tailq_entry);

    trace_ocssd_copy_in_cb(req->cqe.cid, req->ns->id);

    if (!ret) {
        block_acct_done(blk_get_stats(n->conf.blk), &blk_req->acct);
    } else {
        block_acct_failed(blk_get_stats(n->conf.blk), &blk_req->acct);
        NVME_GUEST_ERR(nvme_err_internal_dev_error, "block request failed: %s",
            strerror(-ret));
        req->status = NVME_INTERNAL_DEV_ERROR | NVME_DNR;
    }

    nvme_blk_req_put(n, blk_req);

    if (QTAILQ_EMPTY(&req->blk_req_tailq)) {
        _get_lba_list(o, addr, &dlbal, req);
        req->slba = (uint64_t) dlbal;

        /* second phase of copy is a write */
        req->is_write = true;

        status = ocssd_rw_check_vector_req(o, &req->cmd, req, NULL);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_rw_check_vector_req",
                status);
            goto out;
        }

        addr = req->cmd.cdw12;
        addr = (addr << 32) | req->cmd.cdw13;

        pci_dma_sglist_init(&qsg, &n->parent_obj, 1);
        qemu_sglist_add(&qsg, addr, req->nlb * unit_len);

        status = ocssd_blk_setup_vector(n, ns, &qsg, ns->blk_offset, unit_len,
            req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_blk_setup_vector", status);
            goto out_sglist_destroy;
        }

        if (n->params.ms) {
            qsg.nsg = 0;
            qsg.size = 0;

            qemu_sglist_add(&qsg, req->cmd.mptr, req->nlb * unit_len_meta);

            status = ocssd_blk_setup_vector(n, ns, &qsg, ns->blk_offset_md,
                unit_len_meta, req);
            if (status) {
                trace_ocssd_err(req->cqe.cid, "ocssd_blk_setup_vector", status);
                goto out_sglist_destroy;
            }
        }

        QTAILQ_FOREACH(blk_req, &req->blk_req_tailq, tailq_entry) {
            qemu_iovec_init(&blk_req->iov, blk_req->qsg.nsg);
            _sglist_to_iov(n, &blk_req->qsg, &blk_req->iov);

            block_acct_start(blk_get_stats(n->conf.blk), &blk_req->acct,
                blk_req->iov.size, BLOCK_ACCT_WRITE);

            blk_req->aiocb = blk_aio_pwritev(n->conf.blk, blk_req->blk_offset,
                &blk_req->iov, 0, ocssd_copy_out_cb, blk_req);
        }

out_sglist_destroy:
        qemu_sglist_destroy(&qsg);

out:
        if (req->nlb > 1) {
            g_free(dlbal);
        }

        if (status != NVME_SUCCESS) {
            req->status = status;
            nvme_enqueue_req_completion(cq, req);
        }
    }
}

static uint16_t ocssd_copy(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    NvmeNamespace *ns = req->ns;
    OcssdCopyCmd *cpy = (OcssdCopyCmd *) cmd;
    NvmeBlockBackendRequest *blk_req;

    hwaddr addr = 0x0;
    uint64_t *lbal;
    uint64_t dulbe = 0;
    size_t unit_len = nvme_ns_lbads_bytes(ns);
    size_t unit_len_meta = nvme_ns_ms(ns);
    uint16_t status;

    trace_ocssd_copy(req->cqe.cid, req->nlb);

    if (req->nlb > OCSSD_CMD_MAX_LBAS) {
        trace_ocssd_err(req->cqe.cid, "OCSSD_CMD_MAX_LBAS exceeded",
            NVME_INVALID_FIELD | NVME_DNR);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    _get_lba_list(o, le64_to_cpu(cpy->lbal), &lbal, req);
    req->slba = (uint64_t) lbal;

    status = ocssd_rw_check_vector_req(o, cmd, req, &dulbe);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "ocssd_rw_check_vector_req",
            status);
        goto out;
    }

    if (NVME_ERR_REC_DULBE(n->features.err_rec)) {
        for (uint32_t i = 0; i < req->nlb; i++) {
            if (dulbe & (1 << i)) {
                status = NVME_DULB | NVME_DNR;
                goto out;
            }
        }
    }

    /*
     * For now, use bounce buffers to do the copy. Store the bounce buffer
     * addresses in the unused cdw12/cdw13 and mptr fields so it can be
     * referred to in the callback.
     */
    addr = (hwaddr) g_malloc_n(req->nlb, unit_len);
    req->cmd.cdw12 = addr >> 32;
    req->cmd.cdw13 = addr & 0xffffffff;

    QEMUSGList qsg;
    pci_dma_sglist_init(&qsg, &n->parent_obj, 1);
    qemu_sglist_add(&qsg, addr, req->nlb * unit_len);

    status = ocssd_blk_setup_vector(n, ns, &qsg, ns->blk_offset, unit_len,
        req);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "ocssd_blk_setup_vector", status);
        goto out_sglist_destroy;
    }

    if (n->params.ms) {
        req->cmd.mptr  = (hwaddr) g_malloc_n(req->nlb, unit_len_meta);

        qsg.nsg = 0;
        qsg.size = 0;

        qemu_sglist_add(&qsg, req->cmd.mptr, req->nlb * unit_len_meta);

        status = ocssd_blk_setup_vector(n, ns, &qsg, ns->blk_offset_md,
            unit_len_meta, req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_blk_setup_vector", status);
            goto out_sglist_destroy;
        }
    }

    QTAILQ_FOREACH(blk_req, &req->blk_req_tailq, tailq_entry) {
        qemu_iovec_init(&blk_req->iov, blk_req->qsg.nsg);
        _sglist_to_iov(n, &blk_req->qsg, &blk_req->iov);

        block_acct_start(blk_get_stats(n->conf.blk), &blk_req->acct,
            blk_req->iov.size, BLOCK_ACCT_READ);

        blk_req->aiocb = blk_aio_preadv(n->conf.blk, blk_req->blk_offset,
            &blk_req->iov, 0, ocssd_copy_in_cb, blk_req);
    }

out_sglist_destroy:
    qemu_sglist_destroy(&qsg);

out:
    if (req->nlb > 1) {
        g_free(lbal);
    }

    if (status) {
        g_free((void *) addr);
        g_free((void *) req->cmd.mptr);
        return status;
    }

    return NVME_NO_COMPLETE;
}


static void ocssd_rw_cb(void *opaque, int ret)
{
    NvmeBlockBackendRequest *blk_req = opaque;
    NvmeRequest *req = blk_req->req;
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];

    OcssdCtrl *o = OCSSD(n);

    trace_ocssd_rw_cb(req->cqe.cid, req->ns->id);

    QTAILQ_REMOVE(&req->blk_req_tailq, blk_req, tailq_entry);

    ocssd_rwc_aio_complete(o, blk_req, ret);
    nvme_blk_req_put(n, blk_req);

    if (QTAILQ_EMPTY(&req->blk_req_tailq)) {
        trace_nvme_enqueue_req_completion(req->cqe.cid, cq->cqid);
        nvme_enqueue_req_completion(cq, req);
    }
}

static uint16_t ocssd_rw(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;
    OcssdRwCmd *orw = (OcssdRwCmd *) cmd;

    uint64_t dulbe = 0;
    uint64_t *lbal;
    uint64_t lbal_addr = le64_to_cpu(orw->lbal);
    uint16_t status = NVME_SUCCESS;

    if (req->nlb > OCSSD_CMD_MAX_LBAS) {
        trace_ocssd_err(req->cqe.cid, "OCSSD_CMD_MAX_LBAS exceeded",
            NVME_INVALID_FIELD | NVME_DNR);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    _get_lba_list(o, lbal_addr, &lbal, req);
    req->slba = (uint64_t) lbal;

    _dprint_vector_rw(o, req);

    status = ocssd_rw_check_vector_req(o, cmd, req, &dulbe);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "ocssd_rw_check_vector_req", status);
        goto out;
    }

    if (!req->is_write && NVME_ERR_REC_DULBE(n->features.err_rec)) {
        for (uint32_t i = 0; i < req->nlb; i++) {
            if (dulbe & (1 << i)) {
                status = NVME_DULB | NVME_DNR;
                goto out;
            }
        }
    }

    status = nvme_blk_map(n, cmd, req, ocssd_blk_setup_vector);
    if (status) {
        trace_ocssd_err(req->cqe.cid, "nvme_blk_map", status);
        goto out;
    }

out:
    if (req->nlb > 1) {
        g_free((uint64_t *) req->slba);
    }

    if (status) {
        return status;
    }

    return nvme_blk_submit_io(n, req, ocssd_rw_cb);
}

static uint16_t ocssd_geometry(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    OcssdNamespace *ons;

    uint32_t nsid = le32_to_cpu(cmd->nsid);
    if (unlikely(nsid == 0 || nsid > o->nvme.params.num_ns)) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ons = &o->namespaces[nsid - 1];

    return nvme_dma_read(&o->nvme, (uint8_t *) &ons->id, sizeof(OcssdIdentity),
        cmd, req);
}

static uint16_t ocssd_get_log(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;

    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint16_t lid = dw10 & 0xff;
    uint8_t  rae = (dw10 >> 15) & 0x1;
    uint32_t numdl, numdu, len;
    uint64_t off, lpol, lpou;

    numdl = (dw10 >> 16);
    numdu = (dw11 & 0xffff);
    lpol = dw12;
    lpou = dw13;

    len = (((numdu << 16) | numdl) + 1) << 2;
    off = (lpou << 32ULL) | lpol;

    switch (lid) {
    case OCSSD_CHUNK_INFO:
        return ocssd_do_get_chunk_info(o, cmd, len, off, req);
    case OCSSD_CHUNK_NOTIFICATION:
        return ocssd_do_get_chunk_notification(o, cmd, len, off, rae, req);
    default:
        return nvme_get_log(n, cmd, req);
    }
}

static uint16_t ocssd_get_feature(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;

    uint32_t dw10 = le32_to_cpu(cmd->cdw10);

    trace_ocssd_getfeat(dw10);

    switch (dw10) {
    case OCSSD_MEDIA_FEEDBACK:
        req->cqe.cdw0 = cpu_to_le32(o->features.media_feedback);
        break;
    default:
        return nvme_get_feature(n, cmd, req);
    }

    return NVME_SUCCESS;
}

static uint16_t ocssd_set_feature(OcssdCtrl *o, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeCtrl *n = &o->nvme;

    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);

    trace_ocssd_setfeat(dw10, dw11);

    switch (dw10) {
    case NVME_ERROR_RECOVERY:
        n->features.err_rec = dw11;
        break;
    case OCSSD_MEDIA_FEEDBACK:
        o->features.media_feedback = dw11;
        break;
    default:
        return nvme_set_feature(n, cmd, req);
    }

    return NVME_SUCCESS;
}

static uint16_t ocssd_admin_cmd(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    OcssdCtrl *o = OCSSD(n);

    switch (cmd->opcode) {
    case NVME_ADM_CMD_SET_FEATURES:
        return ocssd_set_feature(o, cmd, req);
    case NVME_ADM_CMD_GET_FEATURES:
        return ocssd_get_feature(o, cmd, req);
    case OCSSD_ADM_CMD_GEOMETRY:
        return ocssd_geometry(o, cmd, req);
    case NVME_ADM_CMD_GET_LOG_PAGE:
        return ocssd_get_log(o, cmd, req);
    default:
        return nvme_admin_cmd(n, cmd, req);
    }
}

static uint16_t ocssd_io_cmd(NvmeCtrl *n, NvmeCmd *cmd, NvmeRequest *req)
{
    OcssdCtrl *o = OCSSD(n);
    NvmeRwCmd *rw;
    uint16_t status;

    uint32_t nsid = le32_to_cpu(cmd->nsid);

    if (unlikely(nsid == 0 || nsid > n->params.num_ns)) {
        trace_nvme_err_invalid_ns(nsid, n->params.num_ns);
        return NVME_INVALID_NSID | NVME_DNR;
    }

    trace_ocssd_io_cmd(req->cqe.cid, nsid, cmd->opcode);

    req->ns = &n->namespaces[nsid - 1];

    switch (cmd->opcode) {
    case NVME_CMD_READ:
    case NVME_CMD_WRITE:
        rw = (NvmeRwCmd *) cmd;

        req->nlb  = le16_to_cpu(rw->nlb) + 1;
        req->is_write = nvme_rw_is_write(req);
        req->slba = le64_to_cpu(rw->slba);

        trace_nvme_rw(req->is_write ? "write" : "read", req->nlb,
            req->nlb << nvme_ns_lbads(req->ns), req->slba);

        status = ocssd_rw_check_scalar_req(o, cmd, req);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "ocssd_rw_check_scalar_req", status);
            return status;
        }

        status = nvme_blk_map(n, cmd, req, ocssd_blk_setup_scalar);
        if (status) {
            trace_ocssd_err(req->cqe.cid, "nvme_blk_map", status);
            return status;
        }

        return nvme_blk_submit_io(n, req, ocssd_rw_cb);

    case NVME_CMD_DSM:
        return ocssd_dsm(o, cmd, req);

    case OCSSD_CMD_VECT_READ:
    case OCSSD_CMD_VECT_WRITE:
        rw = (NvmeRwCmd *) cmd;

        req->nlb = le16_to_cpu(rw->nlb) + 1;
        req->is_write = _is_write(req);

        trace_ocssd_rw(req->cqe.cid, nsid, req->cmd.opcode, req->nlb);

        return ocssd_rw(o, cmd, req);

    case OCSSD_CMD_VECT_COPY:
        rw = (NvmeRwCmd *) cmd;
        req->nlb = le16_to_cpu(rw->nlb) + 1;

        /* first phase of copy is a read */
        req->is_write = false;

        return ocssd_copy(o, cmd, req);

    case OCSSD_CMD_VECT_RESET:
        return ocssd_reset(o, cmd, req);

    default:
        return nvme_io_cmd(n, cmd, req);
    }
}

static uint64_t ocssd_ns_calc_blks(OcssdCtrl *o, OcssdNamespace *ons)
{
    NvmeNamespace *ns = ons->ns;
    return o->hdr.ns_size / (nvme_ns_lbads_bytes(ns) + nvme_ns_ms(ns));
}

static uint64_t ocssd_ns_calc_info_size(OcssdCtrl *o,
    OcssdNamespace *ons)
{
    OcssdIdGeo *geo = &ons->id.geo;
    uint64_t chks_total = geo->num_grp * geo->num_pu * geo->num_chk;

    return QEMU_ALIGN_UP(chks_total * sizeof(OcssdChunkDescriptor),
        o->hdr.sector_size);
}

static uint64_t ocssd_ns_calc_acct_size(OcssdCtrl *o, OcssdNamespace *ons)
{
    OcssdIdGeo *geo = &ons->id.geo;
    uint64_t chks_total = geo->num_grp * geo->num_pu * geo->num_chk;

    return QEMU_ALIGN_UP(chks_total * sizeof(OcssdChunkAcctDescriptor),
        o->hdr.sector_size);
}

static void ocssd_free_namespace(OcssdCtrl *o, OcssdNamespace *ons)
{
    g_free(ons->info.descr);
    g_free(ons->acct.descr);
    g_free(ons->resetfail);
    g_free(ons->writefail);
}

static void ocssd_free_namespaces(OcssdCtrl *o)
{
    for (int i = 0; i < o->hdr.num_ns; i++) {
        ocssd_free_namespace(o, &o->namespaces[i]);
    }
}

static int ocssd_init_namespace(OcssdCtrl *o, OcssdNamespace *ons,
    Error **errp)
{
    NvmeCtrl *n = &o->nvme;
    NvmeNamespace *ns = ons->ns;
    NvmeIdNs *id_ns = &ons->ns->id_ns;
    OcssdParams *params = &o->params;
    BlockBackend *blk = n->conf.blk;
    OcssdIdentity *id = &ons->id;
    OcssdIdGeo *geo = &id->geo;
    OcssdAddrF *addrf = &ons->addrf;
    Error *local_err = NULL;

    int ret;

    nvme_ns_init_identify(n, id_ns);

    /*
     * In addition to checking if the device has the NVME_QUIRK_LIGHTNVM quirk,
     * the Linux NVMe driver also checks if the first byte of the
     * vendor specific area in the identify namespace structure is set to 0x1.
     *
     * This is non-standard and Linux specific.
     */
    id_ns->vs[0] = 0x1;

    ret = blk_pread(blk, ns->blk_offset, id, sizeof(OcssdIdentity));
    if (ret < 0) {
        error_setg_errno(errp, -ret,
            "could not read namespace identity structure: ");
        return 1;
    }
    ns->blk_offset += sizeof(OcssdIdentity);

    if (params->ws_min != UINT32_MAX) {
        id->wrt.ws_min = cpu_to_le32(params->ws_min);
    }

    if (params->ws_opt != UINT32_MAX) {
        id->wrt.ws_opt = cpu_to_le32(params->ws_opt);
    }

    if (params->mw_cunits != UINT32_MAX) {
        id->wrt.mw_cunits = cpu_to_le32(params->mw_cunits);
    }

    if (params->mccap != UINT32_MAX) {
        id->mccap = params->mccap;
    }

    if (params->early_reset) {
        id->mccap |= OCSSD_IDENTITY_MCCAP_EARLY_RESET;
    }

    if (params->wit != UINT8_MAX) {
        id->wit = params->wit;
    }

    id_ns->lbaf[0].lbads = 63 - clz64(o->hdr.sector_size);
    id_ns->lbaf[0].ms = o->hdr.md_size;
    id_ns->nlbaf = 0;
    id_ns->flbas = 0;
    id_ns->mc = o->hdr.md_size ? 0x2 : 0;

    ons->acct.size = ocssd_ns_calc_acct_size(o, ons);
    ons->acct.descr = g_malloc0(ons->acct.size);
    ons->acct.blk_offset = ns->blk_offset;
    ns->blk_offset += ons->acct.size;

    ons->info.size = ocssd_ns_calc_info_size(o, ons);
    ons->info.descr = g_malloc0(ons->info.size);
    ons->info.blk_offset = ns->blk_offset;
    ns->blk_offset += ons->info.size;

    ns->ns_blks = ocssd_ns_calc_blks(o, ons);
    ns->ns_blks -= (sizeof(OcssdIdentity) + ons->info.size) /
        nvme_ns_lbads_bytes(ns);

    ns->blk_offset_md = ns->blk_offset + nvme_ns_lbads_bytes(ns) * ns->ns_blks;

    ons->chks_per_grp = geo->num_chk * geo->num_pu;
    ons->chks_total   = ons->chks_per_grp * geo->num_grp;
    ons->secs_per_chk = geo->clba;
    ons->secs_per_pu  = ons->secs_per_chk * geo->num_chk;
    ons->secs_per_grp = ons->secs_per_pu  * geo->num_pu;
    ons->secs_total   = ons->secs_per_grp * geo->clba;

    ocssd_ns_optimal_addrf(addrf, &id->lbaf);

    /*
     * Size of device (NSZE) is the entire address space (though some space is
     * not usable).
     */
    id_ns->nuse = id_ns->nsze =
        1ULL << (id->lbaf.sec_len + id->lbaf.chk_len +
            id->lbaf.pu_len + id->lbaf.grp_len);

    /*
     * Namespace capacity (NCAP) is set to the actual usable size in logical
     * blocks.
     */
    id_ns->ncap = ns->ns_blks;

    ret = ocssd_ns_load_chunk_info(o, ons);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not load chunk info");
        return 1;
    }

    ret = ocssd_ns_load_chunk_acct(o, ons);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not load chunk acct");
        return 1;
    }

    if (params->chunkinfo_fname) {
        if (ocssd_load_chunk_info_from_file(o, params->chunkinfo_fname,
            &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not load chunk info from file");
            return 1;
        }

        for (int i = 0; i < o->hdr.num_ns; i++) {
            ret = blk_pwrite(o->nvme.conf.blk, ons->info.blk_offset,
                ons->info.descr, ons->info.size, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "could not commit chunk info");
                return 1;
            }

            ret = blk_pwrite(o->nvme.conf.blk, ons->acct.blk_offset,
                ons->acct.descr, ons->acct.size, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "could not commit chunk acct");
                return 1;
            }
        }
    }

    for (int i = 0; i < ons->chks_total; i++) {
        OcssdChunkDescriptor *cnk = &ons->info.descr[i];
        ons->wear_index_total += cnk->wear_index;
    }

    ons->wear_index_avg = ons->wear_index_total / ons->chks_total;

    ons->resetfail = NULL;
    if (params->resetfail_fname) {
        ons->resetfail = g_malloc0_n(ons->chks_total, sizeof(*ons->resetfail));
        if (!ons->resetfail) {
            error_setg_errno(errp, ENOMEM, "could not allocate memory");
            return 1;
        }

        if (ocssd_load_reset_error_injection_from_file(o,
            params->resetfail_fname, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not load reset error injection from file");
            return 1;
        }
    }

    ons->writefail = NULL;
    if (params->writefail_fname) {
        ons->writefail = g_malloc0_n(ons->secs_total, sizeof(*ons->writefail));
        if (!ons->writefail) {
            error_setg_errno(errp, ENOMEM, "could not allocate memory");
            return 1;
        }

        if (ocssd_load_write_error_injection_from_file(o,
            params->writefail_fname, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "could not load write error injection from file");
            return 1;
        }

        /*
         * We fail resets for a chunk after a write failure to it, so make sure
         * to allocate the resetfailure buffer if it has not been already.
         */
        if (!ons->resetfail) {
            ons->resetfail = g_malloc0_n(ons->chks_total,
                sizeof(*ons->resetfail));
        }
    }

    return 0;
}

static int ocssd_init_namespaces(OcssdCtrl *o, Error **errp)
{
    NvmeCtrl *n = &o->nvme;
    Error *local_err = NULL;

    n->namespaces = g_new0(NvmeNamespace, o->hdr.num_ns);
    o->namespaces = g_new0(OcssdNamespace, o->hdr.num_ns);
    for (int i = 0; i < o->hdr.num_ns; i++) {
        OcssdNamespace *ons = &o->namespaces[i];
        NvmeNamespace *ns = ons->ns = &n->namespaces[i];

        ns->id = i + 1;
        ns->blk_offset = o->hdr.sector_size + i * o->hdr.ns_size;

        if (ocssd_init_namespace(o, ons, &local_err)) {
            error_propagate_prepend(errp, local_err,
                "init namespaces failed: ");
            return 1;
        }
    }

    return 0;
}

static void ocssd_realize(PCIDevice *pci_dev, Error **errp)
{
    int ret;

    OcssdCtrl *o = OCSSD(pci_dev);
    NvmeCtrl *n = &o->nvme;
    NvmeIdCtrl *id_ctrl = &n->id_ctrl;
    Error *local_err = NULL;

    n->namespaces = NULL;
    n->admin_cmd = ocssd_admin_cmd;
    n->io_cmd = ocssd_io_cmd;

    if (nvme_init_blk(n, &local_err)) {
        error_propagate_prepend(errp, local_err, "nvme_init_blk failed: ");
        return;
    }

    ret = blk_pread(n->conf.blk, 0, &o->hdr, sizeof(OcssdFormatHeader));
    if (ret < 0) {
        error_setg(errp, "could not read block format header");
        return;
    }

    n->params.num_ns = o->hdr.num_ns;
    n->params.ms = o->hdr.md_size;

    if (nvme_init_state(n, &local_err)) {
        error_propagate_prepend(errp, local_err, "nvme_init_state failed: ");
        return;
    }

    nvme_init_pci(n, pci_dev);

    pci_config_set_vendor_id(pci_dev->config, PCI_VENDOR_ID_CNEX);
    pci_config_set_device_id(pci_dev->config, 0x1f1f);

    ocssd_init_namespaces(o, errp);

    nvme_init_ctrl(n);

    n->id_ctrl.oncs |= cpu_to_le16(NVME_ONCS_DSM);

    strpadcpy((char *)id_ctrl->mn, sizeof(id_ctrl->mn),
        "QEMU NVM Express LightNVM Controller", ' ');
}

static void ocssd_exit(PCIDevice *pci_dev)
{
    OcssdCtrl *o = OCSSD(pci_dev);

    ocssd_free_namespaces(o);
    nvme_free_ctrl(&o->nvme, pci_dev);
}

static Property ocssd_props[] = {
    DEFINE_BLOCK_PROPERTIES(OcssdCtrl, nvme.conf),
    DEFINE_NVME_PROPERTIES(OcssdCtrl, nvme.params),
    DEFINE_OCSSD_PROPERTIES(OcssdCtrl, params),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription ocssd_vmstate = {
    .name = "ocssd",
    .unmigratable = 1,
};

static void ocssd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ocssd_realize;
    pc->exit = ocssd_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->vendor_id = PCI_VENDOR_ID_CNEX;
    pc->device_id = 0x1f1f;
    pc->revision = 2;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "OpenChannel 2.0 NVMe";
    dc->props = ocssd_props;
    dc->vmsd = &ocssd_vmstate;
}

static void ocssd_instance_init(Object *obj)
{
    OcssdCtrl *s = OCSSD(obj);

    device_add_bootindex_property(obj, &s->nvme.conf.bootindex,
                                  "bootindex", "/namespace@1,0",
                                  DEVICE(obj), &error_abort);
}

static const TypeInfo ocssd_info = {
    .name          = TYPE_OCSSD,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OcssdCtrl),
    .class_init    = ocssd_class_init,
    .instance_init = ocssd_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void ocssd_register_types(void)
{
    type_register_static(&ocssd_info);
}

type_init(ocssd_register_types)
