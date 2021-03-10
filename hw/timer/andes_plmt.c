/*
 * Andes PLMT (Platform Level Machine Timer)
 *
 * Copyright (c) 2021 Andes Tech. Corp.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/timer/andes_plmt.h"

static uint64_t andes_cpu_riscv_read_rtc(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
        timebase_freq, NANOSECONDS_PER_SECOND);
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void
andes_plmt_write_timecmp(RISCVCPU *cpu, uint64_t value)
{
    uint64_t next;
    uint64_t diff;

    uint64_t rtc_r = andes_cpu_riscv_read_rtc(ANDES_PLMT_TIMEBASE_FREQ);

    cpu->env.timecmp = (uint64_t)value;
    if (cpu->env.timecmp <= rtc_r) {
        /*
         * if we're setting an MTIMECMP value in the "past",
         * immediately raise the timer interrupt
         */
        riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(1));
        return;
    }

    /* otherwise, set up the future timer interrupt */
    riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(0));
    diff = cpu->env.timecmp - rtc_r;
    /* back to ns (note args switched in muldiv64) */
    next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        muldiv64(diff, NANOSECONDS_PER_SECOND, ANDES_PLMT_TIMEBASE_FREQ);
    timer_mod(cpu->env.timer, next);
}

/*
 * Callback used when the timer set using timer_mod expires.
 * Should raise the timer interrupt line
 */
static void
andes_plmt_timer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(1));
}

static uint64_t
andes_plmt_read(void *opaque, hwaddr addr, unsigned size)
{
    AndesPLMTState *plmt = opaque;
    uint64_t rz = 0;

    if ((addr >= (plmt->timecmp_base)) &&
        (addr < (plmt->timecmp_base + (plmt->num_harts << 3)))) {
        /* %8=0:timecmp_lo, %8=4:timecmp_hi */
        size_t hartid = (addr - plmt->timecmp_base) >> 3;
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("plmt: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            rz = env->timecmp & (unsigned long)0xFFFFFFFF;
        } else if ((addr & 0x7) == 4) {
            rz = (env->timecmp >> 32) & (unsigned long)0xFFFFFFFF;
        } else {
            error_report("plmt: invalid read: %08x", (uint32_t)addr);
        }
    } else if (addr == (plmt->time_base)) {
        /* time_lo */
        rz = andes_cpu_riscv_read_rtc(ANDES_PLMT_TIMEBASE_FREQ)
                    & (unsigned long)0xFFFFFFFF;
    } else if (addr == (plmt->time_base + 4)) {
        /* time_hi */
        rz = (andes_cpu_riscv_read_rtc(ANDES_PLMT_TIMEBASE_FREQ) >> 32)
                    & (unsigned long)0xFFFFFFFF;
    } else {
        error_report("plmt: invalid read: %08x", (uint32_t)addr);
    }

    return rz;
}

static void
andes_plmt_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    AndesPLMTState *plmt = opaque;

    if ((addr >= (plmt->timecmp_base)) &&
        (addr < (plmt->timecmp_base + (plmt->num_harts << 3)))) {
        /* %8=0:timecmp_lo, %8=4:timecmp_hi */
        size_t hartid = (addr - plmt->timecmp_base) >> 3;
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("plmt: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            uint64_t timecmp_hi = (unsigned long)env->timecmp >> 32;
            andes_plmt_write_timecmp(RISCV_CPU(cpu),
                (timecmp_hi << 32) | (value & (unsigned long)0xFFFFFFFF));
        } else if ((addr & 0x7) == 4) {
            uint64_t timecmp_lo = env->timecmp;
            andes_plmt_write_timecmp(RISCV_CPU(cpu),
                (value << 32) | (timecmp_lo & (unsigned long)0xFFFFFFFF));
        } else {
            error_report("plmt: invalid write: %08x", (uint32_t)addr);
        }
    } else if (addr == (plmt->time_base)) {
        /* time_lo */
        error_report("plmt: time_lo write not implemented");
    } else if (addr == (plmt->time_base + 4)) {
        /* time_hi */
        error_report("plmt: time_hi write not implemented");
    } else {
        error_report("plmt: invalid write: %08x", (uint32_t)addr);
    }
}

static const MemoryRegionOps andes_plmt_ops = {
    .read = andes_plmt_read,
    .write = andes_plmt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    }
};

static Property andes_plmt_properties[] = {
    DEFINE_PROP_UINT32("num-harts", AndesPLMTState, num_harts, 0),
    DEFINE_PROP_UINT32("time-base", AndesPLMTState, time_base, 0),
    DEFINE_PROP_UINT32("timecmp-base", AndesPLMTState, timecmp_base, 0),
    DEFINE_PROP_UINT32("aperture-size", AndesPLMTState, aperture_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void andes_plmt_realize(DeviceState *dev, Error **errp)
{
    AndesPLMTState *s = ANDES_PLMT(dev);
    memory_region_init_io(&s->mmio, OBJECT(dev), &andes_plmt_ops, s,
        TYPE_ANDES_PLMT, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void andes_plmt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = andes_plmt_realize;
    device_class_set_props(dc, andes_plmt_properties);
}

static const TypeInfo andes_plmt_info = {
    .name = TYPE_ANDES_PLMT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AndesPLMTState),
    .class_init = andes_plmt_class_init,
};

static void andes_plmt_register_types(void)
{
    type_register_static(&andes_plmt_info);
}

type_init(andes_plmt_register_types)

/*
 * Create PLMT device.
 */
DeviceState*
andes_plmt_create(hwaddr addr, hwaddr size, uint32_t num_harts,
    uint32_t time_base, uint32_t timecmp_base)
{
    int i;
    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = qemu_get_cpu(i);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            continue;
        }
        riscv_cpu_set_rdtime_fn(env, andes_cpu_riscv_read_rtc,
            ANDES_PLMT_TIMEBASE_FREQ);

        env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  &andes_plmt_timer_cb, cpu);
        env->timecmp = 0;
    }

    DeviceState *dev = qdev_new(TYPE_ANDES_PLMT);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "time-base", time_base);
    qdev_prop_set_uint32(dev, "timecmp-base", timecmp_base);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
