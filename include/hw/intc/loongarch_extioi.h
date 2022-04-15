/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 3A5000 ext interrupt controller definitions
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "hw/sysbus.h"
#include "hw/loongarch/loongarch.h"

#ifndef LOONGARCH_EXTIOI_H
#define LOONGARCH_EXTIOI_H

#define LS3A_INTC_IP                 8
#define MAX_CORES                    LOONGARCH_MAX_VCPUS
#define EXTIOI_IRQS                  (256)
/* 32 irqs belong to a group */
#define EXTIOI_IRQS_GROUP_COUNT      (256 / 32)
/* map to ipnum per 32 irqs */
#define EXTIOI_IRQS_NODETYPE_COUNT   16

#define APIC_BASE                    0x1400
#define ENABLE_OFFSET                0x1600
#define IPMAP_OFFSET                 0x14c0
#define COREMAP_OFFSET               0x1c00
#define NODETYPE_OFFSET              0x14a0
#define BOUNCE_OFFSET                0x1680
#define COREISR_OFFSET               0x1800

#define EXTIOI_NODETYPE_START        (0x14a0 - APIC_BASE)
#define EXTIOI_NODETYPE_END          (0x14c0 - APIC_BASE)
#define EXTIOI_BOUNCE_START          0
#define EXTIOI_BOUNCE_END            (0x16a0 - BOUNCE_OFFSET)
#define EXTIOI_COREISR_START         (0x1800 - BOUNCE_OFFSET)
#define EXTIOI_COREISR_END           (0x1B20 - BOUNCE_OFFSET)

#define EXTIOI_IPMAP_START           0
#define EXTIOI_IPMAP_END             (0x14c8 - IPMAP_OFFSET)
#define EXTIOI_ENABLE_START          (0x1600 - IPMAP_OFFSET)
#define EXTIOI_ENABLE_END            (0x1618 - IPMAP_OFFSET)

#define EXTIOI_COREMAP_START         0
#define EXTIOI_COREMAP_END           (0x1d00 - COREMAP_OFFSET)
#define EXTIOI_SIZE                  0x900

#define TYPE_LOONGARCH_EXTIOI "loongarch_extioi"
#define EXTIOI_LINKNAME(name) TYPE_LOONGARCH_EXTIOI#name
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchExtIOI, LOONGARCH_EXTIOI)
struct LoongArchExtIOI {
    SysBusDevice parent_obj;
    /* hardware state */
    uint32_t nodetype[EXTIOI_IRQS_NODETYPE_COUNT / 2];
    uint32_t bounce[EXTIOI_IRQS_GROUP_COUNT];
    uint32_t coreisr[MAX_CORES][EXTIOI_IRQS_GROUP_COUNT];

    uint8_t enable[EXTIOI_IRQS / 8];
    uint8_t ipmap[8];
    uint8_t coremap[EXTIOI_IRQS];
    qemu_irq parent_irq[MAX_CORES][LS3A_INTC_IP];
    qemu_irq irq[EXTIOI_IRQS];
    MemoryRegion mmio[MAX_CORES];
    MemoryRegion mmio_nodetype[MAX_CORES];
    MemoryRegion mmio_ipmap_enable[MAX_CORES];
    MemoryRegion mmio_bounce_coreisr[MAX_CORES];
    MemoryRegion mmio_coremap[MAX_CORES];
};

#endif /* LOONGARCH_EXTIOI_H */
