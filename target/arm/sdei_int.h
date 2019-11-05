/*
 * ARM SDEI emulation internal interfaces
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 *
 * Authors:
 *    Heyi Guo <guoheyi@huawei.com>
 *    Jingyi Wang <wangjingyi11@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_SDEI_INT_H
#define QEMU_SDEI_INT_H

#include <linux/kvm.h>
#include <linux/arm_sdei.h>
#include <asm-arm64/kvm.h>
#include "hw/intc/arm_gic_common.h"
#include "qemu/thread.h"

#define SDEI_STD_EVT_SOFTWARE_SIGNAL        0
#define SDEI_FEATURE_BIND_SLOTS             0
#define SDEI_PARAM_MAX                      18

#define PRIVATE_SLOT_COUNT                  16
#define PLAT_PRIVATE_SLOT_COUNT             8
#define SHARED_SLOT_COUNT                   32
#define PLAT_SHARED_SLOT_COUNT              16
#define SDEI_INVALID_INTERRUPT              -1
#define SDEI_INVALID_EVENT_ID               -1

#define SDEI_EVENT_TO_SLOT(event)           ((event) & 0xFFFFFF)
#define SDEI_IS_SHARED_EVENT(event)         \
    (SDEI_EVENT_TO_SLOT(event) >= PRIVATE_SLOT_COUNT)

typedef enum {
    SDEI_PRIO_NORMAL        = 0,
    SDEI_PRIO_CRITICAL      = 1,
} QemuSDEIPriority;

typedef struct QemuSDEProp {
    QemuMutex       lock;
    int32_t         event_id;
    int             interrupt;
    bool            is_shared;
    bool            is_critical;
    /* This is the internal index for private or shared SDE */
    int             sde_index;
    int             refcount;
} QemuSDEProp;

typedef struct QemuSDE {
    QemuSDEProp     *prop;
    CPUState        *target_cpu;
    qemu_irq        irq;
    QemuMutex       lock;
    bool            enabled;
    bool            running;
    bool            pending;
    bool            unregister_pending;
    uint64_t        ep_address;
    uint64_t        ep_argument;
    uint64_t        routing_mode;
    int32_t         event_id;
    /*
     * For it is not easy to save the pointer target_cpu during migration, we
     * add below field to save the corresponding numerical values.
     */
    uint64_t        cpu_affinity;
} QemuSDE;

/*
 * GP registers x0~x17 may be modified by client, so they must be saved by
 * dispatcher.
 */
#define SAVED_GP_NUM        18

typedef struct QemuSDECpuCtx {
    uint64_t        xregs[SAVED_GP_NUM];
    uint64_t        pc;
    uint32_t        pstate;
} QemuSDECpuCtx;

typedef enum {
    SDEI_EVENT_PRIO_NORMAL = 0,
    SDEI_EVENT_PRIO_CRITICAL,
    SDEI_EVENT_PRIO_COUNT,
} SdeiEventPriority;

typedef struct QemuSDECpu {
    QemuSDE         *private_sde_array[PRIVATE_SLOT_COUNT];
    QemuSDECpuCtx   ctx[SDEI_EVENT_PRIO_COUNT];
    bool            masked;
    int32_t         critical_running_event;
    int32_t         normal_running_event;
} QemuSDECpu;

typedef struct QemuSDEState {
    DeviceState     parent_obj;
    DeviceState     *gic_dev;
    QemuSDEProp     sde_props_state[PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT];
    QemuSDECpu      *sde_cpus;
    int             sdei_max_cpus;
    int             num_irq;
    QemuSDE         *shared_sde_array[SHARED_SLOT_COUNT];
    int32_t         irq_map[GIC_MAXIRQ];
    QemuMutex       sdei_interrupt_bind_lock;
} QemuSDEState;

#endif
