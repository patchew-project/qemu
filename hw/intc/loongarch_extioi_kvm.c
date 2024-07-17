/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch kvm extioi interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qemu/typedefs.h"
#include "hw/intc/loongarch_extioi.h"
#include "hw/sysbus.h"
#include "linux/kvm.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"

static void kvm_extioi_access_regs(int fd, uint64_t addr,
                                       void *val, int is_write)
{
        kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS,
                          addr, val, is_write, &error_abort);
}

static int kvm_loongarch_extioi_pre_save(void *opaque)
{
    KVMLoongArchExtIOI *s = (KVMLoongArchExtIOI *)opaque;
    KVMLoongArchExtIOIClass *class = KVM_LOONGARCH_EXTIOI_GET_CLASS(s);
    int fd = class->dev_fd;

    kvm_extioi_access_regs(fd, EXTIOI_NODETYPE_START,
                           (void *)s->nodetype, false);
    kvm_extioi_access_regs(fd, EXTIOI_IPMAP_START, (void *)s->ipmap, false);
    kvm_extioi_access_regs(fd, EXTIOI_ENABLE_START, (void *)s->enable, false);
    kvm_extioi_access_regs(fd, EXTIOI_BOUNCE_START, (void *)s->bounce, false);
    kvm_extioi_access_regs(fd, EXTIOI_ISR_START, (void *)s->isr, false);
    kvm_extioi_access_regs(fd, EXTIOI_COREMAP_START,
                           (void *)s->coremap, false);
    kvm_extioi_access_regs(fd, EXTIOI_SW_COREMAP_FLAG,
                           (void *)s->sw_coremap, false);
    kvm_extioi_access_regs(fd, EXTIOI_COREISR_START,
                           (void *)s->coreisr, false);

    return 0;
}

static int kvm_loongarch_extioi_post_load(void *opaque, int version_id)
{
    KVMLoongArchExtIOI *s = (KVMLoongArchExtIOI *)opaque;
    KVMLoongArchExtIOIClass *class = KVM_LOONGARCH_EXTIOI_GET_CLASS(s);
    int fd = class->dev_fd;

    kvm_extioi_access_regs(fd, EXTIOI_NODETYPE_START,
                           (void *)s->nodetype, true);
    kvm_extioi_access_regs(fd, EXTIOI_IPMAP_START, (void *)s->ipmap, true);
    kvm_extioi_access_regs(fd, EXTIOI_ENABLE_START, (void *)s->enable, true);
    kvm_extioi_access_regs(fd, EXTIOI_BOUNCE_START, (void *)s->bounce, true);
    kvm_extioi_access_regs(fd, EXTIOI_ISR_START, (void *)s->isr, true);
    kvm_extioi_access_regs(fd, EXTIOI_COREMAP_START, (void *)s->coremap, true);
    kvm_extioi_access_regs(fd, EXTIOI_SW_COREMAP_FLAG,
                           (void *)s->sw_coremap, true);
    kvm_extioi_access_regs(fd, EXTIOI_COREISR_START, (void *)s->coreisr, true);

    return 0;
}

static void kvm_loongarch_extioi_realize(DeviceState *dev, Error **errp)
{
    KVMLoongArchExtIOIClass *extioi_class = KVM_LOONGARCH_EXTIOI_GET_CLASS(dev);
    struct kvm_create_device cd = {0};
    Error *err = NULL;
    int ret;

    extioi_class->parent_realize(dev, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!extioi_class->is_created) {
        cd.type = KVM_DEV_TYPE_LA_EXTIOI;
        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd);
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "Creating the KVM extioi device failed");
            return;
        }
        extioi_class->is_created = true;
        extioi_class->dev_fd = cd.fd;
        fprintf(stdout, "Create LoongArch extioi irqchip in KVM done!\n");
    }
}

static const VMStateDescription vmstate_kvm_extioi_core = {
    .name = "kvm-extioi-single",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = kvm_loongarch_extioi_pre_save,
    .post_load = kvm_loongarch_extioi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(nodetype, KVMLoongArchExtIOI,
                             EXTIOI_IRQS_NODETYPE_COUNT / 2),
        VMSTATE_UINT32_ARRAY(bounce, KVMLoongArchExtIOI,
                             EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(isr, KVMLoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_2DARRAY(coreisr, KVMLoongArchExtIOI, EXTIOI_CPUS,
                               EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(enable, KVMLoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(ipmap, KVMLoongArchExtIOI,
                             EXTIOI_IRQS_IPMAP_SIZE / 4),
        VMSTATE_UINT32_ARRAY(coremap, KVMLoongArchExtIOI, EXTIOI_IRQS / 4),
        VMSTATE_UINT8_ARRAY(sw_coremap, KVMLoongArchExtIOI, EXTIOI_IRQS),
        VMSTATE_END_OF_LIST()
    }
};

static void kvm_loongarch_extioi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    KVMLoongArchExtIOIClass *extioi_class = KVM_LOONGARCH_EXTIOI_CLASS(oc);

    extioi_class->parent_realize = dc->realize;
    dc->realize = kvm_loongarch_extioi_realize;
    extioi_class->is_created = false;
    dc->vmsd = &vmstate_kvm_extioi_core;
}

static const TypeInfo kvm_loongarch_extioi_info = {
    .name = TYPE_KVM_LOONGARCH_EXTIOI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMLoongArchExtIOI),
    .class_size = sizeof(KVMLoongArchExtIOIClass),
    .class_init = kvm_loongarch_extioi_class_init,
};

static void kvm_loongarch_extioi_register_types(void)
{
    type_register_static(&kvm_loongarch_extioi_info);
}

type_init(kvm_loongarch_extioi_register_types)
