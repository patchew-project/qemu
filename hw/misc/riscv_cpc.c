/*
 * Cluster Power Controller emulation
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * Copyright (c) 2025 MIPS
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"

#include "hw/misc/riscv_cpc.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_aclint.h"

static inline uint64_t cpc_vp_run_mask(RISCVCPCState *cpc)
{
    if (cpc->num_vp == 64) {
        return 0xffffffffffffffff;
    }
    return (1ULL << cpc->num_vp) - 1;
}

static void riscv_cpu_reset_async_work(CPUState *cs, run_on_cpu_data data)
{
    RISCVCPCState *cpc = (RISCVCPCState *) data.host_ptr;

    cpu_reset(cs);
    cs->halted = 0;
    cpc->vp_running |= 1ULL << cs->cpu_index;
}

static void cpc_run_vp(RISCVCPCState *cpc, uint64_t vp_run)
{
    CPUState *cs = first_cpu;

    CPU_FOREACH(cs) {
        uint64_t i = 1ULL << cs->cpu_index;
        if (i & vp_run & ~cpc->vp_running) {
            /*
             * To avoid racing with a CPU we are just kicking off.
             * We do the final bit of preparation for the work in
             * the target CPUs context.
             */
            async_safe_run_on_cpu(cs, riscv_cpu_reset_async_work,
                                  RUN_ON_CPU_HOST_PTR(cpc));
        }
    }
}

static void cpc_stop_vp(RISCVCPCState *cpc, uint64_t vp_stop)
{
    CPUState *cs = first_cpu;

    CPU_FOREACH(cs) {
        uint64_t i = 1ULL << cs->cpu_index;
        if (i & vp_stop & cpc->vp_running) {
            cpu_interrupt(cs, CPU_INTERRUPT_HALT);
            cpc->vp_running &= ~i;
        }
    }
}

static void cpc_write(void *opaque, hwaddr offset, uint64_t data,
                      unsigned size)
{
    RISCVCPCState *s = opaque;
    int cpu_index, c;

    for (c = 0; c < s->num_core; c++) {
        cpu_index = c * s->num_hart +
                    s->cluster_id * s->num_core * s->num_hart;
        if (offset == CPC_CL_BASE_OFS + CPC_VP_RUN_OFS + c * 0x100) {
            cpc_run_vp(s, (data << cpu_index) & cpc_vp_run_mask(s));
            return;
        }
        if (offset == CPC_CL_BASE_OFS + CPC_VP_STOP_OFS + c * 0x100) {
            cpc_stop_vp(s, (data << cpu_index) & cpc_vp_run_mask(s));
            return;
        }
    }

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return;
}

static uint64_t cpc_read(void *opaque, hwaddr offset, unsigned size)
{
    RISCVCPCState *s = opaque;
    int c;

    for (c = 0; c < s->num_core; c++) {
        if (offset == CPC_CL_BASE_OFS + CPC_STAT_CONF_OFS + c * 0x100) {
            /* Return the state as U6. */
            return CPC_Cx_STAT_CONF_SEQ_STATE_U6;
        }
    }

    switch (offset) {
    case CPC_CM_STAT_CONF_OFS:
        return CPC_Cx_STAT_CONF_SEQ_STATE_U5;
    case CPC_MTIME_REG_OFS:
        return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ,
                        NANOSECONDS_PER_SECOND);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        return 0;
    }
}

static const MemoryRegionOps cpc_ops = {
    .read = cpc_read,
    .write = cpc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void riscv_cpc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RISCVCPCState *s = RISCV_CPC(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &cpc_ops, s, "riscv-cpc",
                          CPC_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->mr);
}

static void riscv_cpc_realize(DeviceState *dev, Error **errp)
{
    RISCVCPCState *s = RISCV_CPC(dev);

    if (s->vp_start_running > cpc_vp_run_mask(s)) {
        error_setg(errp,
                   "incorrect vp_start_running 0x%" PRIx64 " for num_vp = %d",
                   s->vp_running, s->num_vp);
        return;
    }
}

static void riscv_cpc_reset(DeviceState *dev)
{
    RISCVCPCState *s = RISCV_CPC(dev);

    /* Reflect the fact that all VPs are halted on reset */
    s->vp_running = 0;

    /* Put selected VPs into run state */
    cpc_run_vp(s, s->vp_start_running);
}

static const VMStateDescription vmstate_riscv_cpc = {
    .name = "riscv-cpc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(vp_running, RISCVCPCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property riscv_cpc_properties[] = {
    DEFINE_PROP_UINT32("cluster-id", RISCVCPCState, cluster_id, 0x0),
    DEFINE_PROP_UINT32("num-vp", RISCVCPCState, num_vp, 0x1),
    DEFINE_PROP_UINT32("num-hart", RISCVCPCState, num_hart, 0x1),
    DEFINE_PROP_UINT32("num-core", RISCVCPCState, num_core, 0x1),
    DEFINE_PROP_UINT64("vp-start-running", RISCVCPCState, vp_start_running, 0x1),
};

static void riscv_cpc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = riscv_cpc_realize;
    device_class_set_legacy_reset(dc, riscv_cpc_reset);
    dc->vmsd = &vmstate_riscv_cpc;
    device_class_set_props(dc, riscv_cpc_properties);
}

static const TypeInfo riscv_cpc_info = {
    .name          = TYPE_RISCV_CPC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVCPCState),
    .instance_init = riscv_cpc_init,
    .class_init    = riscv_cpc_class_init,
};

static void riscv_cpc_register_types(void)
{
    type_register_static(&riscv_cpc_info);
}

type_init(riscv_cpc_register_types)
