/*
 *  NUCLEI TIMER (Timer Unit) interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This provides a parameterizable timer controller based on NucLei's Systimer.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "target/riscv/cpu.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

static uint64_t cpu_riscv_read_rtc(uint64_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    timebase_freq, NANOSECONDS_PER_SECOND);
}

static void nuclei_timer_update_compare(NucLeiSYSTIMERState *s)
{
    CPUState *cpu = qemu_get_cpu(0);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
    uint64_t cmp, real_time;
    int64_t diff;

    real_time = s->mtime_lo | ((uint64_t)s->mtime_hi << 32);

    cmp = (uint64_t)s->mtimecmp_lo | ((uint64_t)s->mtimecmp_hi << 32);
    env->mtimecmp = cmp;
    env->timecmp = cmp;

    diff = cmp - real_time;

    if (real_time >= cmp) {
        qemu_set_irq(*(s->timer_irq), 1);
    } else {
        qemu_set_irq(*(s->timer_irq), 0);

        if (s->mtimecmp_hi != 0xffffffff) {
            uint64_t next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            muldiv64(diff, NANOSECONDS_PER_SECOND, s->timebase_freq);
            timer_mod(env->mtimer, next_ns);
        }
    }
}

static void nuclei_timer_reset(DeviceState *dev)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);
    s->mtime_lo = 0x0;
    s->mtime_hi = 0x0;
    s->mtimecmp_lo = 0xFFFFFFFF;
    s->mtimecmp_hi = 0xFFFFFFFF;
    s->mstop = 0x0;
    s->mstop = 0x0;
}

static uint64_t nuclei_timer_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);
    CPUState *cpu = qemu_get_cpu(0);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
    uint64_t value = 0;

    switch (offset) {
    case NUCLEI_SYSTIMER_REG_MTIMELO:
        value = cpu_riscv_read_rtc(s->timebase_freq);
        s->mtime_lo = value & 0xffffffff;
        s->mtime_hi = (value >> 32) & 0xffffffff;
        value = s->mtime_lo;
        break;
    case NUCLEI_SYSTIMER_REG_MTIMEHI:
        value = s->mtime_hi;
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPLO:
        s->mtimecmp_lo = (env->mtimecmp) & 0xFFFFFFFF;
        value = s->mtimecmp_lo;
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPHI:
        s->mtimecmp_hi = (env->mtimecmp >> 32) & 0xFFFFFFFF;
        value = s->mtimecmp_hi;
        break;
    case NUCLEI_SYSTIMER_REG_MSFTRST:
        break;
    case NUCLEI_SYSTIMER_REG_MSTOP:
        value = s->mstop;
        break;
    case NUCLEI_SYSTIMER_REG_MSIP:
        value = s->msip;
        break;
    default:
        break;
    }
    value &= 0xFFFFFFFF;
    return value;

}

static void nuclei_timer_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);
    CPUState *cpu = qemu_get_cpu(0);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;

    value = value & 0xFFFFFFFF;
    switch (offset) {
    case NUCLEI_SYSTIMER_REG_MTIMELO:
        s->mtime_lo = value;
        env->mtimer->expire_time |= (value & 0xFFFFFFFF);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMEHI:
        s->mtime_hi = value;
        env->mtimer->expire_time |= ((value << 32) & 0xFFFFFFFF);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPLO:
        s->mtimecmp_lo = value;
        s->mtimecmp_hi = 0xFFFFFFFF;
        env->mtimecmp |= (value & 0xFFFFFFFF);
        nuclei_timer_update_compare(s);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPHI:
        s->mtimecmp_hi = value;
        env->mtimecmp |= ((value << 32) & 0xFFFFFFFF);
        nuclei_timer_update_compare(s);
        break;
    case NUCLEI_SYSTIMER_REG_MSFTRST:
        if (!(value & 0x80000000) == 0) {
            nuclei_timer_reset((DeviceState *)s);
        }
        break;
    case NUCLEI_SYSTIMER_REG_MSTOP:
        s->mstop = value;
        break;
    case NUCLEI_SYSTIMER_REG_MSIP:
        s->msip = value;
        if ((s->msip & 0x1) == 1) {
            qemu_set_irq(*(s->soft_irq), 1);
        } else {
            qemu_set_irq(*(s->soft_irq), 0);
        }

        break;
    default:
        break;
    }
}

static const MemoryRegionOps nuclei_timer_ops = {
    .read = nuclei_timer_read,
    .write = nuclei_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static Property nuclei_systimer_properties[] = {
    DEFINE_PROP_UINT32("aperture-size", NucLeiSYSTIMERState, aperture_size, 0),
    DEFINE_PROP_UINT32("timebase-freq", NucLeiSYSTIMERState, timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_timer_realize(DeviceState *dev, Error **errp)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);

    if (s->aperture_size == 0) {
        s->aperture_size = 0x1000;
    }
    memory_region_init_io(&s->iomem, OBJECT(dev), &nuclei_timer_ops,
                          s, TYPE_NUCLEI_SYSTIMER, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void nuclei_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = nuclei_timer_realize;
    dc->reset = nuclei_timer_reset;
    dc->desc = "NucLei Systimer Timer";
    device_class_set_props(dc, nuclei_systimer_properties);
}

static const TypeInfo nuclei_timer_info = {
    .name = TYPE_NUCLEI_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucLeiSYSTIMERState),
    .class_init = nuclei_timer_class_init,
};

static void nuclei_timer_register_types(void)
{
    type_register_static(&nuclei_timer_info);
}
type_init(nuclei_timer_register_types);

static void nuclei_mtimecmp_cb(void *opaque)
{
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(0));
    CPURISCVState *env = &cpu->env;
    nuclei_eclic_systimer_cb(((RISCVCPU *)cpu)->env.eclic);
    timer_del(env->mtimer);
}

DeviceState *nuclei_systimer_create(hwaddr addr, hwaddr size,
                                    DeviceState *eclic, uint32_t timebase_freq)
{
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(0));
    CPURISCVState *env = &cpu->env;

    env->features |= (1ULL << RISCV_FEATURE_ECLIC);
    env->mtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                               &nuclei_mtimecmp_cb, cpu);
    env->mtimecmp = 0;

    DeviceState *dev = qdev_new(TYPE_NUCLEI_SYSTIMER);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "timebase-freq", timebase_freq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);
    if (eclic != NULL) {
        s->eclic = eclic;
        s->soft_irq = &(NUCLEI_ECLIC(eclic)->irqs[Internal_SysTimerSW_IRQn]);
        s->timer_irq = &(NUCLEI_ECLIC(eclic)->irqs[Internal_SysTimer_IRQn]);
    }
    return dev;
}
