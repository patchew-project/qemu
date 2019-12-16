/*
 * Allwinner H3 CPU Configuration Module emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "arm-powerctl.h"
#include "hw/misc/allwinner-h3-cpucfg.h"
#include "trace.h"

/* CPUCFG register offsets */
enum {
    REG_CPUS_RST_CTRL       = 0x0000, /* CPUs Reset Control */
    REG_CPU0_RST_CTRL       = 0x0040, /* CPU#0 Reset Control */
    REG_CPU0_CTRL           = 0x0044, /* CPU#0 Control */
    REG_CPU0_STATUS         = 0x0048, /* CPU#0 Status */
    REG_CPU1_RST_CTRL       = 0x0080, /* CPU#1 Reset Control */
    REG_CPU1_CTRL           = 0x0084, /* CPU#1 Control */
    REG_CPU1_STATUS         = 0x0088, /* CPU#1 Status */
    REG_CPU2_RST_CTRL       = 0x00C0, /* CPU#2 Reset Control */
    REG_CPU2_CTRL           = 0x00C4, /* CPU#2 Control */
    REG_CPU2_STATUS         = 0x00C8, /* CPU#2 Status */
    REG_CPU3_RST_CTRL       = 0x0100, /* CPU#3 Reset Control */
    REG_CPU3_CTRL           = 0x0104, /* CPU#3 Control */
    REG_CPU3_STATUS         = 0x0108, /* CPU#3 Status */
    REG_CPU_SYS_RST         = 0x0140, /* CPU System Reset */
    REG_CLK_GATING          = 0x0144, /* CPU Clock Gating */
    REG_GEN_CTRL            = 0x0184, /* General Control */
    REG_SUPER_STANDBY       = 0x01A0, /* Super Standby Flag */
    REG_ENTRY_ADDR          = 0x01A4, /* Reset Entry Address */
    REG_DBG_EXTERN          = 0x01E4, /* Debug External */
    REG_CNT64_CTRL          = 0x0280, /* 64-bit Counter Control */
    REG_CNT64_LOW           = 0x0284, /* 64-bit Counter Low */
    REG_CNT64_HIGH          = 0x0288, /* 64-bit Counter High */
};

/* CPUCFG register flags */
enum {
    CPUX_RESET_RELEASED     = ((1 << 1) | (1 << 0)),
    CPUX_STATUS_SMP         = (1 << 0),
    CPU_SYS_RESET_RELEASED  = (1 << 0),
    CLK_GATING_ENABLE       = ((1 << 8) | 0xF),
};

/* CPUCFG register reset values */
enum {
    REG_CLK_GATING_RST      = 0x0000010F,
    REG_GEN_CTRL_RST        = 0x00000020,
    REG_SUPER_STANDBY_RST   = 0x0,
    REG_CNT64_CTRL_RST      = 0x0,
};

static void allwinner_h3_cpucfg_cpu_reset(AwH3CpuCfgState *s, uint8_t cpu_id)
{
    int ret;

    trace_allwinner_h3_cpucfg_cpu_reset(cpu_id, s->entry_addr);

    ret = arm_set_cpu_on(cpu_id, s->entry_addr, 0, 3, false);
    if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS) {
        error_report("%s: failed to bring up CPU %d: err %d",
                     __func__, cpu_id, ret);
        return;
    }
}

static uint64_t allwinner_h3_cpucfg_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    const AwH3CpuCfgState *s = (AwH3CpuCfgState *)opaque;
    uint64_t val = 0;

    switch (offset) {
    case REG_CPUS_RST_CTRL:     /* CPUs Reset Control */
    case REG_CPU_SYS_RST:       /* CPU System Reset */
        val = CPU_SYS_RESET_RELEASED;
        break;
    case REG_CPU0_RST_CTRL:     /* CPU#0 Reset Control */
    case REG_CPU1_RST_CTRL:     /* CPU#1 Reset Control */
    case REG_CPU2_RST_CTRL:     /* CPU#2 Reset Control */
    case REG_CPU3_RST_CTRL:     /* CPU#3 Reset Control */
        val = CPUX_RESET_RELEASED;
        break;
    case REG_CPU0_CTRL:         /* CPU#0 Control */
    case REG_CPU1_CTRL:         /* CPU#1 Control */
    case REG_CPU2_CTRL:         /* CPU#2 Control */
    case REG_CPU3_CTRL:         /* CPU#3 Control */
        val = 0;
        break;
    case REG_CPU0_STATUS:       /* CPU#0 Status */
    case REG_CPU1_STATUS:       /* CPU#1 Status */
    case REG_CPU2_STATUS:       /* CPU#2 Status */
    case REG_CPU3_STATUS:       /* CPU#3 Status */
        val = CPUX_STATUS_SMP;
        break;
    case REG_CLK_GATING:        /* CPU Clock Gating */
        val = CLK_GATING_ENABLE;
        break;
    case REG_GEN_CTRL:          /* General Control */
        val = s->gen_ctrl;
        break;
    case REG_SUPER_STANDBY:     /* Super Standby Flag */
        val = s->super_standby;
        break;
    case REG_ENTRY_ADDR:        /* Reset Entry Address */
        val = s->entry_addr;
        break;
    case REG_DBG_EXTERN:        /* Debug External */
        break;
    case REG_CNT64_CTRL:        /* 64-bit Counter Control */
        val = s->counter_ctrl;
        break;
    case REG_CNT64_LOW:         /* 64-bit Counter Low */
        val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) & 0xffffffff;
        break;
    case REG_CNT64_HIGH:        /* 64-bit Counter High */
        val = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 32;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    trace_allwinner_h3_cpucfg_read(offset, val, size);

    return val;
}

static void allwinner_h3_cpucfg_write(void *opaque, hwaddr offset,
                                      uint64_t val, unsigned size)
{
    AwH3CpuCfgState *s = (AwH3CpuCfgState *)opaque;

    trace_allwinner_h3_cpucfg_write(offset, val, size);

    switch (offset) {
    case REG_CPUS_RST_CTRL:     /* CPUs Reset Control */
    case REG_CPU_SYS_RST:       /* CPU System Reset */
        break;
    case REG_CPU0_RST_CTRL:     /* CPU#0 Reset Control */
        if (val) {
            allwinner_h3_cpucfg_cpu_reset(s, 0);
        }
        break;
    case REG_CPU1_RST_CTRL:     /* CPU#1 Reset Control */
        if (val) {
            allwinner_h3_cpucfg_cpu_reset(s, 1);
        }
        break;
    case REG_CPU2_RST_CTRL:     /* CPU#2 Reset Control */
        if (val) {
            allwinner_h3_cpucfg_cpu_reset(s, 2);
        }
        break;
    case REG_CPU3_RST_CTRL:     /* CPU#3 Reset Control */
        if (val) {
            allwinner_h3_cpucfg_cpu_reset(s, 3);
        }
        break;
    case REG_CPU0_CTRL:         /* CPU#0 Control */
    case REG_CPU1_CTRL:         /* CPU#1 Control */
    case REG_CPU2_CTRL:         /* CPU#2 Control */
    case REG_CPU3_CTRL:         /* CPU#3 Control */
    case REG_CPU0_STATUS:       /* CPU#0 Status */
    case REG_CPU1_STATUS:       /* CPU#1 Status */
    case REG_CPU2_STATUS:       /* CPU#2 Status */
    case REG_CPU3_STATUS:       /* CPU#3 Status */
    case REG_CLK_GATING:        /* CPU Clock Gating */
    case REG_GEN_CTRL:          /* General Control */
        s->gen_ctrl = val;
        break;
    case REG_SUPER_STANDBY:     /* Super Standby Flag */
        s->super_standby = val;
        break;
    case REG_ENTRY_ADDR:        /* Reset Entry Address */
        s->entry_addr = val;
        break;
    case REG_DBG_EXTERN:        /* Debug External */
        break;
    case REG_CNT64_CTRL:        /* 64-bit Counter Control */
        s->counter_ctrl = val;
        break;
    case REG_CNT64_LOW:         /* 64-bit Counter Low */
    case REG_CNT64_HIGH:        /* 64-bit Counter High */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }
}

static const MemoryRegionOps allwinner_h3_cpucfg_ops = {
    .read = allwinner_h3_cpucfg_read,
    .write = allwinner_h3_cpucfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
    .impl.min_access_size = 4,
};

static void allwinner_h3_cpucfg_reset(DeviceState *dev)
{
    AwH3CpuCfgState *s = AW_H3_CPUCFG(dev);

    /* Set default values for registers */
    s->gen_ctrl = REG_GEN_CTRL_RST;
    s->super_standby = REG_SUPER_STANDBY_RST;
    s->entry_addr = 0;
    s->counter_ctrl = REG_CNT64_CTRL_RST;
}

static void allwinner_h3_cpucfg_realize(DeviceState *dev, Error **errp)
{
}

static void allwinner_h3_cpucfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwH3CpuCfgState *s = AW_H3_CPUCFG(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_h3_cpucfg_ops, s,
                          TYPE_AW_H3_CPUCFG, 1 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_h3_cpucfg_vmstate = {
    .name = "allwinner-h3-cpucfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(gen_ctrl, AwH3CpuCfgState),
        VMSTATE_UINT32(super_standby, AwH3CpuCfgState),
        VMSTATE_UINT32(counter_ctrl, AwH3CpuCfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_h3_cpucfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_h3_cpucfg_reset;
    dc->realize = allwinner_h3_cpucfg_realize;
    dc->vmsd = &allwinner_h3_cpucfg_vmstate;
}

static const TypeInfo allwinner_h3_cpucfg_info = {
    .name          = TYPE_AW_H3_CPUCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_h3_cpucfg_init,
    .instance_size = sizeof(AwH3CpuCfgState),
    .class_init    = allwinner_h3_cpucfg_class_init,
};

static void allwinner_h3_cpucfg_register(void)
{
    type_register_static(&allwinner_h3_cpucfg_info);
}

type_init(allwinner_h3_cpucfg_register)
