/*
 * Broadcom BCM283x ARM timer variant based on ARM SP804
 * Copyright (c) 2019, Mark <alnyan@airmail.cc>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/timer/bcm283x_timer.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"

#define TIMER_CTRL_32BIT            (1 << 1)
#define TIMER_CTRL_DIV1             (0 << 2)
#define TIMER_CTRL_DIV16            (1 << 2)
#define TIMER_CTRL_DIV256           (2 << 2)
#define TIMER_CTRL_IE               (1 << 5)
#define TIMER_CTRL_ENABLE           (1 << 7)
#define TIMER_CTRL_ENABLE_FREECNTR  (1 << 9)

/* BCM283x's implementation of SP804 ARM timer */

static void bcm283x_timer_set_irq(void *opaque, int irq, int level)
{
    BCM283xTimerState *s = BCM283xTimer(opaque);

    s->int_level = level;
    qemu_set_irq(s->irq, s->int_level);
}

static void bcm283x_timer_update(BCM283xTimerState *s)
{
    if (s->int_level && (s->control & TIMER_CTRL_IE)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void bcm283x_timer_tick(void *opaque)
{
    BCM283xTimerState *s = BCM283xTimer(opaque);
    s->int_level = 1;
    bcm283x_timer_update(s);
}

static void bcm283x_free_timer_tick(void *opaque)
{
    /* Do nothing */
}

static uint64_t bcm283x_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM283xTimerState *s = BCM283xTimer(opaque);

    switch (offset >> 2) {
    case 0: /* Load register */
    case 6: /* Reload register */
        return s->limit;
    case 1: /* Value register */
        return ptimer_get_count(s->timer);
    case 2: /* Control register */
        return s->control;
    case 3: /* IRQ clear/ACK register */
        /*
         * The register is write-only,
         * but returns reverse "ARMT" string bytes
         */
        return 0x544D5241;
    case 4: /* RAW IRQ register */
        return s->int_level;
    case 5: /* Masked IRQ register */
        if ((s->control & TIMER_CTRL_IE) == 0) {
            return 0;
        }
        return s->int_level;
    case 8: /* Free-running counter */
        return ptimer_get_count(s->free_timer);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int) offset);
        return 0;
    }
}

static void bcm283x_timer_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    BCM283xTimerState *s = BCM283xTimer(opaque);
    uint32_t freq, freecntr_freq;

    switch (offset >> 2) {
    case 0: /* Load register */
        s->limit = value;
        ptimer_set_limit(s->timer, s->limit, 1);
        break;
    case 1: /* Value register */
        /* Read only */
        break;
    case 2: /* Control register */
        if (s->control & TIMER_CTRL_ENABLE) {
            ptimer_stop(s->timer);
        }

        s->control = value;

        /* Configure SP804 */
        freq = BCM283x_SYSTEM_CLOCK_FREQ / (s->prediv + 1);
        /* Set pre-scaler */
        switch ((value >> 2) & 3) {
        case 1: /* 16 */
            freq >>= 4;
            break;
        case 2: /* 256 */
            freq >>= 8;
            break;
        }
        ptimer_set_limit(s->timer, s->limit, s->control & TIMER_CTRL_ENABLE);
        ptimer_set_freq(s->timer, freq);

        /* Configure free-running counter */
        freecntr_freq = BCM283x_SYSTEM_CLOCK_FREQ /
            (1 + ((value >> 16) & 0xFF));
        if (s->control & TIMER_CTRL_32BIT) {
            ptimer_set_limit(s->free_timer, 0xFFFFFFFF,
                    s->control & TIMER_CTRL_ENABLE_FREECNTR);
        } else {
            ptimer_set_limit(s->free_timer, 0xFFFF,
                    s->control & TIMER_CTRL_ENABLE_FREECNTR);
        }
        ptimer_set_freq(s->free_timer, freecntr_freq);

        if (s->control & TIMER_CTRL_ENABLE) {
            ptimer_run(s->timer, 0);
        } else {
            ptimer_stop(s->free_timer);
        }

        if (s->control & TIMER_CTRL_ENABLE_FREECNTR) {
            ptimer_run(s->free_timer, 0);
        } else {
            ptimer_stop(s->free_timer);
        }
        break;
    case 3: /* IRQ clear/ACK register */
        s->int_level = 0;
        break;
    case 6: /* Reload register */
        s->limit = value;
        ptimer_set_limit(s->timer, s->limit, 0);
        break;
    case 7: /* Pre-divider register */
        s->prediv = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int) offset);
        break;
    }

    bcm283x_timer_update(s);
}

static const MemoryRegionOps bcm283x_timer_ops = {
    .read = bcm283x_timer_read,
    .write = bcm283x_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static const VMStateDescription vmstate_bcm283x_timer = {
    .name = "bcm283x_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, BCM283xTimerState),
        VMSTATE_UINT32(limit, BCM283xTimerState),
        VMSTATE_UINT32(int_level, BCM283xTimerState),
        VMSTATE_PTIMER(timer, BCM283xTimerState),
        VMSTATE_PTIMER(free_timer, BCM283xTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm283x_timer_init(Object *obj)
{
    BCM283xTimerState *s = BCM283xTimer(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bcm283x_timer_ops, s,
            TYPE_BCM283xTimer, 0x100);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void bcm283x_timer_reset(DeviceState *dev)
{
    BCM283xTimerState *s = BCM283xTimer(dev);

    s->limit = 0;
    s->int_level = 0;
    s->control = TIMER_CTRL_IE | (0x0E << 16);
    s->prediv = 0x7D;

    /*
     * Stop the timers.
     * No need to update freqs/limits as this will automatically be done once
     * the system writes control register.
     */
    ptimer_stop(s->timer);
    ptimer_stop(s->free_timer);
}

static void bcm283x_timer_realize(DeviceState *dev, Error **errp)
{
    BCM283xTimerState *s = BCM283xTimer(dev);
    QEMUBH *bh;

    s->limit = 0;
    s->int_level = 0;
    s->control = TIMER_CTRL_IE | (0x0E << 16);
    s->prediv = 0x7D;

    /* Create a regular SP804 timer */
    bh = qemu_bh_new(bcm283x_timer_tick, s);
    s->timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    s->irq = qemu_allocate_irq(bcm283x_timer_set_irq, s, 0);

    /* Create a free-running timer */
    bh = qemu_bh_new(bcm283x_free_timer_tick, s);
    s->free_timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);

    vmstate_register(NULL, -1, &vmstate_bcm283x_timer, s);
}

static void bcm283x_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = bcm283x_timer_realize;
    k->vmsd = &vmstate_bcm283x_timer;
    k->reset = bcm283x_timer_reset;
}

static const TypeInfo bcm283x_timer_info = {
    .name           = TYPE_BCM283xTimer,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(BCM283xTimerState),
    .instance_init  = bcm283x_timer_init,
    .class_init     = bcm283x_timer_class_init
};

static void bcm283x_timer_register_types(void)
{
    type_register_static(&bcm283x_timer_info);
}

type_init(bcm283x_timer_register_types)
