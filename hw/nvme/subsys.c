/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "nvme.h"

int nvme_subsys_register_ctrl(NvmeSubsystem *subsys, NvmeCtrl *n, Error **errp)
{
    int cntlid, nsid;

    for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
        if (!subsys->ctrls[cntlid]) {
            break;
        }
    }

    if (cntlid == ARRAY_SIZE(subsys->ctrls)) {
        error_setg(errp, "no more free controller id");
        return -1;
    }

    subsys->ctrls[cntlid] = n;

    for (nsid = 1; nsid < ARRAY_SIZE(subsys->namespaces); nsid++) {
        NvmeNamespace *ns = subsys->namespaces[nsid];
        if (ns && (ns->flags & NVME_NS_SHARED)) {
            nvme_attach_ns(n, ns);
        }
    }

    return cntlid;
}

void nvme_subsys_unregister_ctrl(NvmeSubsystem *subsys, NvmeCtrl *n)
{
    subsys->ctrls[n->cntlid] = NULL;
    n->cntlid = -1;
}

static void nvme_subsys_device_setup(NvmeSubsystemDevice *dev)
{
    NvmeSubsystem *subsys = &dev->subsys;
    const char *nqn = dev->params.nqn ?
        dev->params.nqn : dev->parent_obj.id;

    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
             "nqn.2019-08.org.qemu:%s", nqn);
}

static void nvme_subsys_device_realize(DeviceState *dev, Error **errp)
{
    NvmeSubsystemDevice *subsys = NVME_SUBSYSTEM_DEVICE(dev);

    qbus_create_inplace(&subsys->bus, sizeof(NvmeBus), TYPE_NVME_BUS, dev,
                        dev->id);

    nvme_subsys_device_setup(subsys);
}

static Property nvme_subsystem_device_props[] = {
    DEFINE_PROP_STRING("nqn", NvmeSubsystemDevice, params.nqn),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_subsys_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->realize = nvme_subsys_device_realize;
    dc->desc = "Virtual NVMe subsystem";
    dc->hotpluggable = false;

    device_class_set_props(dc, nvme_subsystem_device_props);
}

static const TypeInfo nvme_subsys_device_info = {
    .name = TYPE_NVME_SUBSYSTEM_DEVICE,
    .parent = TYPE_DEVICE,
    .class_init = nvme_subsys_device_class_init,
    .instance_size = sizeof(NvmeSubsystemDevice),
};

static void nvme_subsys_register_types(void)
{
    type_register_static(&nvme_subsys_device_info);
}

type_init(nvme_subsys_register_types)
