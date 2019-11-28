/*
 * CCW PING-PONG
 *
 * Copyright 2019 IBM Corp.
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/pong.h"

#define PONG_BUF_SIZE 0x1000
static char buf[PONG_BUF_SIZE];

static int pong_ccw_cb(SubchDev *sch, CCW1 ccw)
{
    int rc = 0;
    static int value;
    int len;

    len = (ccw.count > PONG_BUF_SIZE) ? PONG_BUF_SIZE : ccw.count;
    switch (ccw.cmd_code) {
    case PONG_WRITE:
        rc = ccw_dstream_read_buf(&sch->cds, buf, len);
        value = atol(buf);
        break;
    case PONG_READ:
        sprintf(buf, "%08x", value + 1);
        rc = ccw_dstream_write_buf(&sch->cds, buf, len);
        break;
    default:
        rc = -ENOSYS;
        break;
    }

    sch->curr_status.scsw.count = ccw_dstream_residual_count(&sch->cds);

    if (rc == -EIO) {
        /* I/O error, specific devices generate specific conditions */
        SCHIB *schib = &sch->curr_status;

        sch->curr_status.scsw.dstat = SCSW_DSTAT_UNIT_CHECK;
        sch->sense_data[0] = 0x40;    /* intervention-req */
        schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
        schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                   SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
    }
    return rc;
}

static void pong_ccw_realize(DeviceState *ds, Error **errp)
{
    uint16_t chpid;
    CcwPONGDevice *dev = CCW_PONG(ds);
    CcwDevice *cdev = CCW_DEVICE(ds);
    CCWDeviceClass *cdk = CCW_DEVICE_GET_CLASS(cdev);
    SubchDev *sch;
    Error *err = NULL;

    sch = css_create_sch(cdev->devno, errp);
    if (!sch) {
        return;
    }

    sch->driver_data = dev;
    cdev->sch = sch;
    chpid = css_find_free_chpid(sch->cssid);

    if (chpid > MAX_CHPID) {
        error_setg(&err, "No available chpid to use.");
        goto out_err;
    }

    sch->id.reserved = 0xff;
    sch->id.cu_type = dev->cu_type;
    css_sch_build_virtual_schib(sch, (uint8_t)chpid, CCW_PONG_CHPID_TYPE);
    sch->do_subchannel_work = do_subchannel_work_virtual;
    sch->ccw_cb = pong_ccw_cb;

    cdk->realize(cdev, &err);
    if (err) {
        goto out_err;
    }

    css_reset_sch(sch);
    return;

out_err:
    error_propagate(errp, err);
    css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
    cdev->sch = NULL;
    g_free(sch);
}

static Property pong_ccw_properties[] = {
    DEFINE_PROP_UINT16("cu_type", CcwPONGDevice, cu_type, CCW_PONG_CU_TYPE),
    DEFINE_PROP_END_OF_LIST(),
};

static void pong_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = pong_ccw_properties;
    dc->bus_type = TYPE_VIRTUAL_CSS_BUS;
    dc->realize = pong_ccw_realize;
    dc->hotpluggable = false;
}

static const TypeInfo pong_ccw_info = {
    .name = TYPE_CCW_PONG,
    .parent = TYPE_CCW_DEVICE,
    .instance_size = sizeof(CcwPONGDevice),
    .class_init = pong_ccw_class_init,
    .class_size = sizeof(CcwPONGClass),
};

static void pong_ccw_register(void)
{
    type_register_static(&pong_ccw_info);
}

type_init(pong_ccw_register)
