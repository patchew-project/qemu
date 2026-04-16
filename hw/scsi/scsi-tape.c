/*
 * SCSI Tape Device emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Rafet Taskin <rafettaskindev@gmail.com>
 *
 * SCSI Tape device emulation.
 *
 */

#include "qemu/hw-version.h"
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/memalign.h"
#include "qemu/cutils.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "system/block-backend.h"
#include "hw/block/block.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "qom/object.h"

#define SCSI_MAX_INQUIRY_LEN    256

#define TYPE_SCSI_TAPE "scsi-tape"

OBJECT_DECLARE_SIMPLE_TYPE(SCSITapeState, SCSI_TAPE)

typedef struct SCSITapeReq {
    SCSIRequest req;

    uint32_t buflen;
    struct iovec iov;

    QEMUIOVector qiov;
} SCSITapeReq;

struct SCSITapeState {
    SCSIDevice qdev;

    char *vendor;
    char *product;
    char *version;
    char *serial;
};

static void scsi_free_request(SCSIRequest *req)
{
    SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);

    qemu_vfree(r->iov.iov_base);
}

static uint8_t *scsi_get_buf(SCSIRequest *req)
{
    SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);

    return (uint8_t *)r->iov.iov_base;
}

static void scsi_init_iovec(SCSITapeReq *r, size_t size)
{
    if (!r->iov.iov_base) {
        r->buflen = size;
        r->iov.iov_base = qemu_memalign(qemu_real_host_page_size(), size);
    }

    r->iov.iov_len = size;
    qemu_iovec_init_external(&r->qiov, &r->iov, 1);
}

static void scsi_check_condition(SCSITapeReq *r, SCSISense sense)
{
    scsi_req_build_sense(&r->req, sense);
    scsi_req_complete(&r->req, CHECK_CONDITION);
}

static int scsi_disk_emulate_vpd_page(SCSIRequest *req, uint8_t *outbuf)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, req->dev);
    uint8_t page_code = req->cmd.buf[2];

    outbuf[0] = TYPE_TAPE;
    outbuf[1] = page_code;
    outbuf[2] = 0x00;
    outbuf[3] = 0x00;

    switch (page_code) {
    case 0x00: /* Supported VPD pages */
        outbuf[4] = 0x00;   /* page 0x00 (this page) */
        if (s->serial) {
            outbuf[5] = 0x80;   /* page 0x80 (serial number) */
            outbuf[3] = 2;      /* page data length */
            return 6;
        }
        outbuf[3] = 1;
        return 5;

    case 0x80: /* Unit Serial Number */
        if (!s->serial) {
            return -1;   /* not supported, caller sends INVALID_FIELD */
        }
        {
            int l = strlen(s->serial);
            if (l > 36) {
                l = 36;
            }
            outbuf[3] = l;
            memcpy(&outbuf[4], s->serial, l);
            return 4 + l;
        }

    default:
        return -1;  /* unsupported VPD page */
    }
}

static int scsi_tape_emulate_inquiry(SCSIRequest *req, uint8_t *outbuf)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, req->dev);
    int buflen;

    if (req->cmd.buf[1] & 0x1) {
        return scsi_disk_emulate_vpd_page(req, outbuf);
    }

    /* Standard INQUIRY, not a VPD request */
    if (req->cmd.buf[2] != 0) {
        return -1;
    }

    /* PAGE_CODE == 0 */
    buflen = req->cmd.xfer;
    if (buflen > SCSI_MAX_INQUIRY_LEN) {
        buflen = SCSI_MAX_INQUIRY_LEN;
    }

    outbuf[0] = TYPE_TAPE;      /* 0x01 = Tape */
    outbuf[1] = 0x80;           /* Always removable */
    outbuf[2] = 0x05;           /* SPC-3 */
    outbuf[3] = 0x02 | 0x10;    /* Format 2, HiSup */

    if (buflen > 36) {
        outbuf[4] = buflen - 5;
    } else {
        outbuf[4] = 36 - 5;
    }

    outbuf[7] = 0x10 | (req->bus->info->tcq ? 0x02 : 0);

    strpadcpy((char *)&outbuf[16], 16, s->product, ' ');
    strpadcpy((char *)&outbuf[8], 8, s->vendor, ' ');

    memset(&outbuf[32], 0, 4);
    memcpy(&outbuf[32], s->version, MIN(4, strlen(s->version)));

    return buflen;
}

/*
 * > 0 - number of bytes from device to host
 * < 0 - number of bytes from host to device
 * = 0 - no data transfer, already completed
 */
static int32_t scsi_tape_emulate_command(SCSIRequest *req, uint8_t *buf)
{
    SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);
    uint8_t *outbuf;
    int32_t len = 0;
    uint8_t command = buf[0];

    if (req->cmd.xfer > 65536) {
        scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
        return 0;
    }
    r->buflen = MAX(4096, req->cmd.xfer);
    scsi_init_iovec(r, r->buflen);

    outbuf = r->iov.iov_base;
    memset(outbuf, 0, r->buflen);

    switch (command) {
    case INQUIRY:
        len = scsi_tape_emulate_inquiry(req, outbuf);
        if (len < 0) {
            scsi_check_condition(r, SENSE_CODE(INVALID_FIELD));
            return 0;
        }
        break;

    case TEST_UNIT_READY:
        if (!blk_is_available(req->dev->conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
            return 0;
        }

        /* TODO: check if tape is really ready */
        scsi_req_complete(&r->req, GOOD);
        return 0;
    case REQUEST_SENSE:
        /* TODO: returning NO SENSE for now handle it later. */
        /* 0x0: 0x70 = current errors, fixed format */
        /* 0x2: sense key (0x00 = NO SENSE) */
        /* 0x7: additional sense length */
        {
            int sense_len = MIN(buf[4], 18);
            memset(outbuf, 0, sense_len);
            outbuf[0] = 0x70;
            outbuf[2] = 0x00;
            outbuf[7] = 10;
            len = sense_len;
        }
        break;

    case REWIND:
        if (!blk_is_available(req->dev->conf.blk)) {
            scsi_check_condition(r, SENSE_CODE(NO_MEDIUM));
            return 0;
        }

        /* TODO: reset tape position to beginning of SIMH image */
        scsi_req_complete(&r->req, GOOD);
        return 0;

    /* Add rest later */
    default:
        scsi_check_condition(r, SENSE_CODE(INVALID_OPCODE));
        return 0;
    }

    r->iov.iov_len = len;
    return len;
}


/* TODO */
static void scsi_tape_emulate_read_data(SCSIRequest *req)
{
    SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);
    int buflen = r->iov.iov_len;

    if (buflen) {
        r->iov.iov_len = 0;
        scsi_req_data(&r->req, buflen);
        return;
    }

    scsi_req_complete(&r->req, GOOD);
}

/* TODO: not implemented yet */
static void scsi_tape_emulate_write_data(SCSIRequest *req)
{
    scsi_req_complete(req, GOOD);
}

static const SCSIReqOps scsi_tape_reqops = {
    .size         = sizeof(SCSITapeReq),
    .free_req     = scsi_free_request,
    .send_command = scsi_tape_emulate_command,
    .read_data    = scsi_tape_emulate_read_data,
    .write_data   = scsi_tape_emulate_write_data,
    .get_buf      = scsi_get_buf,
};

static SCSIRequest *scsi_tape_new_request(SCSIDevice *d, uint32_t tag,
                                          uint32_t lun, uint8_t *buf,
                                          void *hba_private)
{
    return scsi_req_alloc(&scsi_tape_reqops, d, tag, lun, hba_private);
}

static void scsi_tape_realize(SCSIDevice *dev, Error **errp)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, dev);

    dev->type = TYPE_TAPE;

    if (!s->qdev.conf.blk) {
        error_setg(errp, "Drive property not set");
        return;
    }

    if (!blk_is_inserted(s->qdev.conf.blk)) {
        error_setg(errp, "Device needs media, but drive is empty");
        return;
    }

    if (!s->vendor) {
        s->vendor = g_strdup("QEMU");
    }
    if (!s->product) {
        s->product = g_strdup("QEMU TAPE");
    }
    if (!s->version) {
        s->version = g_strdup(QEMU_HW_VERSION);
    }
}

static Property scsi_tape_properties[] = {
    DEFINE_PROP_DRIVE("drive", SCSITapeState, qdev.conf.blk),
    DEFINE_PROP_STRING("vendor", SCSITapeState, vendor),
    DEFINE_PROP_STRING("product", SCSITapeState, product),
    DEFINE_PROP_STRING("ver", SCSITapeState, version),
    DEFINE_PROP_STRING("serial", SCSITapeState, serial),
};

static void scsi_tape_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize   = scsi_tape_realize;
    sc->alloc_req = scsi_tape_new_request;
    dc->desc      = "SCSI Tape";
    device_class_set_props(dc, scsi_tape_properties);
}

static const TypeInfo scsi_tape_info = {
    .name          = TYPE_SCSI_TAPE,
    .parent        = TYPE_SCSI_DEVICE,
    .instance_size = sizeof(SCSITapeState),
    .class_init    = scsi_tape_class_initfn,
};

static void scsi_tape_register_types(void)
{
    type_register_static(&scsi_tape_info);
}

type_init(scsi_tape_register_types)
