/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *            Halil Pasic <pasic@linux.ibm.com>
 *            Pierre Morel <pmorel@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <linux/vfio.h>
#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/ap-device.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "cpu.h"
#include "kvm_s390x.h"
#include "sysemu/sysemu.h"
#include "hw/s390x/ap-bridge.h"
#include "exec/address-spaces.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/css.h"

#define VFIO_AP_DEVICE_TYPE      "vfio-ap"

typedef struct VFIOAPDevice {
    APDevice apdev;
    VFIODevice vdev;
} VFIOAPDevice;

static uint32_t ap_pqap_clear_irq(VFIODevice *vdev, APQueue *apq)
{
    struct vfio_ap_aqic param;
    uint32_t retval;

    param.apqn = apq->apqn;
    param.isc = apq->isc;
    param.argsz = sizeof(param);

    retval = ioctl(vdev->fd, VFIO_AP_CLEAR_IRQ, &param);
    switch (retval) {
    case 0:    /* Fall through and return the instruction status */
        release_indicator(&apq->routes.adapter, apq->nib);
    case -EIO: /* The case the PQAP instruction failed with status */
        return  param.status;
    default:   /* The case the ioctl call failed without isuing instruction */
        break;
    }
    return ap_reg_set_status(AP_RC_INVALID_ADDR);
}

static uint32_t ap_pqap_set_irq(VFIODevice *vdev, APQueue *apq, uint64_t g_nib)
{
    struct vfio_ap_aqic param;
    uint32_t retval;
    uint32_t id;

    id = css_get_adapter_id(CSS_IO_ADAPTER_AP, apq->isc);
    if (id == -1) {
        return ap_reg_set_status(AP_RC_INVALID_ADDR);
    }
    apq->routes.adapter.adapter_id = id;
    apq->nib = get_indicator(ldq_p(&g_nib), 8);

    retval = map_indicator(&apq->routes.adapter, apq->nib);
    if (retval) {
        return ap_reg_set_status(AP_RC_INVALID_ADDR);
    }

    param.apqn = apq->apqn;
    param.isc = apq->isc;
    param.nib = g_nib;
    param.adapter_id = id;
    param.argsz = sizeof(param);

    retval =  ioctl(vdev->fd, VFIO_AP_SET_IRQ, &param);
    switch (retval) {
    case -EIO: /* The case the PQAP instruction failed with status */
        release_indicator(&apq->routes.adapter, apq->nib);
    case 0:    /* Fall through and return the instruction status */
        return  param.status;
    default:   /* The case the ioctl call failed without isuing instruction */
        break;
    }
    release_indicator(&apq->routes.adapter, apq->nib);
    return ap_reg_set_status(AP_RC_INVALID_ADDR);
}

static int ap_pqap_aqic(CPUS390XState *env)
{
    uint64_t g_nib = env->regs[2];
    uint8_t apid = ap_reg_get_apid(env->regs[0]);
    uint8_t apqi = ap_reg_get_apqi(env->regs[0]);
    uint32_t retval;
    APDevice *ap = s390_get_ap();
    VFIODevice *vdev;
    VFIOAPDevice *ap_vdev;
    APQueue *apq;

    ap_vdev = DO_UPCAST(VFIOAPDevice, apdev, ap);
    vdev = &ap_vdev->vdev;
    apq = &ap->card[apid].queue[apqi];
    apq->isc = ap_reg_get_isc(env->regs[1]);
    apq->apqn = (apid << 8) | apqi;

    if (ap_reg_get_ir(env->regs[1])) {
        retval = ap_pqap_set_irq(vdev, apq, g_nib);
    } else {
        retval = ap_pqap_clear_irq(vdev, apq);
    }

    env->regs[1] = retval;
    if (retval & AP_STATUS_RC_MASK) {
        return 3;
    }

    return 0;
}

/*
 * ap_pqap
 * @env: environment pointing to registers
 * return value: Code Condition
 */
int ap_pqap(CPUS390XState *env)
{
    int cc = 0;

    switch (ap_reg_get_fc(env->regs[0])) {
    case AQIC:
        if (!s390_has_feat(S390_FEAT_AP_QUEUE_INTERRUPT_CONTROL)) {
            return -PGM_OPERATION;
        }
        cc = ap_pqap_aqic(env);
        break;
    default:
        return -PGM_OPERATION;
    }
    return cc;
}

static void vfio_ap_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operations for
 * vfio-ap device now.
 */
struct VFIODeviceOps vfio_ap_ops = {
    .vfio_compute_needs_reset = vfio_ap_compute_needs_reset,
};

static void vfio_ap_put_device(VFIOAPDevice *vapdev)
{
    g_free(vapdev->vdev.name);
    vfio_put_base_device(&vapdev->vdev);
}

static VFIOGroup *vfio_ap_get_group(VFIOAPDevice *vapdev, Error **errp)
{
    GError *gerror = NULL;
    char *symlink, *group_path;
    int groupid;

    symlink = g_strdup_printf("%s/iommu_group", vapdev->vdev.sysfsdev);
    group_path = g_file_read_link(symlink, &gerror);
    g_free(symlink);

    if (!group_path) {
        error_setg(errp, "%s: no iommu_group found for %s: %s",
                   VFIO_AP_DEVICE_TYPE, vapdev->vdev.sysfsdev, gerror->message);
        return NULL;
    }

    if (sscanf(basename(group_path), "%d", &groupid) != 1) {
        error_setg(errp, "vfio: failed to read %s", group_path);
        g_free(group_path);
        return NULL;
    }

    g_free(group_path);

    return vfio_get_group(groupid, &address_space_memory, errp);
}

static void vfio_ap_realize(DeviceState *dev, Error **errp)
{
    int ret;
    char *mdevid;
    Error *local_err = NULL;
    VFIOGroup *vfio_group;
    APDevice *apdev = DO_UPCAST(APDevice, parent_obj, dev);
    VFIOAPDevice *vapdev = DO_UPCAST(VFIOAPDevice, apdev, apdev);

    vfio_group = vfio_ap_get_group(vapdev, &local_err);
    if (!vfio_group) {
        goto out_err;
    }

    vapdev->vdev.ops = &vfio_ap_ops;
    vapdev->vdev.type = VFIO_DEVICE_TYPE_AP;
    mdevid = basename(vapdev->vdev.sysfsdev);
    vapdev->vdev.name = g_strdup_printf("%s", mdevid);
    vapdev->vdev.dev = dev;

    ret = vfio_get_device(vfio_group, mdevid, &vapdev->vdev, &local_err);
    if (ret) {
        goto out_get_dev_err;
    }

    css_register_io_adapters(CSS_IO_ADAPTER_AP, true, false,
                             0, &error_abort);
    return;

out_get_dev_err:
    vfio_ap_put_device(vapdev);
    vfio_put_group(vfio_group);
out_err:
    error_propagate(errp, local_err);
}

static void vfio_ap_unrealize(DeviceState *dev, Error **errp)
{
    APDevice *apdev = DO_UPCAST(APDevice, parent_obj, dev);
    VFIOAPDevice *vapdev = DO_UPCAST(VFIOAPDevice, apdev, apdev);
    VFIOGroup *group = vapdev->vdev.group;

    vfio_ap_put_device(vapdev);
    vfio_put_group(group);
}

static Property vfio_ap_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOAPDevice, vdev.sysfsdev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_ap_reset(DeviceState *dev)
{
    int ret;
    APDevice *apdev = DO_UPCAST(APDevice, parent_obj, dev);
    VFIOAPDevice *vapdev = DO_UPCAST(VFIOAPDevice, apdev, apdev);

    ret = ioctl(vapdev->vdev.fd, VFIO_DEVICE_RESET);
    if (ret) {
        error_report("%s: failed to reset %s device: %s", __func__,
                     vapdev->vdev.name, strerror(errno));
    }
}

static const VMStateDescription vfio_ap_vmstate = {
    .name = VFIO_AP_DEVICE_TYPE,
    .unmigratable = 1,
};

static void vfio_ap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = vfio_ap_properties;
    dc->vmsd = &vfio_ap_vmstate;
    dc->desc = "VFIO-based AP device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ap_realize;
    dc->unrealize = vfio_ap_unrealize;
    dc->hotpluggable = false;
    dc->reset = vfio_ap_reset;
}

static const TypeInfo vfio_ap_info = {
    .name = VFIO_AP_DEVICE_TYPE,
    .parent = AP_DEVICE_TYPE,
    .instance_size = sizeof(VFIOAPDevice),
    .class_init = vfio_ap_class_init,
};

static void vfio_ap_type_init(void)
{
    type_register_static(&vfio_ap_info);
}

type_init(vfio_ap_type_init)
