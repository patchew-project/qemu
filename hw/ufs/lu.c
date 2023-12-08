/*
 * QEMU UFS Logical Unit
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/memalign.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "sysemu/block-backend.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "ufs.h"

#define SCSI_COMMAND_FAIL (-1)
#define REPORT_ZONES_DESC_HD_SIZE (64)

static void ufs_build_upiu_sense_data(UfsRequest *req, uint8_t *sense,
                                      uint32_t sense_len)
{
    req->rsp_upiu.sr.sense_data_len = cpu_to_be16(sense_len);
    assert(sense_len <= SCSI_SENSE_LEN);
    memcpy(req->rsp_upiu.sr.sense_data, sense, sense_len);
}

static void ufs_build_scsi_response_upiu(UfsRequest *req, uint8_t *sense,
                                         uint32_t sense_len,
                                         uint32_t transfered_len,
                                         int16_t status)
{
    uint32_t expected_len = be32_to_cpu(req->req_upiu.sc.exp_data_transfer_len);
    uint8_t flags = 0, response = UFS_COMMAND_RESULT_SUCCESS;
    uint16_t data_segment_length;

    if (expected_len > transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(expected_len - transfered_len);
        flags |= UFS_UPIU_FLAG_UNDERFLOW;
    } else if (expected_len < transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(transfered_len - expected_len);
        flags |= UFS_UPIU_FLAG_OVERFLOW;
    }

    if (status != 0) {
        ufs_build_upiu_sense_data(req, sense, sense_len);
        response = UFS_COMMAND_RESULT_FAIL;
    }

    data_segment_length =
        cpu_to_be16(sense_len + sizeof(req->rsp_upiu.sr.sense_data_len));
    ufs_build_upiu_header(req, UFS_UPIU_TRANSACTION_RESPONSE, flags, response,
                          status, data_segment_length);
}

static inline uint32_t ufs_zone_idx(UfsLu *lu, uint64_t lba)
{
    return lba / lu->zone_desc.zone_size;
}

static inline UfsZoneState *ufs_get_zone_by_lba(UfsLu *lu, uint64_t lba)
{
    uint32_t zone_idx = ufs_zone_idx(lu, lba);

    if (zone_idx >= lu->zone_desc.nr_zones) {
        return NULL;
    }

    return &lu->zone_array[zone_idx];
}

static void ufs_assign_zone_cond(UfsLu *lu, UfsZoneState *zone,
                                 UfsZoneCond new_cond)
{
    switch (zone->cond) {
    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
        lu->zone_desc.nr_open--;
        break;
    case ZONE_COND_CLOSED:
    case ZONE_COND_FULL:
        break;
    default:
        break;
    }

    zone->cond = new_cond;

    switch (new_cond) {
    case ZONE_COND_EMPTY:
        zone->wp = zone->start;
        break;
    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
        lu->zone_desc.nr_open++;
        break;
    case ZONE_COND_CLOSED:
    case ZONE_COND_FULL:
    case ZONE_COND_READ_ONLY:
        break;
    default:
        break;
    }
}

static inline uint64_t ufs_zone_wr_boundary(UfsLu *lu, UfsZoneState *zone)
{
    return zone->start + lu->zone_desc.zone_cap;
}

static int ufs_full_zone(UfsLu *lu, UfsZoneState *zone)
{
    switch (zone->cond) {
    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
        ufs_assign_zone_cond(lu, zone, ZONE_COND_FULL);
        return 0;

    case ZONE_COND_EMPTY:
    case ZONE_COND_CLOSED:
    case ZONE_COND_READ_ONLY:
    case ZONE_COND_FULL:
    case ZONE_COND_OFFLINE:
    default:
        return SCSI_COMMAND_FAIL;
    }
}

static void ufs_scsi_command_complete(SCSIRequest *scsi_req, size_t resid)
{
    UfsRequest *req = scsi_req->hba_private;
    int16_t status = scsi_req->status;

    uint32_t transfered_len = scsi_req->cmd.xfer - resid;

    ufs_build_scsi_response_upiu(req, scsi_req->sense, scsi_req->sense_len,
                                 transfered_len, status);

    ufs_complete_req(req, UFS_REQUEST_SUCCESS);

    scsi_req->hba_private = NULL;
    scsi_req_unref(scsi_req);
}

static QEMUSGList *ufs_get_sg_list(SCSIRequest *scsi_req)
{
    UfsRequest *req = scsi_req->hba_private;
    return req->sg;
}

static int ufs_open_zone(UfsLu *lu, UfsZoneState *zone)
{
    switch (zone->cond) {
    case ZONE_COND_EMPTY:
    case ZONE_COND_CLOSED:
        if (lu->zone_desc.nr_open >= lu->zone_desc.max_open) {
            return SCSI_COMMAND_FAIL;
        }
        ufs_assign_zone_cond(lu, zone, ZONE_COND_IMPLICIT_OPEN);
        return 0;

    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
        return 0;

    case ZONE_COND_FULL:
    case ZONE_COND_READ_ONLY:
    case ZONE_COND_OFFLINE:
    default:
        return SCSI_COMMAND_FAIL;
    }
}

static const struct SCSIBusInfo ufs_scsi_info = {
    .tcq = true,
    .max_target = 0,
    .max_lun = UFS_MAX_LUS,
    .max_channel = 0,

    .get_sg_list = ufs_get_sg_list,
    .complete = ufs_scsi_command_complete,
};

static int ufs_emulate_report_luns(UfsRequest *req, uint8_t *outbuf,
                                   uint32_t outbuf_len)
{
    UfsHc *u = req->hc;
    int len = 0;

    /* TODO: Support for cases where SELECT REPORT is 1 and 2 */
    if (req->req_upiu.sc.cdb[2] != 0) {
        return SCSI_COMMAND_FAIL;
    }

    len += 8;

    for (uint8_t lun = 0; lun < UFS_MAX_LUS; ++lun) {
        if (u->lus[lun]) {
            if (len + 8 > outbuf_len) {
                break;
            }

            memset(outbuf + len, 0, 8);
            outbuf[len] = 0;
            outbuf[len + 1] = lun;
            len += 8;
        }
    }

    /* store the LUN list length */
    stl_be_p(outbuf, len - 8);

    return len;
}

static int ufs_scsi_emulate_vpd_page(UfsRequest *req, uint8_t *outbuf,
                                     uint32_t outbuf_len)
{
    uint8_t page_code = req->req_upiu.sc.cdb[2];
    int start, buflen = 0;

    outbuf[buflen++] = TYPE_WLUN;
    outbuf[buflen++] = page_code;
    outbuf[buflen++] = 0x00;
    outbuf[buflen++] = 0x00;
    start = buflen;

    switch (page_code) {
    case 0x00: /* Supported page codes, mandatory */
    {
        outbuf[buflen++] = 0x00; /* list of supported pages (this page) */
        outbuf[buflen++] = 0x87; /* mode page policy */
        break;
    }
    case 0x87: /* Mode Page Policy, mandatory */
    {
        outbuf[buflen++] = 0x3f; /* apply to all mode pages and subpages */
        outbuf[buflen++] = 0xff;
        outbuf[buflen++] = 0; /* shared */
        outbuf[buflen++] = 0;
        break;
    }
    default:
        return SCSI_COMMAND_FAIL;
    }
    /* done with EVPD */
    assert(buflen - start <= 255);
    outbuf[start - 1] = buflen - start;
    return buflen;
}

static int ufs_emulate_wlun_inquiry(UfsRequest *req, uint8_t *outbuf,
                                    uint32_t outbuf_len)
{
    if (outbuf_len < SCSI_INQUIRY_LEN) {
        return 0;
    }

    if (req->req_upiu.sc.cdb[1] & 0x1) {
        /* Vital product data */
        return ufs_scsi_emulate_vpd_page(req, outbuf, outbuf_len);
    }

    /* Standard INQUIRY data */
    if (req->req_upiu.sc.cdb[2] != 0) {
        return SCSI_COMMAND_FAIL;
    }

    outbuf[0] = TYPE_WLUN;
    outbuf[1] = 0;
    outbuf[2] = 0x6; /* SPC-4 */
    outbuf[3] = 0x2;
    outbuf[4] = 31;
    outbuf[5] = 0;
    outbuf[6] = 0;
    outbuf[7] = 0x2;
    strpadcpy((char *)&outbuf[8], 8, "QEMU", ' ');
    strpadcpy((char *)&outbuf[16], 16, "QEMU UFS", ' ');
    memset(&outbuf[32], 0, 4);

    return SCSI_INQUIRY_LEN;
}
static int ufs_emulate_zbc_vpd_page(UfsLu *lu, UfsRequest *req, uint8_t *outbuf,
                                    uint32_t outbuf_len)
{
    uint8_t page_code = req->req_upiu.sc.cdb[2];
    int start, buflen = 0;

    outbuf[buflen++] = TYPE_ZBC;
    outbuf[buflen++] = page_code;
    outbuf[buflen++] = 0x00;
    outbuf[buflen++] = 0x00;
    start = buflen;

    switch (page_code) {
    case 0x00: /* Supported page codes, mandatory */
    {
        outbuf[buflen++] = 0x00; /* list of supported pages (this page) */
        outbuf[buflen++] = 0x87; /* mode page policy */
        outbuf[buflen++] = 0xb1; /* block device characteristics */
        outbuf[buflen++] = 0xb6; /* ZBC device characteristics */
        break;
    }
    case 0xb6: /* ZBC device characteristics */
    {
        outbuf[buflen++] = 0x01; /* Host aware zoned block device model */
        /* reserved */
        outbuf[buflen++] = 0x00;
        outbuf[buflen++] = 0x00;
        outbuf[buflen++] = 0x00;

        /* optimal number of open sequential write preferred zones */
        stl_be_p(&outbuf[buflen], 0xffffffff);
        buflen += 4;

        /* optimal number of non-seq. written seq. write preferred zones  */
        stl_be_p(&outbuf[buflen], 0xffffffff);
        buflen += 4;

        /* maximum number of open sequential write required zones */
        if (lu->zone_desc.max_open) {
            stl_be_p(&outbuf[buflen], lu->zone_desc.max_open);
            buflen += 4;
        }

        /* reserved */
        outbuf[buflen++] = 0x00;
        outbuf[buflen++] = 0x00;
        outbuf[buflen++] = 0x00;
        break;
    }
    default:
        return SCSI_COMMAND_FAIL;
    }
    /* done with EVPD */
    assert(buflen - start <= 255);
    outbuf[start - 1] = buflen - start;
    return buflen;
}

static int ufs_scsi_emulate_zbc_in(UfsLu *lu, UfsRequest *req, uint8_t *outbuf)
{
    uint8_t *cdb = req->req_upiu.sc.cdb;
    uint64_t zone_start_lba, lba,
        max_lba = be64_to_cpu(lu->unit_desc.logical_block_count);
    uint32_t alloc_len, rep_opts, buf_offset;
    bool partial;
    unsigned int rep_max_zones, num_zones = 0;
    UfsZoneState *zone;

    if (cdb[1] != ZI_REPORT_ZONES) {
        return SCSI_COMMAND_FAIL;
    }

    zone_start_lba = ldq_be_p(&cdb[2]);
    alloc_len = ldl_be_p(&cdb[10]);

    if (alloc_len == 0) {
        return 0;
    }
    rep_opts = cdb[14] & 0x3f;
    partial = cdb[14] & 0x80;

    if (zone_start_lba > max_lba) {
        return SCSI_COMMAND_FAIL;
    }

    rep_max_zones = alloc_len / REPORT_ZONES_DESC_HD_SIZE;
    buf_offset = REPORT_ZONES_DESC_HD_SIZE;

    for (lba = zone_start_lba; lba < max_lba; lba = zone->start + zone->size) {
        zone = ufs_get_zone_by_lba(lu, lba);
        if (!zone) {
            break;
        }

        switch (rep_opts) {
        case 0x00:
            /* All zones */
            break;
        case 0x01:
            /* Empty zones */
            if (zone->cond != ZONE_COND_EMPTY) {
                continue;
            }
            break;
        case 0x02:
            /* Implicit open zones */
            if (zone->cond != ZONE_COND_IMPLICIT_OPEN) {
                continue;
            }
            break;
        case 0x03:
            /* Explicit open zones */
            if (zone->cond != ZONE_COND_EXPLICIT_OPEN) {
                continue;
            }
            break;
        case 0x04:
            /* Closed zones */
            if (zone->cond != ZONE_COND_CLOSED) {
                continue;
            }
            break;
        case 0x05:
            /* Full zones */
            if (zone->cond != ZONE_COND_FULL) {
                continue;
            }
            break;
        case 0x06:
        case 0x07:
        case 0x10:
            /*
             * Read-only, offline, reset WP recommended are
             * not emulated: no zones to report;
             */
            continue;
        case 0x11:
            /* non-seq-resource set */
            break;
        case 0x3e:
            /* All zones except gap zones. */
            break;
        case 0x3f:
            /* Not write pointer (conventional) zones */
            break;
        default:
            return SCSI_COMMAND_FAIL;
        }

        if (num_zones < rep_max_zones) {
            /* Fill zone descriptor */
            outbuf[0 + buf_offset] = zone->type;
            outbuf[1 + buf_offset] = zone->cond << 4;

            stq_be_p(&outbuf[8 + buf_offset], zone->size);
            stq_be_p(&outbuf[16 + buf_offset], zone->start);
            stq_be_p(&outbuf[24 + buf_offset], zone->wp);
            buf_offset += REPORT_ZONES_DESC_HD_SIZE;
        }

        if (partial && num_zones >= rep_max_zones) {
            break;
        }

        num_zones++;
    }

    /* Report header */
    /* Zone list length. */
    stl_be_p(&outbuf[0], num_zones * REPORT_ZONES_DESC_HD_SIZE);
    /* Maximum LBA */
    stq_be_p(&outbuf[8], be64_to_cpu(lu->unit_desc.logical_block_count) - 1);
    /* Zone starting LBA granularity. */
    if (lu->zone_desc.zone_cap < lu->zone_desc.zone_size) {
        stq_be_p(&outbuf[16], lu->zone_desc.zone_size);
    }

    return buf_offset;
}

static void ufs_reset_write_pointer_zone(UfsLu *lu, UfsZoneState *zone)
{
    switch (zone->cond) {
    case ZONE_COND_EMPTY:
        /* nothing to do */
        break;
    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
    case ZONE_COND_READ_ONLY:
    case ZONE_COND_OFFLINE:
        /* ignore reset write pointer */
        break;
    case ZONE_COND_CLOSED:
    case ZONE_COND_FULL:
        ufs_assign_zone_cond(lu, zone, ZONE_COND_EMPTY);
        break;
    default:
        break;
    }
}

static void ufs_reset_write_pointer_all(UfsLu *lu)
{
    unsigned int i;

    for (i = 0; i < lu->zone_desc.nr_zones; i++) {
        ufs_reset_write_pointer_zone(lu, &lu->zone_array[i]);
    }
}

static int ufs_scsi_emulate_zbc_out(UfsLu *lu, UfsRequest *req)
{
    uint8_t *cdb = req->req_upiu.sc.cdb;
    uint64_t zone_start_lba,
        max_lba = be64_to_cpu(lu->unit_desc.logical_block_count);
    UfsZoneState *zone;

    switch (cdb[1]) {
    case ZO_CLOSE_ZONE:
        break;
    case ZO_FINISH_ZONE: {
        zone_start_lba = ldq_be_p(&cdb[2]);

        if (zone_start_lba > max_lba) {
            /* overflow lba range */
            return SCSI_COMMAND_FAIL;
        }

        zone = ufs_get_zone_by_lba(lu, zone_start_lba);

        if (zone_start_lba != zone->start) {
            /* invalid field error */
            return SCSI_COMMAND_FAIL;
        }

        if (ufs_full_zone(lu, zone)) {
            /* fail to finish zone */
            return SCSI_COMMAND_FAIL;
        }
        break;
    }
    case ZO_OPEN_ZONE:
        /* TODO: support open zone command */
        break;

    case ZO_RESET_WRITE_POINTER: {
        bool all = cdb[14] & 0x01;

        if (all) {
            ufs_reset_write_pointer_all(lu);
            return 0;
        }

        zone_start_lba = ldq_be_p(&cdb[2]);

        if (zone_start_lba > max_lba) {
            /* overflow lba range */
            return SCSI_COMMAND_FAIL;
        }

        zone = ufs_get_zone_by_lba(lu, zone_start_lba);

        if ((zone->cond == ZONE_COND_READ_ONLY) ||
            (zone->cond == ZONE_COND_OFFLINE))
            return SCSI_COMMAND_FAIL;

        if (zone_start_lba != zone->start) {
            /* invalid field error */
            return SCSI_COMMAND_FAIL;
        }

        ufs_reset_write_pointer_zone(lu, zone);
        break;
    }
    default:
        return SCSI_COMMAND_FAIL;
    }

    return 0;
}

static int ufs_check_zone_state_for_write(UfsZoneState *zone)
{
    switch (zone->cond) {
    case ZONE_COND_EMPTY:
    case ZONE_COND_IMPLICIT_OPEN:
    case ZONE_COND_EXPLICIT_OPEN:
    case ZONE_COND_CLOSED:
        return 0;

    case ZONE_COND_FULL:
    case ZONE_COND_READ_ONLY:
    case ZONE_COND_OFFLINE:
    default:
        return SCSI_COMMAND_FAIL;
    }
}

/* Unaligned Write fail */
const struct SCSISense sense_code_UNALIGNED_WRITE = { .key = ILLEGAL_REQUEST,
                                                      .asc = 0x21,
                                                      .ascq = 0x04 };

/* Insufficient zone resource fail */
const struct SCSISense sense_code_INSUFFICENT_ZONE_RESOURCES = {
    .key = DATA_PROTECT, .asc = 0x0C, .ascq = 0x12
};


static int ufs_check_zone_write(UfsLu *lu, UfsZoneState *zone, uint64_t lba,
                                uint32_t len)
{
    if (ufs_check_zone_state_for_write(zone)) {
        return SCSI_COMMAND_FAIL;
    }

    if (unlikely((lba + len) > ufs_zone_wr_boundary(lu, zone))) {
        return SCSI_COMMAND_FAIL;
    }

    return 0;
}

static UfsReqResult ufs_emulate_zbc_cmd(UfsLu *lu, UfsRequest *req)
{
    g_autofree uint8_t *outbuf = NULL;
    uint8_t sense_buf[UFS_SENSE_SIZE];
    uint8_t scsi_status;
    int len = 0;

    switch (req->req_upiu.sc.cdb[0]) {
    case WRITE_6:
    case WRITE_10:
        scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
        scsi_status = CHECK_CONDITION;
        break;
    case WRITE_16: {
        uint64_t lba = ldq_be_p(&req->req_upiu.sc.cdb[2]);
        uint32_t req_len = ldl_be_p(&req->req_upiu.sc.cdb[10]);
        UfsZoneState *zone = ufs_get_zone_by_lba(lu, lba);

        if (unlikely(lba != zone->wp)) {
            /* unaligned write error */
            scsi_build_sense(sense_buf, SENSE_CODE(UNALIGNED_WRITE));
            scsi_status = CHECK_CONDITION;
            break;
        }

        len = ufs_check_zone_write(lu, zone, lba, req_len);
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
            break;
        }

        len = ufs_open_zone(lu, zone);
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INSUFFICENT_ZONE_RESOURCES));
            scsi_status = CHECK_CONDITION;
            break;
        }

        zone->wp += req_len;

        if (zone->wp == ufs_zone_wr_boundary(lu, zone)) {
            ufs_full_zone(lu, zone);
        }
        return UFS_REQUEST_NO_COMPLETE;
    }
    case ZBC_IN:
        outbuf = g_new0(uint8_t, req->data_len + REPORT_ZONES_DESC_HD_SIZE);
        len = ufs_scsi_emulate_zbc_in(lu, req, outbuf);
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case ZBC_OUT:
        len = ufs_scsi_emulate_zbc_out(lu, req);
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case INQUIRY:
        /* bypass standard inquiry */
        if (!(req->req_upiu.sc.cdb[1] & 0x1)) {
            return UFS_REQUEST_NO_COMPLETE;
        }
        /* processing only ZBC related page codes */
        if (!(req->req_upiu.sc.cdb[2] == 0x00) &&
            !(req->req_upiu.sc.cdb[2] == 0xb6)) {
            return UFS_REQUEST_NO_COMPLETE;
        }
        /* Vital product data */
        outbuf = g_new0(uint8_t, UFS_BLOCK_SIZE);
        len = ufs_emulate_zbc_vpd_page(lu, req, outbuf, UFS_BLOCK_SIZE);
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    default:
        return UFS_REQUEST_NO_COMPLETE;
    }

    len = MIN(len, (int)req->data_len);
    if (scsi_status == GOOD && len > 0 &&
        dma_buf_read(outbuf, len, NULL, req->sg, MEMTXATTRS_UNSPECIFIED) !=
            MEMTX_OK) {
        return UFS_REQUEST_FAIL;
    }

    ufs_build_scsi_response_upiu(req, sense_buf, sizeof(sense_buf), len,
                                 scsi_status);

    return UFS_REQUEST_SUCCESS;
}

static UfsReqResult ufs_emulate_scsi_cmd(UfsLu *lu, UfsRequest *req)
{
    uint8_t lun = lu->lun;
    uint8_t outbuf[4096];
    uint8_t sense_buf[UFS_SENSE_SIZE];
    uint8_t scsi_status;
    int len = 0;

    switch (req->req_upiu.sc.cdb[0]) {
    case REPORT_LUNS:
        len = ufs_emulate_report_luns(req, outbuf, sizeof(outbuf));
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case INQUIRY:
        len = ufs_emulate_wlun_inquiry(req, outbuf, sizeof(outbuf));
        if (len == SCSI_COMMAND_FAIL) {
            scsi_build_sense(sense_buf, SENSE_CODE(INVALID_FIELD));
            scsi_status = CHECK_CONDITION;
        } else {
            scsi_status = GOOD;
        }
        break;
    case REQUEST_SENSE:
        /* Just return no sense data */
        len = scsi_build_sense_buf(outbuf, sizeof(outbuf), SENSE_CODE(NO_SENSE),
                                   true);
        scsi_status = GOOD;
        break;
    case START_STOP:
        /* TODO: Revisit it when Power Management is implemented */
        if (lun == UFS_UPIU_UFS_DEVICE_WLUN) {
            scsi_status = GOOD;
            break;
        }
        /* fallthrough */
    default:
        scsi_build_sense(sense_buf, SENSE_CODE(INVALID_OPCODE));
        scsi_status = CHECK_CONDITION;
    }

    len = MIN(len, (int)req->data_len);
    if (scsi_status == GOOD && len > 0 &&
        dma_buf_read(outbuf, len, NULL, req->sg, MEMTXATTRS_UNSPECIFIED) !=
            MEMTX_OK) {
        return UFS_REQUEST_FAIL;
    }

    ufs_build_scsi_response_upiu(req, sense_buf, sizeof(sense_buf), len,
                                 scsi_status);
    return UFS_REQUEST_SUCCESS;
}

static UfsReqResult ufs_process_scsi_cmd(UfsLu *lu, UfsRequest *req)
{
    uint8_t task_tag = req->req_upiu.header.task_tag;

    if (lu->zone_enabled) {
        UfsReqResult result = ufs_emulate_zbc_cmd(lu, req);
        /* UFS_REQUEST_NO_COMPLETE continues command processing */
        if (result != UFS_REQUEST_NO_COMPLETE) {
            return result;
        }
    }
    /*
     * Each ufs-lu has its own independent virtual SCSI bus. Therefore, we can't
     * use scsi_target_emulate_report_luns() which gets all lu information over
     * the SCSI bus. Therefore, we use ufs_emulate_scsi_cmd() like the
     * well-known lu.
     */
    if (req->req_upiu.sc.cdb[0] == REPORT_LUNS) {
        return ufs_emulate_scsi_cmd(lu, req);
    }

    SCSIRequest *scsi_req =
        scsi_req_new(lu->scsi_dev, task_tag, lu->lun, req->req_upiu.sc.cdb,
                     UFS_CDB_SIZE, req);

    uint32_t len = scsi_req_enqueue(scsi_req);
    if (len) {
        scsi_req_continue(scsi_req);
    }

    return UFS_REQUEST_NO_COMPLETE;
}

static Property ufs_lu_props[] = {
    DEFINE_PROP_DRIVE("drive", UfsLu, conf.blk),
    DEFINE_PROP_UINT8("lun", UfsLu, lun, 0),
    DEFINE_PROP_BOOL("zoned", UfsLu, params.zoned, false),
    DEFINE_PROP_SIZE("zoned.zone_size", UfsLu, params.zone_size,
                     UFS_DEFAULT_ZONE_SIZE /* 128MiB */),
    DEFINE_PROP_SIZE("zoned.zone_capacity", UfsLu, params.zone_cap, 0),
    DEFINE_PROP_UINT32("zoned.max_open", UfsLu, params.zone_max_open, 6),
    DEFINE_PROP_END_OF_LIST(),
};

static bool ufs_add_lu(UfsHc *u, UfsLu *lu, Error **errp)
{
    BlockBackend *blk = lu->conf.blk;
    int64_t brdv_len = blk_getlength(blk);
    uint64_t raw_dev_cap =
        be64_to_cpu(u->geometry_desc.total_raw_device_capacity);

    if (u->device_desc.number_lu >= UFS_MAX_LUS) {
        error_setg(errp, "ufs host controller has too many logical units.");
        return false;
    }

    if (u->lus[lu->lun] != NULL) {
        error_setg(errp, "ufs logical unit %d already exists.", lu->lun);
        return false;
    }

    u->lus[lu->lun] = lu;
    u->device_desc.number_lu++;
    raw_dev_cap += (brdv_len >> UFS_GEOMETRY_CAPACITY_SHIFT);
    u->geometry_desc.total_raw_device_capacity = cpu_to_be64(raw_dev_cap);
    return true;
}

void ufs_init_wlu(UfsLu *wlu, uint8_t wlun)
{
    wlu->lun = wlun;
    wlu->scsi_op = &ufs_emulate_scsi_cmd;
}

static void ufs_zoned_init_state(UfsLu *lu)
{
    uint64_t start = 0, zone_size = lu->zone_desc.zone_size;
    uint64_t nblocks = be64_to_cpu(lu->unit_desc.logical_block_count);
    UfsZoneState *zone;
    int i;

    lu->zone_array = g_new0(UfsZoneState, lu->zone_desc.nr_zones);
    zone = lu->zone_array;

    for (i = 0; i < lu->zone_desc.nr_zones; i++, zone++) {
        if (start + zone_size > nblocks) {
            zone_size = nblocks - start;
        }
        zone->id = i;
        /* SEQUENTIAL WRITE REQUIRED */
        zone->type = 0x2;
        zone->cond = ZONE_COND_EMPTY;
        zone->start = start;
        zone->size = zone_size;
        zone->wp = start;

        start += zone_size;
    }
}

static void ufs_lu_init_zoned(UfsLu *lu)
{
    ufs_zoned_init_state(lu);

    lu->zone_desc.nr_open = 0;
    lu->zone_desc.max_open = lu->params.zone_max_open;
    lu->zone_enabled = true;

    /* Host-managed zoned block device */
    lu->scsi_dev->type = TYPE_ZBC;
}

static int ufs_lu_zoned_check_calc_geometry(UfsLu *lu, Error **errp)
{
    uint64_t zone_size = lu->params.zone_size, zone_cap = lu->params.zone_cap,
             nr_blocks;

    if (!zone_cap) {
        zone_cap = zone_size;
    }

    if (zone_cap > zone_size) {
        error_setg(errp,
                   "zone capacity %" PRIu64 "B exceeds "
                   "zone size %" PRIu64 "B",
                   zone_cap, zone_size);
        return -1;
    }

    nr_blocks = be64_to_cpu(lu->unit_desc.logical_block_count);
    lu->zone_desc.zone_size = zone_size / UFS_BLOCK_SIZE;
    lu->zone_desc.zone_cap = zone_cap / UFS_BLOCK_SIZE;
    lu->zone_desc.nr_zones = DIV_ROUND_UP(nr_blocks, lu->zone_desc.zone_size);

    if (!lu->zone_desc.nr_zones) {
        error_setg(errp,
                   "insufficient drive capacity, must be at least the size "
                   "of one zone (%" PRIu64 "B)",
                   zone_size);
        return -1;
    }

    return 0;
}

static void ufs_init_lu(UfsLu *lu)
{
    BlockBackend *blk = lu->conf.blk;
    int64_t brdv_len = blk_getlength(blk);

    memset(&lu->unit_desc, 0, sizeof(lu->unit_desc));
    lu->zone_enabled = false;
    lu->unit_desc.length = sizeof(UnitDescriptor);
    lu->unit_desc.descriptor_idn = UFS_QUERY_DESC_IDN_UNIT;
    lu->unit_desc.lu_enable = 0x01;
    lu->unit_desc.logical_block_size = UFS_BLOCK_SIZE_SHIFT;
    lu->unit_desc.unit_index = lu->lun;
    lu->unit_desc.logical_block_count =
        cpu_to_be64(brdv_len / (1 << lu->unit_desc.logical_block_size));

    lu->scsi_op = &ufs_process_scsi_cmd;
}

static bool ufs_lu_check_constraints(UfsLu *lu, Error **errp)
{
    if (!lu->conf.blk) {
        error_setg(errp, "drive property not set");
        return false;
    }

    if (lu->lun >= UFS_MAX_LUS) {
        error_setg(errp, "lun must be between 0 and %d", UFS_MAX_LUS - 1);
        return false;
    }

    return true;
}

static void ufs_init_scsi_device(UfsLu *lu, BlockBackend *blk, Error **errp)
{
    DeviceState *scsi_dev;

    scsi_bus_init(&lu->bus, sizeof(lu->bus), DEVICE(lu), &ufs_scsi_info);

    blk_ref(blk);
    blk_detach_dev(blk, DEVICE(lu));
    lu->conf.blk = NULL;

    /*
     * The ufs-lu is the device that is wrapping the scsi-hd. It owns a virtual
     * SCSI bus that serves the scsi-hd.
     */
    scsi_dev = qdev_new("scsi-hd");
    object_property_add_child(OBJECT(&lu->bus), "ufs-scsi", OBJECT(scsi_dev));

    qdev_prop_set_uint32(scsi_dev, "physical_block_size", UFS_BLOCK_SIZE);
    qdev_prop_set_uint32(scsi_dev, "logical_block_size", UFS_BLOCK_SIZE);
    qdev_prop_set_uint32(scsi_dev, "scsi-id", 0);
    qdev_prop_set_uint32(scsi_dev, "lun", lu->lun);
    if (!qdev_prop_set_drive_err(scsi_dev, "drive", blk, errp)) {
        object_unparent(OBJECT(scsi_dev));
        return;
    }

    if (!qdev_realize_and_unref(scsi_dev, &lu->bus.qbus, errp)) {
        object_unparent(OBJECT(scsi_dev));
        return;
    }

    blk_unref(blk);
    lu->scsi_dev = SCSI_DEVICE(scsi_dev);
}

static void ufs_lu_realize(DeviceState *dev, Error **errp)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);
    BusState *s = qdev_get_parent_bus(dev);
    UfsHc *u = UFS(s->parent);
    BlockBackend *blk = lu->conf.blk;

    if (!ufs_lu_check_constraints(lu, errp)) {
        return;
    }

    if (!blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!blkconf_blocksizes(&lu->conf, errp)) {
        return;
    }

    if (!blkconf_apply_backend_options(&lu->conf, !blk_supports_write_perm(blk),
                                       true, errp)) {
        return;
    }

    ufs_init_lu(lu);
    if (!ufs_add_lu(u, lu, errp)) {
        return;
    }

    ufs_init_scsi_device(lu, blk, errp);

    if (lu->params.zoned) {
        if (ufs_lu_zoned_check_calc_geometry(lu, errp)) {
            return;
        }
        ufs_lu_init_zoned(lu);
    }
}

static void ufs_lu_unrealize(DeviceState *dev)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);

    if (lu->scsi_dev) {
        object_unref(OBJECT(lu->scsi_dev));
        lu->scsi_dev = NULL;
    }
    if (lu->zone_enabled) {
        /* release zoned ufs structure */
        g_free(lu->zone_array);
    }
}

static void ufs_lu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ufs_lu_realize;
    dc->unrealize = ufs_lu_unrealize;
    dc->bus_type = TYPE_UFS_BUS;
    device_class_set_props(dc, ufs_lu_props);
    dc->desc = "Virtual UFS logical unit";
}

static const TypeInfo ufs_lu_info = {
    .name = TYPE_UFS_LU,
    .parent = TYPE_DEVICE,
    .class_init = ufs_lu_class_init,
    .instance_size = sizeof(UfsLu),
};

static void ufs_lu_register_types(void)
{
    type_register_static(&ufs_lu_info);
}

type_init(ufs_lu_register_types)
