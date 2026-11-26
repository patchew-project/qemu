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
#include "system/block-backend.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "ufs.h"

#define SCSI_COMMAND_FAIL (-1)
#define REPORT_ZONES_HEADER_SIZE 64
#define REPORT_ZONES_DESC_SIZE 64
#define ZBC_VPD_B6_LEN 64
/* zones per zone report of the drive */
#define UFS_ZBC_REPORT_BATCH 256

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

    data_segment_length = sense_len + sizeof(req->rsp_upiu.sr.sense_data_len);
    ufs_build_upiu_header(req, UFS_UPIU_TRANSACTION_RESPONSE, flags, response,
                          status, data_segment_length);
}

/*
 * The wrapped scsi-hd is a direct-access (TYPE_DISK) device, so every
 * command that a zoned LU forwards to it, including MODE SENSE, takes the
 * regular disk path. Only the identification differs: a zoned LU reports
 * itself as a host-managed zoned block device. Set the peripheral device
 * type of the INQUIRY data that scsi-hd built to TYPE_ZBC, and add the ZBC
 * device characteristics VPD page (emulated in ufs_emulate_zbc_cmd()) to the
 * list of supported VPD pages. Since WRITE SAME is rejected, also clear its
 * maximum length in the Block Limits VPD page and its bits in the Logical
 * Block Provisioning VPD page. scsi-hd pads its data with zeros up to the
 * allocation length, and len is never larger than that.
 */
static void ufs_zoned_fixup_inquiry(const uint8_t *cdb, uint8_t *buf,
                                    uint32_t len)
{
    uint8_t page_len;

    if (len == 0) {
        return;
    }

    buf[0] = TYPE_ZBC;

    if (!(cdb[1] & 0x1) || len < 4) {
        return;
    }

    /* WRITE SAME is rejected, see ufs_emulate_zbc_cmd() */
    if (cdb[2] == 0xb0) {
        /* Block Limits: maximum WRITE SAME length */
        if (len > 36) {
            memset(&buf[36], 0, MIN(len, 44) - 36);
        }
        return;
    }
    if (cdb[2] == 0xb2) {
        /* Logical Block Provisioning: clear LBPWS and LBPWS10 */
        if (len > 5) {
            buf[5] &= ~0x60;
        }
        return;
    }
    if (cdb[2] != 0x00) {
        return;
    }

    /* Supported VPD pages, in ascending order: 0xb6 goes last */
    page_len = buf[3];
    if (page_len < 255 && !memchr(&buf[4], 0xb6, MIN(page_len, len - 4))) {
        buf[3] = page_len + 1;
        if (4 + page_len < len) {
            buf[4 + page_len] = 0xb6;
        }
    }
}

/* Whether a zoned LU adjusts the data that scsi-hd returns for cdb */
static bool ufs_zoned_fixup_cmd(const uint8_t *cdb)
{
    return cdb[0] == INQUIRY ||
           (cdb[0] == SERVICE_ACTION_IN_16 &&
            (cdb[1] & 0x1f) == SAI_READ_CAPACITY_16);
}

#define UFS_GROUP_NUMBER_MASK 0x1F
#define UFS_WB_GROUP_NUMBER_DEFAULT 0x00 /* 00000b */
#define UFS_WB_GROUP_NUMBER_PINNED 0x18 /* 11000b */
static bool ufs_wb_check_write_pinned(UfsHc *u, UfsRequest *req)
{
    uint8_t cmd = req->req_upiu.sc.cdb[0];
    uint8_t group_number = UFS_WB_GROUP_NUMBER_DEFAULT;

    if (u->attributes.wb_buffer_partial_flush_mode != UFS_WB_FLUSH_PINNED) {
        return false;
    }

    if (cmd == WRITE_16) {
        group_number = req->req_upiu.sc.cdb[14] & UFS_GROUP_NUMBER_MASK;

    } else if (cmd == WRITE_10) {
        group_number = req->req_upiu.sc.cdb[6] & UFS_GROUP_NUMBER_MASK;
    }

    return (group_number == UFS_WB_GROUP_NUMBER_PINNED);
}

static void ufs_wb_process_write_normal(UfsHc *u, uint32_t transfered_len)
{
    UfsWb *wb = &u->wb;
    uint64_t curr_bytes, used_bytes, remain_bytes;

    if (!wb->curr_bytes) {
        return;
    }

    curr_bytes = wb->curr_bytes - wb->pinned_curr_bytes;
    used_bytes = wb->used_bytes - wb->pinned_used_bytes;

    if (used_bytes >= curr_bytes) {
        return;
    }

    remain_bytes = curr_bytes - used_bytes;
    wb->used_bytes += MIN(remain_bytes, transfered_len);
}

#define UFS_WB_TOTAL_WRITTEN_DIV (10 * 1024 * 1024) /* 10MiB */
static void ufs_wb_process_write_pinned(UfsHc *u, uint32_t transfered_len)
{
    UfsWb *wb = &u->wb;
    uint64_t remain_bytes, remain_data;
    uint32_t total_written;

    if (!wb->pinned_curr_bytes) {
        ufs_wb_process_write_normal(u, transfered_len);
        return;
    }

    if (wb->pinned_used_bytes >= wb->pinned_curr_bytes) {
        ufs_wb_process_write_normal(u, transfered_len);
        return;
    }

    remain_bytes = wb->pinned_curr_bytes - wb->pinned_used_bytes;
    if (remain_bytes >= transfered_len) {
        wb->pinned_total_written_bytes += transfered_len;
        wb->pinned_used_bytes += transfered_len;
        wb->used_bytes += transfered_len;
        remain_data = 0;

    } else {
        wb->pinned_total_written_bytes += remain_bytes;
        wb->pinned_used_bytes += remain_bytes;
        wb->used_bytes += remain_bytes;
        remain_data = transfered_len - remain_bytes;
    }

    total_written = wb->pinned_total_written_bytes / UFS_WB_TOTAL_WRITTEN_DIV;
    u->attributes.pinned_wb_cumm_written_size = cpu_to_be32(total_written);

    ufs_wb_process_write_normal(u, remain_data);
}

static void ufs_wb_process_write_req(UfsRequest *req, uint32_t transfered_len)
{
    UfsHc *u = req->hc;

    if (!u->flags.wb_en || !ufs_is_write_req(req)) {
        return;
    }

    if (ufs_wb_check_write_pinned(u, req)) {
        ufs_wb_process_write_pinned(u, transfered_len);
    } else {
        ufs_wb_process_write_normal(u, transfered_len);
    }

    ufs_wb_update_avail_buffer(u);
}

#define UFS_HID_MAX_FRAGMENTS 1024
static void ufs_hid_count_fragments(UfsRequest *req)
{
    UfsHc *u = req->hc;

    if (!ufs_is_write_req(req)) {
        return;
    }

    u->hid_fragment_count =
        MIN(u->hid_fragment_count + 1, UFS_HID_MAX_FRAGMENTS);
}

static void ufs_scsi_command_complete(SCSIRequest *scsi_req, size_t resid)
{
    UfsRequest *req = scsi_req->hba_private;
    int16_t status = scsi_req->status;
    uint32_t transfered_len = scsi_req->cmd.xfer - resid;

    assert(req != NULL);
    req->sreq = NULL;

    /* WB / HID accounting should only happen for successful commands */
    if (status == GOOD) {
        ufs_wb_process_write_req(req, transfered_len);
        ufs_hid_count_fragments(req);
    }

    ufs_build_scsi_response_upiu(req, scsi_req->sense, scsi_req->sense_len,
                                 transfered_len, status);

    ufs_complete_req(req, UFS_REQUEST_SUCCESS);

    scsi_req->hba_private = NULL;
    scsi_req_unref(scsi_req);
}

static void ufs_scsi_command_cancelled(SCSIRequest *scsi_req)
{
    UfsRequest *req = scsi_req->hba_private;

    assert(req != NULL);
    req->sreq = NULL;
    scsi_req->hba_private = NULL;
    scsi_req_unref(scsi_req);
}

static QEMUSGList *ufs_get_sg_list(SCSIRequest *scsi_req)
{
    UfsRequest *req = scsi_req->hba_private;
    UfsLu *lu = container_of(scsi_req->bus, UfsLu, bus);

    /*
     * INQUIRY and READ CAPACITY(16) data of a zoned LU go through
     * ufs_scsi_transfer_data()
     */
    if (lu->zoned && ufs_zoned_fixup_cmd(scsi_req->cmd.buf)) {
        return NULL;
    }

    return req->sg;
}

static void ufs_scsi_transfer_data(SCSIRequest *scsi_req, uint32_t len)
{
    UfsRequest *req = scsi_req->hba_private;
    uint8_t *buf = scsi_req_get_buf(scsi_req);

    /* Only these commands of a zoned LU have no SG list */
    assert(ufs_zoned_fixup_cmd(scsi_req->cmd.buf) && req->sg);

    if (scsi_req->cmd.buf[0] == INQUIRY) {
        ufs_zoned_fixup_inquiry(scsi_req->cmd.buf, buf, len);
    } else if (len > 12) {
        /*
         * RC BASIS 1: the returned LBA is the last one of the LU, not of
         * the conventional zones at its start
         */
        buf[12] = (buf[12] & ~0x30) | 0x10;
    }
    dma_buf_read(buf, len, NULL, req->sg, MEMTXATTRS_UNSPECIFIED);
    scsi_req_continue(scsi_req);
}

static const struct SCSIBusInfo ufs_scsi_info = {
    .tcq = true,
    .max_target = 0,
    .max_lun = UFS_MAX_LUS,
    .max_channel = 0,

    .get_sg_list = ufs_get_sg_list,
    .transfer_data = ufs_scsi_transfer_data,
    .complete = ufs_scsi_command_complete,
    .cancel = ufs_scsi_command_cancelled,
};

static bool ufs_append_lun(uint8_t *outbuf, uint32_t outbuf_len, int *len,
                           uint8_t lun, uint8_t lun_msb)
{
    if (*len + 8 > outbuf_len) {
        return false;
    }

    memset(outbuf + *len, 0, 8);
    outbuf[*len] = lun_msb;
    outbuf[*len + 1] = lun;
    *len += 8;

    return true;
}

static int ufs_emulate_report_luns(UfsRequest *req, uint8_t *outbuf,
                                   uint32_t outbuf_len)
{
    UfsHc *u = req->hc;
    int len = 0;

    uint8_t report_lun_cmd = req->req_upiu.sc.cdb[2];

    if (report_lun_cmd > 2) {
        return SCSI_COMMAND_FAIL;
    }

    if (outbuf_len < 8) {
        return SCSI_COMMAND_FAIL;
    }

    memset(outbuf, 0, 8);
    len += 8;

    if (report_lun_cmd == 2 || report_lun_cmd == 1) {
        ufs_append_lun(outbuf, outbuf_len, &len, u->report_wlu.lun & 0x7F,
                       UFS_WLUN_MSB);
        ufs_append_lun(outbuf, outbuf_len, &len, u->dev_wlu.lun & 0x7F,
                       UFS_WLUN_MSB);
        ufs_append_lun(outbuf, outbuf_len, &len, u->boot_wlu.lun & 0x7F,
                       UFS_WLUN_MSB);
        ufs_append_lun(outbuf, outbuf_len, &len, u->rpmb_wlu.lun & 0x7F,
                       UFS_WLUN_MSB);
    }

    if (report_lun_cmd == 0 || report_lun_cmd == 2) {
        for (uint8_t lun = 0; lun < UFS_MAX_LUS; ++lun) {
            if (u->lus[lun] &&
                !ufs_append_lun(outbuf, outbuf_len, &len, lun, 0)) {
                break;
            }
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

/* ZBC device characteristics VPD page */
static int ufs_emulate_zbc_vpd_page(UfsLu *lu, uint8_t *outbuf)
{
    uint32_t max_open = blk_get_max_open_zones(lu->scsi_dev->conf.blk);

    memset(outbuf, 0, ZBC_VPD_B6_LEN);
    outbuf[0] = TYPE_ZBC;
    outbuf[1] = 0xb6;
    stw_be_p(&outbuf[2], ZBC_VPD_B6_LEN - 4);

    /*
     * URSWRZ: reads are not restricted by the write pointer. qcow2 does not
     * restrict them, and Linux exposes no host-managed drive that does.
     */
    outbuf[4] = 0x01;
    /* optimal number of open sequential write preferred zones */
    stl_be_p(&outbuf[8], 0xffffffff);
    /* optimal number of non-seq. written seq. write preferred zones */
    stl_be_p(&outbuf[12], 0xffffffff);
    /* maximum number of open sequential write required zones */
    stl_be_p(&outbuf[16], max_open ? max_open : 0xffffffff);
    /* zone alignment method 0 and starting LBA granularity 0: zone length */

    return ZBC_VPD_B6_LEN;
}

static const struct SCSISense sense_code_INSUFFICIENT_RESOURCES = {
    .key = ILLEGAL_REQUEST, .asc = 0x55, .ascq = 0x03
};

/*
 * A ZBC IN or ZBC OUT command. It runs in a coroutine, since the zones are
 * those of the drive and the zoned block layer API reads and changes them.
 * It holds a reference to the drive and copies what it needs of the LU, so
 * that it can run on after the request is cancelled and the LU is gone.
 */
typedef struct UfsZbcCmd {
    BlockBackend *blk;
    uint8_t lun;
    uint64_t nr_blocks;
    uint64_t zone_size;
    uint32_t nr_zones;
    /* NULL once the UFS request is cancelled, see ufs_zbc_cancel() */
    UfsRequest *req;
    uint8_t cdb[UFS_CDB_SIZE];
    /* REPORT ZONES data */
    uint8_t *outbuf;
    uint32_t outbuf_len;
    uint32_t len;
    int status;
    SCSISense sense;
} UfsZbcCmd;

/* Byte offset of the zone with index idx */
static inline int64_t ufs_zone_offset(UfsZbcCmd *zc, uint32_t idx)
{
    return (int64_t)(idx * zc->zone_size) << UFS_BLOCK_SIZE_SHIFT;
}

static void ufs_zbc_fail(UfsZbcCmd *zc, SCSISense sense)
{
    zc->status = CHECK_CONDITION;
    zc->sense = sense;
}

static void ufs_zbc_fail_errno(UfsZbcCmd *zc, int ret)
{
    if (scsi_sense_from_zone_errno(-ret, &zc->sense)) {
        zc->status = CHECK_CONDITION;
    } else {
        zc->status = scsi_sense_from_errno(-ret, &zc->sense);
    }
}

/* REPORT ZONES reporting options */
static bool ufs_zbc_report_option_valid(uint8_t rep_opts)
{
    switch (rep_opts) {
    case 0x00 ... 0x07:
    case 0x10:
    case 0x11:
    case 0x3e:
    case 0x3f:
        return true;
    default:
        return false;
    }
}

/*
 * The BLK_ZS_* zone states have the values of the ZBC zone conditions, and
 * the BLK_ZT_* zone types those of the ZBC zone types.
 */
QEMU_BUILD_BUG_ON(BLK_ZS_NOT_WP != 0x0 || BLK_ZS_EMPTY != 0x1 ||
                  BLK_ZS_IOPEN != 0x2 || BLK_ZS_EOPEN != 0x3 ||
                  BLK_ZS_CLOSED != 0x4 || BLK_ZS_RDONLY != 0xd ||
                  BLK_ZS_FULL != 0xe || BLK_ZS_OFFLINE != 0xf);
QEMU_BUILD_BUG_ON(BLK_ZT_CONV != 0x1 || BLK_ZT_SWR != 0x2 ||
                  BLK_ZT_SWP != 0x3);

static bool ufs_zbc_report_option_match(BlockZoneState state, uint8_t rep_opts)
{
    switch (rep_opts) {
    case 0x00: /* all zones */
    case 0x3e: /* all zones except gap zones */
        return true;
    case 0x01:
        return state == BLK_ZS_EMPTY;
    case 0x02:
        return state == BLK_ZS_IOPEN;
    case 0x03:
        return state == BLK_ZS_EOPEN;
    case 0x04:
        return state == BLK_ZS_CLOSED;
    case 0x05:
        return state == BLK_ZS_FULL;
    case 0x06:
        return state == BLK_ZS_RDONLY;
    case 0x07:
        return state == BLK_ZS_OFFLINE;
    case 0x3f:
        return state == BLK_ZS_NOT_WP;
    default:
        /*
         * Reset write pointer recommended and non-sequential write resources
         * active zones: none exist.
         */
        return false;
    }
}

/*
 * zc->outbuf holds at least the header and otherwise no more than the smaller
 * of the allocation length and the whole report.
 */
static void coroutine_fn ufs_zbc_report_zones(UfsZbcCmd *zc)
{
    uint8_t *cdb = zc->cdb;
    uint64_t nr_blocks = zc->nr_blocks;
    uint64_t zone_start_lba = ldq_be_p(&cdb[2]);
    uint32_t alloc_len = ldl_be_p(&cdb[10]);
    uint8_t rep_opts = cdb[14] & 0x3f;
    bool partial = cdb[14] & 0x80;
    g_autofree BlockZoneDescriptor *zones = NULL;
    uint32_t limit, max_descs, nr_descs = 0;
    uint32_t offset = REPORT_ZONES_HEADER_SIZE;
    uint32_t idx;

    trace_ufs_zbc_report_zones(zc->lun, zone_start_lba, alloc_len, rep_opts);

    if ((cdb[1] & 0x1f) != UFS_ZBC_REPORT_ZONES ||
        !ufs_zbc_report_option_valid(rep_opts)) {
        ufs_zbc_fail(zc, SENSE_CODE(INVALID_FIELD));
        return;
    }

    if (zone_start_lba >= nr_blocks) {
        ufs_zbc_fail(zc, SENSE_CODE(LBA_OUT_OF_RANGE));
        return;
    }

    if (alloc_len == 0) {
        return;
    }

    assert(zc->outbuf_len >= REPORT_ZONES_HEADER_SIZE);
    limit = MIN(alloc_len, zc->outbuf_len);
    max_descs = limit > REPORT_ZONES_HEADER_SIZE ?
                (limit - REPORT_ZONES_HEADER_SIZE) / REPORT_ZONES_DESC_SIZE :
                0;

    /* The zone list length counts every matching zone, so look at them all */
    zones = g_new(BlockZoneDescriptor, UFS_ZBC_REPORT_BATCH);
    idx = zone_start_lba / zc->zone_size;
    while (idx < zc->nr_zones) {
        unsigned int i, n = MIN(UFS_ZBC_REPORT_BATCH, zc->nr_zones - idx);
        int ret;

        /* Nobody waits for the report of a cancelled request */
        if (!zc->req) {
            return;
        }

        ret = blk_co_zone_report(zc->blk, ufs_zone_offset(zc, idx), &n,
                                 zones);
        if (ret < 0) {
            ufs_zbc_fail_errno(zc, ret);
            return;
        }
        if (n == 0) {
            break;
        }

        for (i = 0; i < n; i++) {
            BlockZoneDescriptor *zone = &zones[i];
            uint8_t *desc = &zc->outbuf[offset];

            if (!ufs_zbc_report_option_match(zone->state, rep_opts)) {
                continue;
            }

            if (nr_descs < max_descs) {
                desc[0] = zone->type;
                desc[1] = zone->state << 4;
                stq_be_p(&desc[8], zone->length >> UFS_BLOCK_SIZE_SHIFT);
                stq_be_p(&desc[16], zone->start >> UFS_BLOCK_SIZE_SHIFT);
                stq_be_p(&desc[24], zone->wp >> UFS_BLOCK_SIZE_SHIFT);
                offset += REPORT_ZONES_DESC_SIZE;
            } else if (partial) {
                goto out;
            }

            nr_descs++;
        }
        idx += n;
    }

out:
    /* Zone list length and maximum LBA */
    stl_be_p(&zc->outbuf[0], nr_descs * REPORT_ZONES_DESC_SIZE);
    stq_be_p(&zc->outbuf[8], nr_blocks - 1);

    zc->len = MIN(offset, limit);
}

static bool ufs_zbc_zone_op_of(uint8_t action, BlockZoneOp *op)
{
    switch (action) {
    case UFS_ZBC_CLOSE_ZONE:
        *op = BLK_ZO_CLOSE;
        return true;
    case UFS_ZBC_FINISH_ZONE:
        *op = BLK_ZO_FINISH;
        return true;
    case UFS_ZBC_OPEN_ZONE:
        *op = BLK_ZO_OPEN;
        return true;
    case UFS_ZBC_RESET_WRITE_POINTER:
        *op = BLK_ZO_RESET;
        return true;
    default:
        return false;
    }
}

/*
 * Whether a zone action changes a zone in the given state. For the others,
 * ZBC completes the command without doing anything, for example OPEN ZONE
 * of a full zone, and so does the ALL bit for the zones that it skips. A
 * single read-only or offline zone goes to the drive, which fails the action.
 */
static bool ufs_zbc_action_applies(uint8_t action, BlockZoneState state,
                                   bool all)
{
    switch (state) {
    case BLK_ZS_EMPTY:
        return !all && (action == UFS_ZBC_OPEN_ZONE ||
                        action == UFS_ZBC_FINISH_ZONE);
    case BLK_ZS_IOPEN:
        return !all || action != UFS_ZBC_OPEN_ZONE;
    case BLK_ZS_EOPEN:
        return action != UFS_ZBC_OPEN_ZONE;
    case BLK_ZS_CLOSED:
        return action != UFS_ZBC_CLOSE_ZONE;
    case BLK_ZS_FULL:
        return action == UFS_ZBC_RESET_WRITE_POINTER;
    case BLK_ZS_RDONLY:
    case BLK_ZS_OFFLINE:
        return !all;
    default:
        return false;
    }
}

/*
 * A zone action with the ALL bit, other than RESET WRITE POINTER, which the
 * drive applies to all zones at once. OPEN ZONE opens the zones that are
 * closed when it starts, and fails without changing anything if they and
 * the explicitly open zones together exceed the open zone limit. Opening
 * one can close an implicitly open zone, which must not be opened then.
 * FINISH ZONE finishes the closed zones last, since the drive needs a free
 * open zone to finish one.
 */
static void coroutine_fn ufs_zbc_zone_action_all(UfsZbcCmd *zc, uint8_t action,
                                                 BlockZoneOp op)
{
    BlockBackend *blk = zc->blk;
    uint32_t max_open = blk_get_max_open_zones(blk);
    g_autofree BlockZoneDescriptor *zones = NULL;
    g_autoptr(GArray) closed = NULL;
    uint32_t idx, nr_open = 0;
    unsigned int i, n;
    int ret;

    zones = g_new(BlockZoneDescriptor, UFS_ZBC_REPORT_BATCH);
    if (action == UFS_ZBC_OPEN_ZONE || action == UFS_ZBC_FINISH_ZONE) {
        closed = g_array_new(false, false, sizeof(uint32_t));
    }

    for (idx = 0; idx < zc->nr_zones; idx += n) {
        /* The request was cancelled: change no more zones */
        if (!zc->req) {
            return;
        }
        n = MIN(UFS_ZBC_REPORT_BATCH, zc->nr_zones - idx);
        ret = blk_co_zone_report(blk, ufs_zone_offset(zc, idx), &n, zones);
        if (ret < 0) {
            ufs_zbc_fail_errno(zc, ret);
            return;
        }
        if (n == 0) {
            break;
        }

        for (i = 0; i < n; i++) {
            BlockZoneDescriptor *zone = &zones[i];
            uint32_t zone_idx = idx + i;

            if (closed && zone->state == BLK_ZS_CLOSED) {
                g_array_append_val(closed, zone_idx);
                continue;
            }
            if (action == UFS_ZBC_OPEN_ZONE) {
                nr_open += zone->state == BLK_ZS_EOPEN;
                continue;
            }
            if (!ufs_zbc_action_applies(action, zone->state, true)) {
                continue;
            }
            if (!zc->req) {
                return;
            }
            ret = blk_co_zone_mgmt(blk, op, zone->start, zone->length);
            if (ret < 0) {
                trace_ufs_zbc_zone_mgmt_failed(zc->lun, action, zone->start,
                                               ret);
                ufs_zbc_fail_errno(zc, ret);
                return;
            }
        }
    }

    if (!closed) {
        return;
    }

    if (action == UFS_ZBC_OPEN_ZONE && max_open &&
        nr_open + closed->len > max_open) {
        ufs_zbc_fail(zc, SENSE_CODE(INSUFFICIENT_ZONE_RESOURCES));
        return;
    }

    for (i = 0; i < closed->len; i++) {
        int64_t start = ufs_zone_offset(zc, g_array_index(closed, uint32_t, i));
        int64_t end = MIN(start + ufs_zone_offset(zc, 1),
                          (int64_t)zc->nr_blocks << UFS_BLOCK_SIZE_SHIFT);

        if (!zc->req) {
            return;
        }
        ret = blk_co_zone_mgmt(blk, op, start, end - start);
        if (ret < 0) {
            trace_ufs_zbc_zone_mgmt_failed(zc->lun, action, start, ret);
            ufs_zbc_fail_errno(zc, ret);
            return;
        }
    }
}

static void coroutine_fn ufs_zbc_zone_action(UfsZbcCmd *zc)
{
    BlockBackend *blk = zc->blk;
    uint8_t *cdb = zc->cdb;
    uint64_t nr_blocks = zc->nr_blocks;
    uint8_t action = cdb[1] & 0x1f;
    uint64_t zone_id = ldq_be_p(&cdb[2]);
    uint16_t zone_count = lduw_be_p(&cdb[12]);
    bool all = cdb[14] & 0x01;
    BlockZoneDescriptor zone;
    unsigned int n = 1;
    BlockZoneOp op;
    int ret;

    trace_ufs_zbc_zone_op(zc->lun, action, zone_id, all);

    if (!ufs_zbc_zone_op_of(action, &op)) {
        ufs_zbc_fail(zc, SENSE_CODE(INVALID_FIELD));
        return;
    }

    if (all) {
        if (action != UFS_ZBC_RESET_WRITE_POINTER) {
            ufs_zbc_zone_action_all(zc, action, op);
            return;
        }
        ret = blk_co_zone_mgmt(blk, op, 0, nr_blocks << UFS_BLOCK_SIZE_SHIFT);
        if (ret < 0) {
            trace_ufs_zbc_zone_mgmt_failed(zc->lun, action, 0, ret);
            ufs_zbc_fail_errno(zc, ret);
        }
        return;
    }

    /* Operations on a range of zones are not supported */
    if (zone_count > 1) {
        ufs_zbc_fail(zc, SENSE_CODE(INVALID_FIELD));
        return;
    }

    if (zone_id >= nr_blocks) {
        ufs_zbc_fail(zc, SENSE_CODE(LBA_OUT_OF_RANGE));
        return;
    }

    ret = blk_co_zone_report(blk, zone_id << UFS_BLOCK_SIZE_SHIFT, &n, &zone);
    if (ret < 0) {
        ufs_zbc_fail_errno(zc, ret);
        return;
    }

    /* The zone ID is the first LBA of a sequential write required zone */
    if (n != 1 || zone.start != zone_id << UFS_BLOCK_SIZE_SHIFT ||
        zone.type == BLK_ZT_CONV) {
        ufs_zbc_fail(zc, SENSE_CODE(INVALID_FIELD));
        return;
    }

    if (!ufs_zbc_action_applies(action, zone.state, false) || !zc->req) {
        return;
    }

    ret = blk_co_zone_mgmt(blk, op, zone.start, zone.length);
    if (ret < 0) {
        trace_ufs_zbc_zone_mgmt_failed(zc->lun, action, zone.start, ret);
        ufs_zbc_fail_errno(zc, ret);
    }
}

static void coroutine_fn ufs_zbc_co(void *opaque)
{
    UfsZbcCmd *zc = opaque;
    UfsRequest *req;
    uint8_t sense_buf[UFS_SENSE_SIZE] = { 0 };
    UfsReqResult result = UFS_REQUEST_SUCCESS;
    uint32_t xfer_len = 0;

    if (zc->cdb[0] == UFS_ZBC_IN) {
        ufs_zbc_report_zones(zc);
    } else {
        ufs_zbc_zone_action(zc);
    }

    req = zc->req;
    if (req) {
        /*
         * The coroutine may resume outside the controller's MMIO and BH
         * handlers. Engage its reentrancy guard, so that a PRDT that points
         * at the controller's registers cannot reset it under this request.
         */
        MemReentrancyGuard *guard = &req->hc->dev->mem_reentrancy_guard;
        bool engaged = guard->engaged_in_io;

        guard->engaged_in_io = true;
        req->zbc_cmd = NULL;

        if (zc->status == CHECK_CONDITION) {
            scsi_build_sense(sense_buf, zc->sense);
        } else if (zc->status == GOOD) {
            xfer_len = MIN(zc->len, req->data_len);
        }

        if (xfer_len > 0 &&
            dma_buf_read(zc->outbuf, xfer_len, NULL, req->sg,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            result = UFS_REQUEST_FAIL;
        } else {
            ufs_build_scsi_response_upiu(req, sense_buf, sizeof(sense_buf),
                                         xfer_len, zc->status);
        }
        ufs_complete_req(req, result);
        guard->engaged_in_io = engaged;
    }

    /* The LU may be gone, so this can drop the last reference */
    blk_co_unref(zc->blk);
    g_free(zc->outbuf);
    g_free(zc);
}

/*
 * A cancelled request is cleared or completed by the caller. The command
 * runs on, but leaves the request alone, and changes no more zones. A zone
 * action already sent to the drive may still complete.
 */
void ufs_zbc_cancel(UfsRequest *req)
{
    if (req->zbc_cmd) {
        req->zbc_cmd->req = NULL;
        req->zbc_cmd = NULL;
    }
}

static void ufs_zbc_start(UfsLu *lu, UfsRequest *req, uint8_t *outbuf,
                          uint32_t outbuf_len)
{
    UfsZbcCmd *zc = g_new0(UfsZbcCmd, 1);

    zc->blk = lu->scsi_dev->conf.blk;
    blk_ref(zc->blk);
    zc->lun = lu->lun;
    zc->nr_blocks = be64_to_cpu(lu->unit_desc.logical_block_count);
    zc->zone_size = lu->zone_size;
    zc->nr_zones = lu->nr_zones;
    zc->req = req;
    memcpy(zc->cdb, req->req_upiu.sc.cdb, UFS_CDB_SIZE);
    zc->outbuf = outbuf;
    zc->outbuf_len = outbuf_len;
    zc->status = GOOD;
    req->zbc_cmd = zc;

    qemu_coroutine_enter(qemu_coroutine_create(ufs_zbc_co, zc));
}

/*
 * Commands that a zoned LU handles itself, which return true with the result
 * of the request. Everything else, including READ, WRITE, MODE SENSE and the
 * INQUIRY data that ufs_zoned_fixup_inquiry() adjusts, goes to the wrapped
 * scsi-hd. Writes reach the drive through it, and the drive checks them
 * against its zones. scsi-hd reports the zone errors with their sense codes.
 */
static bool ufs_emulate_zbc_cmd(UfsLu *lu, UfsRequest *req,
                                UfsReqResult *result)
{
    g_autofree uint8_t *outbuf = NULL;
    uint8_t *cdb = req->req_upiu.sc.cdb;
    uint8_t sense_buf[UFS_SENSE_SIZE];
    uint8_t scsi_status = GOOD;
    uint32_t len = 0, xfer_len;
    SCSISense sense;

    /*
     * Let scsi-hd report a pending unit attention first, as it does for
     * every command it does not exempt (INQUIRY and REPORT LUNS among them),
     * so that no zone changes before the guest has seen it.
     */
    if (cdb[0] != INQUIRY &&
        (lu->scsi_dev->unit_attention.key == UNIT_ATTENTION ||
         lu->bus.unit_attention.key == UNIT_ATTENTION)) {
        return false;
    }

    switch (cdb[0]) {
    case UFS_ZBC_IN: {
        uint32_t alloc_len = ldl_be_p(&cdb[10]);
        uint64_t report_len = REPORT_ZONES_HEADER_SIZE +
                              (uint64_t)REPORT_ZONES_DESC_SIZE * lu->nr_zones;
        uint32_t outbuf_len;

        /* As for the commands that reach scsi-hd, the PRDT must hold it */
        if (!ufs_fit_data_buffer(req, alloc_len)) {
            *result = UFS_REQUEST_MISMATCH_DATA_BUF;
            return true;
        }

        /*
         * Size the buffer by what can be returned, not by alloc_len alone:
         * at most 64 bytes for each zone of the LU, plus the header.
         */
        outbuf_len = MIN(alloc_len, report_len);
        outbuf_len = MAX(outbuf_len, REPORT_ZONES_HEADER_SIZE);
        outbuf = g_try_malloc0(outbuf_len);
        if (!outbuf) {
            sense = SENSE_CODE(INSUFFICIENT_RESOURCES);
            scsi_status = CHECK_CONDITION;
            break;
        }
        ufs_zbc_start(lu, req, g_steal_pointer(&outbuf), outbuf_len);
        *result = UFS_REQUEST_NO_COMPLETE;
        return true;
    }
    case UFS_ZBC_OUT:
        ufs_zbc_start(lu, req, NULL, 0);
        *result = UFS_REQUEST_NO_COMPLETE;
        return true;
    case INQUIRY:
        /* Only the ZBC device characteristics VPD page is emulated here */
        if (!(cdb[1] & 0x1) || cdb[2] != 0xb6) {
            return false;
        }
        if (!ufs_fit_data_buffer(req, lduw_be_p(&cdb[3]))) {
            *result = UFS_REQUEST_MISMATCH_DATA_BUF;
            return true;
        }
        outbuf = g_malloc0(ZBC_VPD_B6_LEN);
        len = MIN(ufs_emulate_zbc_vpd_page(lu, outbuf), lduw_be_p(&cdb[3]));
        break;
    case WRITE_SAME_10:
    case WRITE_SAME_16:
        /*
         * Not a UFS command (Linux sets no_write_same for UFS), and scsi-hd
         * turns one with NDOB set or a zero block into write-zeroes, which
         * the image does not check against the zones.
         */
        sense = SENSE_CODE(INVALID_OPCODE);
        scsi_status = CHECK_CONDITION;
        break;
    default:
        return false;
    }

    if (scsi_status != GOOD) {
        scsi_build_sense(sense_buf, sense);
        len = 0;
    }

    xfer_len = MIN(len, req->data_len);
    if (xfer_len > 0 &&
        dma_buf_read(outbuf, xfer_len, NULL, req->sg,
                     MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        *result = UFS_REQUEST_FAIL;
        return true;
    }

    ufs_build_scsi_response_upiu(req, sense_buf, sizeof(sense_buf), xfer_len,
                                 scsi_status);

    *result = UFS_REQUEST_SUCCESS;
    return true;
}

static UfsReqResult ufs_emulate_scsi_cmd(UfsLu *lu, UfsRequest *req)
{
    uint8_t lun = lu->lun;
    QEMU_UNINITIALIZED uint8_t outbuf[4096];
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
    uint8_t *cdb = req->req_upiu.sc.cdb;
    uint8_t sync_cache_10[UFS_CDB_SIZE] = { SYNCHRONIZE_CACHE };
    SCSIRequest *scsi_req;
    uint32_t len;

    if (lu->zoned) {
        UfsReqResult result;

        if (ufs_emulate_zbc_cmd(lu, req, &result)) {
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

    /*
     * scsi-hd implements SYNCHRONIZE CACHE(10) only, while Linux sends the
     * 16-byte variant to zoned disks. scsi-hd flushes the whole medium for
     * SYNCHRONIZE CACHE(10) whatever its LBA range, so pass the 16-byte
     * command on as the 10-byte one, keeping IMMED.
     */
    if (cdb[0] == SYNCHRONIZE_CACHE_16) {
        sync_cache_10[1] = cdb[1] & 0x02;
        cdb = sync_cache_10;
    }

    scsi_req = scsi_req_new(lu->scsi_dev, task_tag, lu->lun, cdb, UFS_CDB_SIZE,
                            req);

    /*
     * scsi-hd moves as many bytes as the SG list holds, and without an SG
     * list it would call the transfer_data callback, which only handles the
     * INQUIRY and READ CAPACITY(16) data of a zoned LU. A UFS device moves
     * what the CDB asks for, so fit the PRDT to the transfer length, and
     * fail a request whose PRDT describes less, a missing PRDT included,
     * with OCS MISMATCH DATA BUFFER SIZE, as virtio-scsi fails a buffer
     * that does not match the command. The response reports the rest of the
     * Expected Data Transfer Length as a residual. A request that reports a
     * pending unit attention moves no data; its sense is already set, so
     * let it through and the guest sees the UA.
     */
    if (!scsi_req->sense_len &&
        !ufs_fit_data_buffer(req, scsi_req->cmd.xfer)) {
        scsi_req_unref(scsi_req);
        return UFS_REQUEST_MISMATCH_DATA_BUF;
    }

    req->sreq = scsi_req;

    len = scsi_req_enqueue(scsi_req);
    if (len) {
        scsi_req_continue(scsi_req);
    }

    return UFS_REQUEST_NO_COMPLETE;
}

static const Property ufs_lu_props[] = {
    DEFINE_PROP_DRIVE("drive", UfsLu, conf.blk),
    DEFINE_PROP_UINT8("lun", UfsLu, lun, 0),
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

/*
 * Check every zone of the drive and return the number of its sequential
 * zones, or -1 with errp set. ZBC has no zone capacity: a sequential zone
 * whose capacity is below its length would need gap zones, which are not
 * modelled.
 */
static int64_t ufs_lu_check_zones(BlockBackend *blk, uint64_t nr_zones,
                                  Error **errp)
{
    g_autofree BlockZoneDescriptor *zones =
        g_new(BlockZoneDescriptor, UFS_ZBC_REPORT_BATCH);
    int64_t zone_bytes = blk_get_zone_size(blk);
    uint64_t idx = 0;
    int64_t nr_seq = 0;

    while (idx < nr_zones) {
        unsigned int i, n = MIN(UFS_ZBC_REPORT_BATCH, nr_zones - idx);
        int ret = blk_zone_report(blk, idx * zone_bytes, &n, zones);

        if (ret < 0) {
            error_setg_errno(errp, -ret, "cannot report the zones of the "
                             "drive");
            return -1;
        }
        if (n == 0) {
            break;
        }
        for (i = 0; i < n; i++) {
            if (zones[i].type == BLK_ZT_CONV) {
                continue;
            }
            if (zones[i].cap != zones[i].length) {
                error_setg(errp, "the zone at offset %" PRIu64 " of the "
                           "drive has a capacity (%" PRIu64 ") below its "
                           "length (%" PRIu64 ")", zones[i].start,
                           zones[i].cap, zones[i].length);
                return -1;
            }
            nr_seq++;
        }
        idx += n;
    }
    return nr_seq;
}

/*
 * A logical unit on a host-managed zoned drive is a zoned LU. Its zones are
 * those of the drive, whose geometry must fit the 4 KiB logical blocks.
 */
static bool ufs_lu_init_zoned(UfsLu *lu, Error **errp)
{
    ERRP_GUARD();
    BlockBackend *blk = lu->conf.blk;
    uint64_t nr_blocks = be64_to_cpu(lu->unit_desc.logical_block_count);
    uint64_t zone_size = blk_get_zone_size(blk);
    BlockConf conf = lu->conf;
    uint64_t nr_zones;
    int64_t nr_seq;

    switch (blk_get_zone_model(blk)) {
    case BLK_Z_NONE:
    case BLK_Z_HA:
        /* A host-aware drive takes any write, so it is a regular LU */
        return true;
    case BLK_Z_HM:
        break;
    default:
        g_assert_not_reached();
    }

    if (!blk_supports_write_perm(blk)) {
        error_setg(errp, "a zoned logical unit needs a writable drive");
        return false;
    }

    if (blk_getlength(blk) % UFS_BLOCK_SIZE) {
        error_setg(errp, "the size of a zoned drive must be a multiple of %d "
                   "bytes", UFS_BLOCK_SIZE);
        return false;
    }

    conf.logical_block_size = UFS_BLOCK_SIZE;
    conf.physical_block_size = UFS_BLOCK_SIZE;
    if (!blkconf_check_zoned_geometry(&conf, errp)) {
        return false;
    }

    zone_size /= UFS_BLOCK_SIZE;
    nr_zones = DIV_ROUND_UP(nr_blocks, zone_size);

    /* The REPORT ZONES zone list length must fit in 32 bits */
    if (nr_zones > (UINT32_MAX - REPORT_ZONES_HEADER_SIZE) /
                   REPORT_ZONES_DESC_SIZE) {
        error_setg(errp, "the drive has too many zones (%" PRIu64 ")",
                   nr_zones);
        return false;
    }

    nr_seq = ufs_lu_check_zones(blk, nr_zones, errp);
    if (nr_seq < 0) {
        return false;
    }

    /*
     * ZBC cannot report an active zone limit, so the guest could not keep
     * to one. A limit of at least the number of sequential zones never
     * binds.
     */
    if (blk_get_max_active_zones(blk) &&
        blk_get_max_active_zones(blk) < nr_seq) {
        error_setg(errp, "the drive has an active zone limit (%" PRIu32
                   ") below its %" PRId64 " sequential zones, which ZBC "
                   "cannot report", blk_get_max_active_zones(blk), nr_seq);
        error_append_hint(errp, "Use no active zone limit, or one of at "
                          "least %" PRId64 ".\n", nr_seq);
        return false;
    }

    lu->zone_size = zone_size;
    lu->nr_zones = nr_zones;
    lu->zoned = true;
    return true;
}

static void ufs_init_lu(UfsLu *lu)
{
    BlockBackend *blk = lu->conf.blk;
    int64_t brdv_len = blk_getlength(blk);

    memset(&lu->unit_desc, 0, sizeof(lu->unit_desc));
    lu->zoned = false;
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
    UfsHc *u = UFS_BUS(s)->hc;
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
    if (!ufs_lu_init_zoned(lu, errp)) {
        return;
    }

    if (!ufs_add_lu(u, lu, errp)) {
        return;
    }

    ufs_init_scsi_device(lu, blk, errp);
}

static void ufs_lu_unrealize(DeviceState *dev)
{
    UfsLu *lu = DO_UPCAST(UfsLu, qdev, dev);

    lu->scsi_dev = NULL;
}

static void ufs_lu_class_init(ObjectClass *oc, const void *data)
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
