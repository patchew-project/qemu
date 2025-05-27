/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt KVM support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_ipi.h"
#include "system/kvm.h"
#include "target/loongarch/cpu.h"

static void kvm_ipi_access_regs(int fd, uint64_t addr,
                                uint32_t *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_IPI_GRP_REGS,
                          addr, val, write, &error_abort);
}

static void kvm_loongarch_ipi_save_load_regs(void *opaque, bool write)
{
    LoongsonIPICommonState *ipi = (LoongsonIPICommonState *)opaque;
    LoongarchIPIState *lis = LOONGARCH_IPI(opaque);
    IPICore *cpu;
    uint64_t attr;
    int cpu_id = 0;
    int fd = lis->dev_fd;

    for (cpu_id = 0; cpu_id < ipi->num_cpu; cpu_id++) {
        cpu = &ipi->cpu[cpu_id];
        attr = (cpu_id << 16) | CORE_STATUS_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->status, write);

        attr = (cpu_id << 16) | CORE_EN_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->en, write);

        attr = (cpu_id << 16) | CORE_SET_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->set, write);

        attr = (cpu_id << 16) | CORE_CLEAR_OFF;
        kvm_ipi_access_regs(fd, attr, &cpu->clear, write);

        attr = (cpu_id << 16) | CORE_BUF_20;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[0], write);

        attr = (cpu_id << 16) | CORE_BUF_28;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[2], write);

        attr = (cpu_id << 16) | CORE_BUF_30;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[4], write);

        attr = (cpu_id << 16) | CORE_BUF_38;
        kvm_ipi_access_regs(fd, attr, &cpu->buf[6], write);
    }
}

int kvm_loongarch_ipi_pre_save(void *opaque)
{
    kvm_loongarch_ipi_save_load_regs(opaque, false);
    return 0;
}

int kvm_loongarch_ipi_post_load(void *opaque, int version_id)
{
    kvm_loongarch_ipi_save_load_regs(opaque, true);
    return 0;
}

void kvm_loongarch_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(dev);
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_IPI, false);
    if (ret < 0) {
        fprintf(stderr, "IPI KVM_CREATE_DEVICE failed: %s\n",
                strerror(-ret));
        abort();
    }

    lis->dev_fd = ret;
}
