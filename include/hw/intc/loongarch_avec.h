/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch  Advance interrupt controller definitions
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/loongarch/virt.h"


#define NR_VECTORS     256

#define TYPE_LOONGARCH_AVEC "loongarch_avec"
OBJECT_DECLARE_TYPE(LoongArchAVECState, LoongArchAVECClass, LOONGARCH_AVEC)

typedef struct AVECCore {
    CPUState *cpu;
    qemu_irq parent_irq;
    uint64_t arch_id;
} AVECCore;

struct LoongArchAVECState {
    SysBusDevice parent_obj;
    AVECCore *cpu;
    uint32_t num_cpu;
};

struct LoongArchAVECClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};
