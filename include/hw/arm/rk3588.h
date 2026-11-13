/*
 * Rockchip RK3588 SoC
 *
 * Copyright (C) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_ARM_RK3588_H
#define HW_ARM_RK3588_H

#include "qom/object.h"
#include "target/arm/cpu.h"
#include "hw/char/dw8250.h"
#include "hw/core/qdev.h"
#include "hw/cpu/cluster.h"
#include "hw/intc/arm_gicv3.h"
#include "system/memory.h"

#define TYPE_RK3588_SOC "rockchip.rk3588.soc"
OBJECT_DECLARE_SIMPLE_TYPE(RK3588SoCState, RK3588_SOC)

#define RK3588_A55_CPUS              (4)
#define RK3588_A76_CPUS_PER_CLUSTER  (2)

typedef enum RK3588CpuClusterId {
    RK3588_CLUSTER_A55,
    RK3588_CLUSTER_A76_0,
    RK3588_CLUSTER_A76_1,
    RK3588_CLUSTER_COUNT,
} RK3588CpuClusterId;

#define RK3588_NUM_CPUS (RK3588_A55_CPUS + \
    RK3588_A76_CPUS_PER_CLUSTER * (RK3588_CLUSTER_COUNT - 1))

/* UART1-UART9 are modeled; UART0 in the PMU power domain is unimplemented. */
#define RK3588_NUM_UARTS             (10)

#define RK3588_SRAM_BASE             0xff000000
#define RK3588_HPTIMER_BASE          0xfd8c8000

struct RK3588SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMCPU cpu[RK3588_NUM_CPUS];
    CPUClusterState cluster[RK3588_CLUSTER_COUNT];
    GICv3State gic;
    DW8250State uart[RK3588_NUM_UARTS];
    MemoryRegion *memory;
    MemoryRegion sram;
    DeviceState *hptimer;
};

#endif /* HW_ARM_RK3588_H */
