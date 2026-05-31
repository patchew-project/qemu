/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore System Timer Module (STM)
 *
 * Copyright (c) 2024 Infineon Technologies AG
 */

#include "qemu/osdep.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/timer/tricore_stm.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

enum {
    R_CLC,
    R_RESERVED1,
    R_ID,
    R_RESERVED2,
    R_TIM0,
    R_TIM1,
    R_TIM2,
    R_TIM3,
    R_TIM4,
    R_TIM5,
    R_TIM6,
    R_CAP,
    R_CMP0,
    R_CMP1,
    R_CMCON,
    R_ICR,
    R_ISCR,
    R_RESERVED3,
    R_TIM0SV  = 0x50 / 4,
    R_CAPSV,
    R_RESERVED4,
    R_OCS     = 0xE8 / 4,
    R_KRSTCLR,
    R_KRST1,
    R_KRST0,
    R_ACCEN1,
    R_ACCEN0,
};

static void tricore_stm_tim_update(TriCoreSTMState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->tim_counter += clock_ns_to_ticks(s->fstm, now - s->tim_base_ns);
    s->tim_base_ns = now;
}

static void tricore_stm_timer_update(TriCoreSTMState *s)
{
    uint32_t mstart = (s->regs[R_CMCON] & MASK_CMCON_MSTART0) >> 8;
    uint32_t msize = s->regs[R_CMCON] & MASK_CMCON_MSIZE0;
    uint32_t nbits = msize + 1;
    uint64_t tim_target = s->tim_counter;

    if (!(s->regs[R_ICR] & MASK_ICR_CMP0EN) &&
        (s->regs[R_ICR] & MASK_ICR_CMP0IR)) {
        return;
    }

    /* Calculate the target TIM value */
    if (mstart) {
        tim_target = deposit64(tim_target, 0, mstart, 0);
    }
    tim_target = deposit64(tim_target, mstart, nbits,
                           (uint64_t)s->regs[R_CMP0]);

    /* Wrap around if needed */
    if (tim_target <= s->tim_counter) {
        tim_target += (1ULL << (mstart + nbits));
    }

    timer_mod(s->timer,
              s->tim_base_ns +
              clock_ticks_to_ns(s->fstm, tim_target - s->tim_counter));
}

static void tricore_stm_clock_update(void *opaque, enum ClockEvent event)
{
    TriCoreSTMState *s = TRICORE_STM(opaque);

    if (event == ClockPreUpdate) {
        tricore_stm_tim_update(s);
    } else {
        tricore_stm_timer_update(s);
    }
}

static void tricore_stm_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    TriCoreSTMState *s = TRICORE_STM(opaque);
    hwaddr reg_addr = offset >> 2;

    switch (reg_addr) {
    case R_CLC:
    case R_ID:
    case R_TIM0:
    case R_TIM1:
    case R_TIM2:
    case R_TIM3:
    case R_TIM4:
    case R_TIM5:
    case R_TIM6:
    case R_CAP:
        s->regs[reg_addr] = value;
        break;
    case R_CMP0:
        s->regs[reg_addr] = value;
        tricore_stm_tim_update(s);
        tricore_stm_timer_update(s);
        break;
    case R_CMP1:
    case R_ICR:
        s->regs[reg_addr] = value;
        tricore_stm_timer_update(s);
        break;
    case R_CMCON:
        s->regs[reg_addr] = value;
        tricore_stm_tim_update(s);
        tricore_stm_timer_update(s);
        break;
    case R_ISCR:
        if (value & MASK_ISCR_CMP0IRR) {
            qatomic_and(&s->regs[R_ICR], ~MASK_ICR_CMP0IR);
        }
        if (value & MASK_ISCR_CMP1IRR) {
            qatomic_and(&s->regs[R_ICR], ~MASK_ICR_CMP1IR);
        }
        if (value & MASK_ISCR_CMP0IRS) {
            qatomic_or(&s->regs[R_ICR], MASK_ICR_CMP0IR);
        }
        if (value & MASK_ISCR_CMP1IRS) {
            qatomic_or(&s->regs[R_ICR], MASK_ICR_CMP1IR);
        }
        tricore_stm_timer_update(s);
        break;
    case R_TIM0SV:
    case R_CAPSV:
    case R_OCS:
    case R_KRSTCLR:
    case R_KRST1:
    case R_KRST0:
    case R_ACCEN1:
    case R_ACCEN0:
        s->regs[reg_addr] = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "tricore_stm: write to unknown register 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static uint64_t tricore_stm_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    TriCoreSTMState *s = TRICORE_STM(opaque);
    uint64_t r = 0;
    hwaddr reg_addr = offset >> 2;

    if ((reg_addr >= R_TIM0 && reg_addr <= R_TIM6) ||
        reg_addr == R_TIM0SV) {
        tricore_stm_tim_update(s);
        s->regs[R_CAP] = (uint32_t)(s->tim_counter >> 32);
    }

    switch (reg_addr) {
    case R_CLC:
    case R_ID:
        r = s->regs[reg_addr];
        break;
    case R_TIM0:
    case R_TIM1:
    case R_TIM2:
    case R_TIM3:
    case R_TIM4:
    case R_TIM5:
    case R_TIM6:
        r = s->tim_counter << (reg_addr - R_TIM0) * 4;
        break;
    case R_CAP:
        r = s->regs[R_CAP];
        break;
    case R_ISCR:
        r = 0;
        break;
    case R_CMP0:
    case R_CMP1:
    case R_CMCON:
    case R_ICR:
        r = s->regs[reg_addr];
        break;
    case R_TIM0SV:
        r = s->tim_counter;
        break;
    case R_CAPSV:
        r = s->regs[R_CAP];
        break;
    case R_OCS:
    case R_KRSTCLR:
    case R_KRST1:
    case R_KRST0:
    case R_ACCEN1:
    case R_ACCEN0:
        r = s->regs[reg_addr];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "tricore_stm: read from unknown register 0x%"
                      HWADDR_PRIx "\n", offset);
        r = 0;
        break;
    }

    return r;
}

static void tricore_stm_reset(DeviceState *dev)
{
    TriCoreSTMState *s = TRICORE_STM(dev);

    s->regs[R_CLC] = RESET_TRICORE_STM_CLC;
    s->regs[R_ID] = RESET_TRICORE_STM_ID;
    s->regs[R_TIM0] = RESET_TRICORE_STM_TIM0;
    s->regs[R_TIM1] = RESET_TRICORE_STM_TIM1;
    s->regs[R_TIM2] = RESET_TRICORE_STM_TIM2;
    s->regs[R_TIM3] = RESET_TRICORE_STM_TIM3;
    s->regs[R_TIM4] = RESET_TRICORE_STM_TIM4;
    s->regs[R_TIM5] = RESET_TRICORE_STM_TIM5;
    s->regs[R_TIM6] = RESET_TRICORE_STM_TIM6;
    s->regs[R_CAP] = RESET_TRICORE_STM_CAP;
    s->regs[R_CMP0] = RESET_TRICORE_STM_CMP0;
    s->regs[R_CMP1] = RESET_TRICORE_STM_CMP1;
    s->regs[R_CMCON] = RESET_TRICORE_STM_CMCON;
    s->regs[R_ICR] = RESET_TRICORE_STM_ICR;
    s->regs[R_ISCR] = RESET_TRICORE_STM_ISCR;
    s->regs[R_TIM0SV] = RESET_TRICORE_STM_TIM0SV;
    s->regs[R_CAPSV] = RESET_TRICORE_STM_CAPSV;
    s->regs[R_OCS] = RESET_TRICORE_STM_OCS;
    s->regs[R_KRSTCLR] = RESET_TRICORE_STM_KRSTCLR;
    s->regs[R_KRST1] = RESET_TRICORE_STM_KRST1;
    s->regs[R_KRST0] = RESET_TRICORE_STM_KRST0;
    s->regs[R_ACCEN1] = RESET_TRICORE_STM_ACCEN1;
    s->regs[R_ACCEN0] = RESET_TRICORE_STM_ACCEN0;

    s->tim_counter = 0;
    s->tim_base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static const MemoryRegionOps tricore_stm_ops = {
    .read = tricore_stm_read,
    .write = tricore_stm_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void tricore_stm_timer_hit(void *opaque)
{
    TriCoreSTMState *s = TRICORE_STM(opaque);

    qatomic_or(&s->regs[R_ICR], MASK_ICR_CMP0IR);

    if (s->regs[R_ICR] & MASK_ICR_CMP0EN) {
        qemu_irq_pulse(s->irq);
    }

    tricore_stm_tim_update(s);
    tricore_stm_timer_update(s);
}

static void tricore_stm_realize(DeviceState *dev, Error **errp)
{
    TriCoreSTMState *s = TRICORE_STM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, tricore_stm_timer_hit, s);
    s->tim_base_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tim_counter = 0;

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void tricore_stm_init(Object *obj)
{
    TriCoreSTMState *s = TRICORE_STM(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &tricore_stm_ops, s,
                          "tricore_stm", 0x100);
    s->fstm = qdev_init_clock_in(DEVICE(s), "fstm",
                                 tricore_stm_clock_update, s,
                                 ClockPreUpdate | ClockUpdate);
}

static void tricore_stm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->legacy_reset = tricore_stm_reset;
    dc->realize = tricore_stm_realize;
}

static const TypeInfo tricore_stm_info = {
    .name = TYPE_TRICORE_STM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TriCoreSTMState),
    .instance_init = tricore_stm_init,
    .class_init = tricore_stm_class_init,
};

static void tricore_stm_register_types(void)
{
    type_register_static(&tricore_stm_info);
}

type_init(tricore_stm_register_types)
