/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch kvm ipi interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qemu/typedefs.h"
#include "hw/intc/loongson_ipi.h"
#include "hw/sysbus.h"
#include "linux/kvm.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "sysemu/kvm.h"

#define IPI_DEV_FD_UNDEF        -1

static void kvm_ipi_access_regs(int fd, uint64_t addr,
                                uint32_t *val, int is_write)
{
        kvm_device_access(fd, KVM_DEV_LOONGARCH_IPI_GRP_REGS,
                          addr, val, is_write, &error_abort);
}

static int kvm_loongarch_ipi_pre_save(void *opaque)
{
    KVMLoongArchIPI *ipi = (KVMLoongArchIPI *)opaque;
    KVMLoongArchIPIClass *ipi_class = KVM_LOONGARCH_IPI_GET_CLASS(ipi);
    IPICore *cpu;
    uint64_t attr;
    int cpu_id = 0;
    int fd = ipi_class->dev_fd;

    for (cpu_id = 0; cpu_id < ipi->num_cpu; cpu_id++) {
        cpu = &ipi->cpu[cpu_id];
        attr = (cpu_id << 16) | CORE_STATUS_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->status, false);

        attr = (cpu_id << 16) | CORE_EN_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->en, false);

        attr = (cpu_id << 16) | CORE_SET_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->set, false);

        attr = (cpu_id << 16) | CORE_CLEAR_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->clear, false);

        attr = (cpu_id << 16) | CORE_BUF_20;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[0], false);

        attr = (cpu_id << 16) | CORE_BUF_28;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[2], false);

        attr = (cpu_id << 16) | CORE_BUF_30;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[4], false);

        attr = (cpu_id << 16) | CORE_BUF_38;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[6], false);
    }

    return 0;
}

static int kvm_loongarch_ipi_post_load(void *opaque, int version_id)
{
    KVMLoongArchIPI *ipi = (KVMLoongArchIPI *)opaque;
    KVMLoongArchIPIClass *ipi_class = KVM_LOONGARCH_IPI_GET_CLASS(ipi);
    IPICore *cpu;
    uint64_t attr;
    int cpu_id = 0;
    int fd = ipi_class->dev_fd;

    for (cpu_id = 0; cpu_id < ipi->num_cpu; cpu_id++) {
        cpu = &ipi->cpu[cpu_id];
        attr = (cpu_id << 16) | CORE_STATUS_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->status, true);

        attr = (cpu_id << 16) | CORE_EN_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->en, true);

        attr = (cpu_id << 16) | CORE_SET_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->set, true);

        attr = (cpu_id << 16) | CORE_CLEAR_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->clear, true);

        attr = (cpu_id << 16) | CORE_BUF_20;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[0], true);

        attr = (cpu_id << 16) | CORE_BUF_28;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[2], true);

        attr = (cpu_id << 16) | CORE_BUF_30;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[4], true);

        attr = (cpu_id << 16) | CORE_BUF_38;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[6], true);
    }

    return 0;
}

static void kvm_loongarch_ipi_realize(DeviceState *dev, Error **errp)
{
    KVMLoongArchIPI *ipi = KVM_LOONGARCH_IPI(dev);
    KVMLoongArchIPIClass *ipi_class = KVM_LOONGARCH_IPI_GET_CLASS(dev);
    struct kvm_create_device cd = {0};
    Error *err = NULL;
    int ret;

    if (ipi->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    ipi_class->parent_realize(dev, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    ipi->cpu = g_new0(IPICore, ipi->num_cpu);
    if (ipi->cpu == NULL) {
        error_setg(errp, "Memory allocation for ExtIOICore faile");
        return;
    }

    if (!ipi_class->is_created) {
        cd.type = KVM_DEV_TYPE_LA_IPI;
        ret = kvm_vm_ioctl(kvm_state, KVM_CREATE_DEVICE, &cd);
        if (ret < 0) {
            error_setg_errno(errp, errno, "Creating the KVM device failed");
            return;
        }
        ipi_class->is_created = true;
        ipi_class->dev_fd = cd.fd;
        fprintf(stdout, "Create LoongArch IPI irqchip in KVM done!\n");
    }

    assert(ipi_class->dev_fd != IPI_DEV_FD_UNDEF);
}

static Property kvm_loongarch_ipi_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", KVMLoongArchIPI, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription vmstate_kvm_ipi_core = {
    .name = "kvm-ipi-single",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(status, IPICore),
        VMSTATE_UINT32(en, IPICore),
        VMSTATE_UINT32(set, IPICore),
        VMSTATE_UINT32(clear, IPICore),
        VMSTATE_UINT32_ARRAY(buf, IPICore, 8),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_kvm_loongarch_ipi = {
    .name = TYPE_KVM_LOONGARCH_IPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = kvm_loongarch_ipi_pre_save,
    .post_load = kvm_loongarch_ipi_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(cpu, KVMLoongArchIPI, num_cpu,
                         vmstate_kvm_ipi_core, IPICore),

        VMSTATE_END_OF_LIST()
    }
};

static void kvm_loongarch_ipi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    KVMLoongArchIPIClass *ipi_class = KVM_LOONGARCH_IPI_CLASS(oc);

    ipi_class->parent_realize = dc->realize;
    dc->realize = kvm_loongarch_ipi_realize;

    ipi_class->is_created = false;
    ipi_class->dev_fd = IPI_DEV_FD_UNDEF;

    device_class_set_props(dc, kvm_loongarch_ipi_properties);

    dc->vmsd = &vmstate_kvm_loongarch_ipi;
}

static const TypeInfo kvm_loongarch_ipi_info = {
    .name = TYPE_KVM_LOONGARCH_IPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMLoongArchIPI),
    .class_size = sizeof(KVMLoongArchIPIClass),
    .class_init = kvm_loongarch_ipi_class_init,
};

static void kvm_loongarch_ipi_register_types(void)
{
    type_register_static(&kvm_loongarch_ipi_info);
}

type_init(kvm_loongarch_ipi_register_types)
