/*
 * STM32L4x5 EXTI (Extended interrupts and events controller)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Samuel Tardieu <samuel.tardieu@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is based on the stm32f4xx_exti by Alistair Francis.
 * Original code is licensed under the MIT License:
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32l4x5_exti.h"

#define EXTI_IMR1   0x00
#define EXTI_EMR1   0x04
#define EXTI_RTSR1  0x08
#define EXTI_FTSR1  0x0C
#define EXTI_SWIER1 0x10
#define EXTI_PR1    0x14
#define EXTI_IMR2   0x20
#define EXTI_EMR2   0x24
#define EXTI_RTSR2  0x28
#define EXTI_FTSR2  0x2C
#define EXTI_SWIER2 0x30
#define EXTI_PR2    0x34

#define EXTI_NUM_GPIO_EVENT_IN_LINES 16

/* 0b11111111_10000010_00000000_00000000 */
#define DIRECT_LINE_MASK1 0xFF820000
/* 0b00000000_00000000_00000000_10000111 */
#define DIRECT_LINE_MASK2 0x00000087
/* 0b11111111_11111111_11111111_00000000 */
#define RESERVED_BITS_MASK2 0xFFFFFF00

/* 0b00000000_00000000_00000000_01111000 */
#define ACTIVABLE_MASK2 (~DIRECT_LINE_MASK2 & ~RESERVED_BITS_MASK2)

static void stm32l4x5_exti_reset_hold(Object *obj)
{
    Stm32l4x5ExtiState *s = STM32L4X5_EXTI(obj);

    s->imr[0] = DIRECT_LINE_MASK1;
    s->emr[0] = 0x00000000;
    s->rtsr[0] = 0x00000000;
    s->ftsr[0] = 0x00000000;
    s->swier[0] = 0x00000000;
    s->pr[0] = 0x00000000;

    s->imr[1] = DIRECT_LINE_MASK2;
    s->emr[1] = 0x00000000;
    s->rtsr[1] = 0x00000000;
    s->ftsr[1] = 0x00000000;
    s->swier[1] = 0x00000000;
    s->pr[1] = 0x00000000;
}

static void stm32l4x5_exti_set_irq(void *opaque, int irq, int level)
{
    Stm32l4x5ExtiState *s = opaque;
    const unsigned n = irq >= 32;
    const int oirq = irq;

    trace_stm32l4x5_exti_set_irq(irq, level);

    if (irq >= 32) {
        /* Shift the value to enable access in x2 registers. */
        irq -= 32;
    }

    /* If the interrupt is masked, pr won't be raised */
    if (!((1 << irq) & s->imr[n])) {
        return;
    }

    if (((1 << irq) & s->rtsr[n]) && level) {
        /* Rising Edge */
        s->pr[n] |= 1 << irq;
        qemu_irq_pulse(s->irq[oirq]);
    } else if (((1 << irq) & s->ftsr[n]) && !level) {
        /* Falling Edge */
        s->pr[n] |= 1 << irq;
        qemu_irq_pulse(s->irq[oirq]);
    }

}

static uint64_t stm32l4x5_exti_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Stm32l4x5ExtiState *s = opaque;
    uint32_t r = 0;
    const unsigned n = addr >= EXTI_IMR2;

    switch (addr) {
    case EXTI_IMR1:
    case EXTI_IMR2:
        r = s->imr[n];
        break;
    case EXTI_EMR1:
    case EXTI_EMR2:
        r = s->emr[n];
        break;
    case EXTI_RTSR1:
    case EXTI_RTSR2:
        r = s->rtsr[n];
        break;
    case EXTI_FTSR1:
    case EXTI_FTSR2:
        r = s->ftsr[n];
        break;
    case EXTI_SWIER1:
    case EXTI_SWIER2:
        r = s->swier[n];
        break;
    case EXTI_PR1:
    case EXTI_PR2:
        r = s->pr[n];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32L4X5_exti_read: Bad offset 0x%x\n", (int)addr);
        break;
    }

    trace_stm32l4x5_exti_read(addr, r);

    return r;
}

static void stm32l4x5_exti_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Stm32l4x5ExtiState *s = opaque;
    const uint32_t value = (uint32_t)val64;

    trace_stm32l4x5_exti_write(addr, value);

    switch (addr) {
    case EXTI_IMR1:
        s->imr[0] = value;
        return;
    case EXTI_EMR1:
        s->emr[0] = value;
        return;
    case EXTI_RTSR1:
        s->rtsr[0] = value & ~DIRECT_LINE_MASK1;
        return;
    case EXTI_FTSR1:
        s->ftsr[0] = value & ~DIRECT_LINE_MASK1;
        return;
    case EXTI_SWIER1: {
        const uint32_t set1 = value & ~DIRECT_LINE_MASK1;
        const uint32_t pend1 = set1 & ~s->swier[0] & s->imr[0] & ~s->pr[0];
        s->swier[0] = set1;
        s->pr[0] |= pend1;
        for (int i = 0; i < 32; i++) {
            if (pend1 & (1 << i)) {
                qemu_irq_pulse(s->irq[i]);
            }
        }
        return;
    }
    case EXTI_PR1: {
        const uint32_t cleared1 = s->pr[0] & value & ~DIRECT_LINE_MASK1;
        /* This bit is cleared by writing a 1 to it */
        s->pr[0] &= ~cleared1;
        /* Software triggered interrupts are cleared as well */
        s->swier[0] &= ~cleared1;
        return;
    }
    case EXTI_IMR2:
        s->imr[1] = value & ~RESERVED_BITS_MASK2;
        return;
    case EXTI_EMR2:
        s->emr[1] = value & ~RESERVED_BITS_MASK2;
        return;
    case EXTI_RTSR2:
        s->rtsr[1] = value & ACTIVABLE_MASK2;
        return;
    case EXTI_FTSR2:
        s->ftsr[1] = value & ACTIVABLE_MASK2;
        return;
    case EXTI_SWIER2: {
        const uint32_t set2 = value & ACTIVABLE_MASK2;
        const uint32_t pend2 = set2 & ~s->swier[1] & s->imr[1] & ~s->pr[1];
        s->swier[1] = set2;
        s->pr[1] |= pend2;
        for (int i = 0; i < 8; i++) {
            if (pend2 & (1 << i)) {
                qemu_irq_pulse(s->irq[32 + i]);
            }
        }
        return;
    }
    case EXTI_PR2: {
        const uint32_t cleared = s->pr[1] & value & ACTIVABLE_MASK2;
        /* This bit is cleared by writing a 1 to it */
        s->pr[1] &= ~cleared;
        /* Software triggered interrupts are cleared as well */
        s->swier[1] &= ~cleared;
        return;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32L4X5_exti_write: Bad offset 0x%x\n", (int)addr);
    }
}

static const MemoryRegionOps stm32l4x5_exti_ops = {
    .read = stm32l4x5_exti_read,
    .write = stm32l4x5_exti_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.unaligned = false,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void stm32l4x5_exti_init(Object *obj)
{
    Stm32l4x5ExtiState *s = STM32L4X5_EXTI(obj);
    int i;

    for (i = 0; i < EXTI_NUM_INTERRUPT_OUT_LINES; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_exti_ops, s,
                          TYPE_STM32L4X5_EXTI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(obj), stm32l4x5_exti_set_irq,
                      EXTI_NUM_GPIO_EVENT_IN_LINES);
}

static const VMStateDescription vmstate_stm32l4x5_exti = {
    .name = TYPE_STM32L4X5_EXTI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(imr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(emr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(rtsr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(ftsr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(swier, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(pr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32l4x5_exti_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_stm32l4x5_exti;
    rc->phases.hold = stm32l4x5_exti_reset_hold;
}

static const TypeInfo stm32l4x5_exti_types[] = {
    {
        .name          = TYPE_STM32L4X5_EXTI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Stm32l4x5ExtiState),
        .instance_init = stm32l4x5_exti_init,
        .class_init    = stm32l4x5_exti_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_exti_types)
