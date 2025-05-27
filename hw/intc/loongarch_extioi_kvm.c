/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch EXTIOI interrupt kvm support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "hw/intc/loongarch_extioi.h"
#include "linux/kvm.h"
#include "qapi/error.h"
#include "system/kvm.h"

static void kvm_extioi_access_regs(int fd, uint64_t addr,
                                       void *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS,
                      addr, val, write, &error_abort);
}

static void kvm_extioi_access_sw_status(int fd, uint64_t addr,
                                       void *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS,
                      addr, val, write, &error_abort);
}

static void kvm_extioi_save_load_sw_status(void *opaque, bool write)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(opaque);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int fd = les->dev_fd;
    int addr;

    addr = KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_NUM_CPU;
    kvm_extioi_access_sw_status(fd, addr, &lecs->num_cpu, write);

    addr = KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_FEATURE;
    kvm_extioi_access_sw_status(fd, addr, &lecs->features, write);

    addr = KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_STATE;
    kvm_extioi_access_sw_status(fd, addr, &lecs->status, write);
}

static void kvm_extioi_save_load_regs(void *opaque, bool write)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(opaque);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int fd = les->dev_fd;
    int addr, offset, cpuid;

    for (addr = EXTIOI_NODETYPE_START; addr < EXTIOI_NODETYPE_END; addr += 4) {
        offset = (addr - EXTIOI_NODETYPE_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->nodetype[offset], write);
    }

    for (addr = EXTIOI_IPMAP_START; addr < EXTIOI_IPMAP_END; addr += 4) {
        offset = (addr - EXTIOI_IPMAP_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->ipmap[offset], write);
    }

    for (addr = EXTIOI_ENABLE_START; addr < EXTIOI_ENABLE_END; addr += 4) {
        offset = (addr - EXTIOI_ENABLE_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->enable[offset], write);
    }

    for (addr = EXTIOI_BOUNCE_START; addr < EXTIOI_BOUNCE_END; addr += 4) {
        offset = (addr - EXTIOI_BOUNCE_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->bounce[offset], write);
    }

    for (addr = EXTIOI_ISR_START; addr < EXTIOI_ISR_END; addr += 4) {
        offset = (addr - EXTIOI_ISR_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->isr[offset], write);
    }

    for (addr = EXTIOI_COREMAP_START; addr < EXTIOI_COREMAP_END; addr += 4) {
        offset = (addr - EXTIOI_COREMAP_START) / 4;
        kvm_extioi_access_regs(fd, addr, &lecs->coremap[offset], write);
    }

    for (cpuid = 0; cpuid < lecs->num_cpu; cpuid++) {
        for (addr = EXTIOI_COREISR_START;
             addr < EXTIOI_COREISR_END; addr += 4) {
            offset = (addr - EXTIOI_COREISR_START) / 4;
            addr = (cpuid << 16) | addr;
            kvm_extioi_access_regs(fd, addr,
                                   &lecs->cpu[cpuid].coreisr[offset], write);
        }
    }
}

int kvm_loongarch_extioi_pre_save(void *opaque)
{
    kvm_extioi_save_load_regs(opaque, false);
    kvm_extioi_save_load_sw_status(opaque, false);
    return 0;
}

int kvm_loongarch_extioi_post_load(void *opaque, int version_id)
{
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int fd = les->dev_fd;

    kvm_extioi_save_load_regs(opaque, true);
    kvm_extioi_save_load_sw_status(opaque, true);

    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                      KVM_DEV_LOONGARCH_EXTIOI_CTRL_LOAD_FINISHED,
                      NULL, true, &error_abort);
    return 0;
}

void kvm_loongarch_extioi_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(dev);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(dev);
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_EIOINTC, false);
    if (ret < 0) {
        fprintf(stderr, "create KVM_LOONGARCH_EIOINTC failed: %s\n",
                strerror(-ret));
        abort();
    }

    les->dev_fd = ret;
    ret = kvm_device_access(les->dev_fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                            KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_NUM_CPU,
                            &lecs->num_cpu, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_LOONGARCH_EXTIOI_INIT_NUM_CPU failed: %s\n",
                strerror(-ret));
        abort();
    }

    ret = kvm_device_access(les->dev_fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                            KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_FEATURE,
                            &lecs->features, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_LOONGARCH_EXTIOI_INIT_FEATURE failed: %s\n",
                strerror(-ret));
        abort();
    }
}
