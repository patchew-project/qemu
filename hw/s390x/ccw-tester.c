#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"
#include "hw/s390x/3270-ccw.h"
#include "exec/address-spaces.h"
#include <error.h>

typedef struct CcwTesterDevice {
    CcwDevice parent_obj;
    uint16_t cu_type;
    uint8_t chpid_type;
    struct {
        uint32_t ring[4];
        unsigned int next;
    } fib;
} CcwTesterDevice;


typedef struct CcwTesterClass {
    CCWDeviceClass parent_class;
    DeviceRealize parent_realize;
} CcwTesterClass;

#define TYPE_CCW_TESTER "ccw-tester"

#define CCW_TESTER(obj) \
     OBJECT_CHECK(CcwTesterDevice, (obj), TYPE_CCW_TESTER)
#define CCW_TESTER_CLASS(klass) \
     OBJECT_CLASS_CHECK(CcwTesterClass, (klass), TYPE_CCW_TESTER)
#define CCW_TESTER_GET_CLASS(obj) \
     OBJECT_GET_CLASS(CcwTesterClass, (obj), TYPE_CCW_TESTER)

#define CCW_CMD_READ 0x01U
#define CCW_CMD_WRITE 0x02U

static unsigned int abs_to_ring(unsigned int i)
{
    return i & 0x3;
}

static int  ccw_tester_write_fib(SubchDev *sch, CCW1 ccw)
{
    CcwTesterDevice *d = sch->driver_data;
    bool is_fib = true;
    uint32_t sum;
    int ret = 0;

    ccw_dstream_init(&sch->cds, &ccw, &sch->orb);
    d->fib.next = 0;
    while (ccw_dstream_avail(&sch->cds) > 0) {
        ret = ccw_dstream_read(&sch->cds,
                               d->fib.ring[abs_to_ring(d->fib.next)]);
        if (ret) {
            error(0, -ret, "fib");
            break;
        }
        if (d->fib.next > 2) {
            sum = (d->fib.ring[abs_to_ring(d->fib.next - 1)]
                  + d->fib.ring[abs_to_ring(d->fib.next - 2)]);
            is_fib = sum ==  d->fib.ring[abs_to_ring(d->fib.next)];
            if (!is_fib) {
                break;
            }
        }
        ++(d->fib.next);
    }
    if (!is_fib) {
        sch->curr_status.scsw.ctrl &= ~SCSW_ACTL_START_PEND;
        sch->curr_status.scsw.ctrl |= SCSW_STCTL_PRIMARY |
                                      SCSW_STCTL_SECONDARY |
                                      SCSW_STCTL_ALERT |
                                      SCSW_STCTL_STATUS_PEND;
        sch->curr_status.scsw.count = ccw_dstream_residual_count(&sch->cds);
        sch->curr_status.scsw.cpa = sch->channel_prog + 8;
        sch->curr_status.scsw.dstat =  SCSW_DSTAT_UNIT_EXCEP;
        return -EIO;
    }
    return ret;
}

static int ccw_tester_ccw_cb_impl(SubchDev *sch, CCW1 ccw)
{
    switch (ccw.cmd_code) {
    case CCW_CMD_READ:
        break;
    case CCW_CMD_WRITE:
        return ccw_tester_write_fib(sch, ccw);
    default:
        return -EINVAL;
    }
    return 0;
}

static void ccw_tester_realize(DeviceState *ds, Error **errp)
{
    uint16_t chpid;
    CcwTesterDevice *dev = CCW_TESTER(ds);
    CcwTesterClass *ctc = CCW_TESTER_GET_CLASS(dev);
    CcwDevice *cdev = CCW_DEVICE(ds);
    BusState *qbus = qdev_get_parent_bus(ds);
    VirtualCssBus *cbus = VIRTUAL_CSS_BUS(qbus);
    SubchDev *sch;
    Error *err = NULL;

    sch = css_create_sch(cdev->devno, true, cbus->squash_mcss, errp);
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
    css_sch_build_virtual_schib(sch, (uint8_t)chpid,
                                dev->chpid_type);
    sch->ccw_cb = ccw_tester_ccw_cb_impl;
    sch->do_subchannel_work = do_subchannel_work_virtual;


    ctc->parent_realize(ds, &err);
    if (err) {
        goto out_err;
    }

    css_generate_sch_crws(sch->cssid, sch->ssid, sch->schid,
                          ds->hotplugged, 1);
    return;

out_err:
    error_propagate(errp, err);
    css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
    cdev->sch = NULL;
    g_free(sch);
}

static Property ccw_tester_properties[] = {
    DEFINE_PROP_UINT16("cu_type", CcwTesterDevice, cu_type,
                        0x3831),
    DEFINE_PROP_UINT8("chpid_type", CcwTesterDevice, chpid_type,
                       0x98),
    DEFINE_PROP_END_OF_LIST(),
};

static void ccw_tester_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CcwTesterClass *ctc = CCW_TESTER_CLASS(klass);

    dc->props = ccw_tester_properties;
    dc->bus_type = TYPE_VIRTUAL_CSS_BUS;
    ctc->parent_realize = dc->realize;
    dc->realize = ccw_tester_realize;
    dc->hotpluggable = false;
}

static const TypeInfo ccw_tester_info = {
    .name = TYPE_CCW_TESTER,
    .parent = TYPE_CCW_DEVICE,
    .instance_size = sizeof(CcwTesterDevice),
    .class_init = ccw_tester_class_init,
    .class_size = sizeof(CcwTesterClass),
};

static void ccw_tester_register(void)
{
    type_register_static(&ccw_tester_info);
}

type_init(ccw_tester_register)
