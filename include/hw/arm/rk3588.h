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
#include "hw/core/qdev.h"
#include "hw/cpu/cluster.h"
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

#define RK3588_SRAM_BASE             0xff000000

struct RK3588SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMCPU cpu[RK3588_NUM_CPUS];
    CPUClusterState cluster[RK3588_CLUSTER_COUNT];
    MemoryRegion *memory;
    MemoryRegion sram;
};

#endif /* HW_ARM_RK3588_H */
