/*
 *  SCSI Tape Device Emulation
 *
 *  Copyright (c) Emmanuel Ugwu <emmanuelugwu121@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/scsi/scsi.h"
#include "qemu/hw-version.h"
#include "qemu/memalign.h"
#include "scsi/constants.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "system/block-backend.h"
#include "qemu/cutils.h"
#include "qom/object.h"



#define MAX_SERIAL_LEN           36
#define MAX_SERIAL_LEN_FOR_DEVID 20
#define TYPE_SCSI_TAPE_BASE      "scsi-tape"

OBJECT_DECLARE_TYPE(SCSITapeState, SCSITapeClass, SCSI_TAPE_BASE)

typedef struct SCSITapeClass {
    SCSIDeviceClass parent_class;
} SCSITapeClass;


typedef struct SCSITapeReq {
    SCSIRequest req;
    struct iovec iov;
    uint32_t buflen;
    QEMUIOVector qiov;
    BlockAcctCookie acct;
} SCSITapeReq;

typedef struct SCSITapeState {
    SCSIDevice qdev;
    uint32_t position;
    bool at_filemark;
    bool eof;
    bool bot;
    bool eot;
    char *vendor;
    char *version;
    char *serial;
    char *product;
    char *device_id;
} SCSITapeState;



/*
 * scsi_free_request() will be enabled once scsi_init_iovec() is
 * implemented. It frees the buffer allocated by qemu_memalign().
 * static void scsi_free_request(SCSIRequest *req)
 * {
 *     SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);
 *     static void scsi_free_request(SCSIRequest *req)
 * {
 *     SCSITapeReq *r = DO_UPCAST(SCSITapeReq, req, req);
 *     qemu_vfree(r->iov.iov_base);
 * }
 */

/* TODO: finish implementation for testing realize function for now */
static int32_t scsi_tape_send_command(SCSIRequest *req, uint8_t *buf)
{
    scsi_req_complete(req, GOOD);
    return 0;
}

/*TODO: Add elements when commands are implemented*/
static const SCSIReqOps *const scsi_tape_reqops_dispatch[256] = {

};
static const SCSIReqOps scsi_tape_emulate_reqops = {
    .size         = sizeof(SCSITapeReq),
    /* required to test if tape is detected by QEMU*/
    .send_command = scsi_tape_send_command
};


static SCSIRequest *scsi_tape_new_request(SCSIDevice *dev, uint32_t tag,
                                          uint32_t lun, uint8_t *buf,
                                          void *hba_private)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, dev);
    SCSIRequest *req;
    const SCSIReqOps *ops = scsi_tape_reqops_dispatch[buf[0]];
    if (!ops) {
        ops = &scsi_tape_emulate_reqops;
    }
    req = scsi_req_alloc(ops, &s->qdev, tag, lun, hba_private);

    return req;
}

static void scsi_tape_init(SCSITapeState *s)
{
    s->position = 0;
    s->at_filemark = false;
    s->eof = false;
    s->bot = true;
    s->eot = false;
}



static void scsi_tape_realize(SCSIDevice *dev, Error **errp)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, dev);

    s->qdev.type = TYPE_TAPE;

    if (!s->qdev.conf.blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!blk_attach_dev(s->qdev.conf.blk, &dev->qdev)) {
        error_setg(errp, "failed to attach block backend");
        return;
    }

    if (!s->vendor) {
        s->vendor = g_strdup("QEMU TAPE");
    }
    if (!s->version) {
        s->version = g_strdup(QEMU_HW_VERSION);
    }

    if (s->serial && strlen(s->serial) > MAX_SERIAL_LEN) {
        error_setg(errp, "The serial number can't be longer than %d characters",
                   MAX_SERIAL_LEN);
        goto fail;
    }

    if (!s->device_id) {
        if (s->serial) {
            if (strlen(s->serial) > MAX_SERIAL_LEN_FOR_DEVID) {
                error_setg(errp, "The serial number can't be longer than %d "
                           "characters when it is also used as the default for "
                           "device_id", MAX_SERIAL_LEN_FOR_DEVID);
                goto fail;
            }
            s->device_id = g_strdup(s->serial);
        }
    }

    scsi_tape_init(s);
    return;

fail:
    g_free(s->vendor);
    s->vendor = NULL;
    g_free(s->version);
    s->version = NULL;
    blk_detach_dev(s->qdev.conf.blk, &dev->qdev);
}

static void scsi_tape_unrealize(SCSIDevice *dev)
{
    SCSITapeState *s = DO_UPCAST(SCSITapeState, qdev, dev);

    g_free(s->vendor);
    g_free(s->serial);
    g_free(s->product);
    g_free(s->device_id);
    g_free(s->version);
    if (s->qdev.conf.blk) {
        blk_detach_dev(s->qdev.conf.blk, &dev->qdev);
    }
}

#define DEFINE_SCSI_TAPE_PROPERTIES()                                   \
    DEFINE_PROP_DRIVE_IOTHREAD("drive", SCSITapeState, qdev.conf.blk),  \
    DEFINE_BLOCK_PROPERTIES_BASE(SCSITapeState, qdev.conf),             \
    DEFINE_BLOCK_ERROR_PROPERTIES(SCSITapeState, qdev.conf),            \
    DEFINE_PROP_STRING("ver", SCSITapeState, version),                  \
    DEFINE_PROP_STRING("serial", SCSITapeState, serial),                \
    DEFINE_PROP_STRING("vendor", SCSITapeState, vendor),                \
    DEFINE_PROP_STRING("product", SCSITapeState, product),              \
    DEFINE_PROP_STRING("device_id", SCSITapeState, device_id)

static const Property scsi_tape_properties[] = {
        DEFINE_SCSI_TAPE_PROPERTIES(),
};
static void scsi_tape_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCSIDeviceClass *sc = SCSI_DEVICE_CLASS(klass);

    sc->realize   = scsi_tape_realize;
    sc->unrealize = scsi_tape_unrealize;
    sc->alloc_req = scsi_tape_new_request;
    dc->desc = "virtual SCSI tape";
    device_class_set_props(dc, scsi_tape_properties);
}
static const TypeInfo scsi_tape_info = {
    .name          = TYPE_SCSI_TAPE_BASE,
    .parent        = TYPE_SCSI_DEVICE,
    .instance_size = sizeof(SCSITapeState),
    .class_size    = sizeof(SCSITapeClass),
    .class_init    = scsi_tape_class_init,
};

static void scsi_tape_register_types(void)
{
    type_register_static(&scsi_tape_info);
}

type_init(scsi_tape_register_types)

