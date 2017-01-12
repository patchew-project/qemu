/*
 * s390 CCW Assignment Support
 *
 * Copyright 2017 IBM Corp
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or (at your option) any later version. See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "libgen.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"
#include "s390-ccw.h"

static void s390_ccw_realize(S390CCWDevice *cdev, Error **errp)
{
    CcwDevice *ccw_dev = CCW_DEVICE(cdev);
    DeviceState *parent = DEVICE(ccw_dev);
    BusState *qbus;
    VirtualCssBus *cbus;
    SubchDev *sch;
    CssDevId bus_id;
    int ret;

    if (!cdev->hostid.valid) {
        error_setg(errp, "Invalid hostid");
        return;
    }

    qbus = qdev_get_parent_bus(parent);
    cbus = VIRTUAL_CSS_BUS(qbus);
    if (ccw_dev->bus_id.valid) {
        bus_id = ccw_dev->bus_id;

        if (bus_id.cssid == VIRTUAL_CSSID) {
            error_setg(errp, "Bad guest id: VIRTUAL_CSSID %x forbidden",
                       bus_id.cssid);
            return;
        }

        if (!cbus->map_vir_css) {
            ret = css_create_css_image(bus_id.cssid, false);
            if (ret == -EINVAL) {
                error_setg(errp, "Invalid cssid: %x", bus_id.cssid);
                return;
            }
        }
    } else {
        bus_id = cdev->hostid;
    }

    if (cbus->map_vir_css) {
        bus_id.cssid = VIRTUAL_CSSID;
    }

    sch = css_create_sch(bus_id, errp);
    if (!sch) {
        return;
    }

    sch->driver_data = cdev;

    ret = css_sch_build_schib(sch, &cdev->hostid);
    if (ret) {
        error_setg(errp, "%s: Failed to build initial schib: %d",
                   __func__, ret);
        css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
        g_free(sch);
        return;
    }
    css_generate_sch_crws(sch->cssid, sch->ssid, sch->schid,
                          parent->hotplugged, 1);

    ccw_dev->sch = sch;
    return;
}

static void s390_ccw_unrealize(S390CCWDevice *cdev, Error **errp)
{
    CcwDevice *ccw_dev = CCW_DEVICE(cdev);
    SubchDev *sch = ccw_dev->sch;

    if (sch) {
        css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
        g_free(sch);
        ccw_dev->sch = NULL;
    }
}

static void s390_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_CLASS(klass);

    dc->bus_type = TYPE_VIRTUAL_CSS_BUS;
    cdc->realize = s390_ccw_realize;
    cdc->unrealize = s390_ccw_unrealize;
}

static const TypeInfo s390_ccw_info = {
    .name          = TYPE_S390_CCW,
    .parent        = TYPE_CCW_DEVICE,
    .instance_size = sizeof(S390CCWDevice),
    .abstract      = true,
    .class_size    = sizeof(S390CCWDeviceClass),
    .class_init    = s390_ccw_class_init,
};

static void register_s390_ccw_type(void)
{
    type_register_static(&s390_ccw_info);
}

type_init(register_s390_ccw_type)
