/*
 * ARM PrimeCell Timer modules.
 *
 * Copyright (c) 2005-2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"

/* Common timer implementation.  */

#define TIMER_CTRL_ONESHOT      (1 << 0)
#define TIMER_CTRL_32BIT        (1 << 1)
#define TIMER_CTRL_DIV1         (0 << 2)
#define TIMER_CTRL_DIV16        (1 << 2)
#define TIMER_CTRL_DIV256       (2 << 2)
#define TIMER_CTRL_IE           (1 << 5)
#define TIMER_CTRL_PERIODIC     (1 << 6)
#define TIMER_CTRL_ENABLE       (1 << 7)

#define TYPE_ARM_TIMER "arm-timer"
OBJECT_DECLARE_SIMPLE_TYPE(ArmTimerState, ARM_TIMER)

struct ArmTimerState {
    SysBusDevice parent_obj;
    ptimer_state *timer;
    uint32_t control;
    uint32_t limit;
    uint32_t freq;
    int int_level;
    qemu_irq irq;
};

/* Check all active timers, and schedule the next timer interrupt.  */

static void arm_timer_update(ArmTimerState *s)
{
    /* Update interrupts.  */
    if (s->int_level && (s->control & TIMER_CTRL_IE)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t arm_timer_read(void *opaque, hwaddr offset)
{
    ArmTimerState *s = opaque;

    switch (offset >> 2) {
    case 0: /* TimerLoad */
    case 6: /* TimerBGLoad */
        return s->limit;
    case 1: /* TimerValue */
        return ptimer_get_count(s->timer);
    case 2: /* TimerControl */
        return s->control;
    case 4: /* TimerRIS */
        return s->int_level;
    case 5: /* TimerMIS */
        if ((s->control & TIMER_CTRL_IE) == 0)
            return 0;
        return s->int_level;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int)offset);
        return 0;
    }
}

/*
 * Reset the timer limit after settings have changed.
 * May only be called from inside a ptimer transaction block.
 */
static void arm_timer_recalibrate(ArmTimerState *s, int reload)
{
    uint32_t limit;

    if ((s->control & (TIMER_CTRL_PERIODIC | TIMER_CTRL_ONESHOT)) == 0) {
        /* Free running.  */
        if (s->control & TIMER_CTRL_32BIT)
            limit = 0xffffffff;
        else
            limit = 0xffff;
    } else {
          /* Periodic.  */
          limit = s->limit;
    }
    ptimer_set_limit(s->timer, limit, reload);
}

static void arm_timer_write(void *opaque, hwaddr offset,
                            uint32_t value)
{
    ArmTimerState *s = opaque;
    int freq;

    switch (offset >> 2) {
    case 0: /* TimerLoad */
        s->limit = value;
        ptimer_transaction_begin(s->timer);
        arm_timer_recalibrate(s, 1);
        ptimer_transaction_commit(s->timer);
        break;
    case 1: /* TimerValue */
        /* ??? Linux seems to want to write to this readonly register.
           Ignore it.  */
        break;
    case 2: /* TimerControl */
        ptimer_transaction_begin(s->timer);
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            ptimer_stop(s->timer);
        }
        s->control = value;
        freq = s->freq;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch ((value >> 2) & 3) {
        case 1: freq >>= 4; break;
        case 2: freq >>= 8; break;
        }
        arm_timer_recalibrate(s, s->control & TIMER_CTRL_ENABLE);
        ptimer_set_freq(s->timer, freq);
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Restart the timer if still enabled.  */
            ptimer_run(s->timer, (s->control & TIMER_CTRL_ONESHOT) != 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case 3: /* TimerIntClr */
        s->int_level = 0;
        break;
    case 6: /* TimerBGLoad */
        s->limit = value;
        ptimer_transaction_begin(s->timer);
        arm_timer_recalibrate(s, 0);
        ptimer_transaction_commit(s->timer);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, (int)offset);
    }
    arm_timer_update(s);
}

static void arm_timer_tick(void *opaque)
{
    ArmTimerState *s = opaque;
    s->int_level = 1;
    arm_timer_update(s);
}

static const VMStateDescription vmstate_arm_timer = {
    .name = "arm_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, ArmTimerState),
        VMSTATE_UINT32(limit, ArmTimerState),
        VMSTATE_INT32(int_level, ArmTimerState),
        VMSTATE_PTIMER(timer, ArmTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void arm_timer_reset(DeviceState *dev)
{
    ArmTimerState *s = ARM_TIMER(dev);

    s->control = TIMER_CTRL_IE;
}

static void arm_timer_init(Object *obj)
{
    ArmTimerState *s = ARM_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->timer = ptimer_init(arm_timer_tick, s, PTIMER_POLICY_LEGACY);

    sysbus_init_irq(sbd, &s->irq);
}

static void arm_timer_finalize(Object *obj)
{
    ArmTimerState *s = ARM_TIMER(obj);

    ptimer_free(s->timer);
}

static Property arm_timer_properties[] = {
    DEFINE_PROP_UINT32("freq", ArmTimerState, freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_timer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = arm_timer_reset;
    dc->vmsd = &vmstate_arm_timer;
    device_class_set_props(dc, arm_timer_properties);
}

/*
 * ARM PrimeCell SP804 dual timer module.
 * Docs at
 * https://developer.arm.com/documentation/ddi0271/latest/
 */

#define TYPE_SP804 "sp804"
OBJECT_DECLARE_SIMPLE_TYPE(SP804State, SP804)

struct SP804State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ArmTimerState timer[2];
    int level[2];
    qemu_irq irq;
};

static const uint8_t sp804_ids[] = {
    /* Timer ID */
    0x04, 0x18, 0x14, 0,
    /* PrimeCell ID */
    0xd, 0xf0, 0x05, 0xb1
};

/* Merge the IRQs from the two component devices.  */
static void sp804_set_irq(void *opaque, int irq, int level)
{
    SP804State *s = opaque;

    s->level[irq] = level;
    qemu_set_irq(s->irq, s->level[0] || s->level[1]);
}

static uint64_t sp804_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    SP804State *s = opaque;

    if (offset < 0x20) {
        return arm_timer_read(&s->timer[0], offset);
    }
    if (offset < 0x40) {
        return arm_timer_read(&s->timer[1], offset - 0x20);
    }

    /* TimerPeriphID */
    if (offset >= 0xfe0 && offset <= 0xffc) {
        return sp804_ids[(offset - 0xfe0) >> 2];
    }

    switch (offset) {
    /* Integration Test control registers, which we won't support */
    case 0xf00: /* TimerITCR */
    case 0xf04: /* TimerITOP (strictly write only but..) */
        qemu_log_mask(LOG_UNIMP,
                      "%s: integration test registers unimplemented\n",
                      __func__);
        return 0;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Bad offset %x\n", __func__, (int)offset);
    return 0;
}

static void sp804_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    SP804State *s = opaque;

    if (offset < 0x20) {
        arm_timer_write(&s->timer[0], offset, value);
        return;
    }

    if (offset < 0x40) {
        arm_timer_write(&s->timer[1], offset - 0x20, value);
        return;
    }

    /* Technically we could be writing to the Test Registers, but not likely */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %x\n",
                  __func__, (int)offset);
}

static const MemoryRegionOps sp804_ops = {
    .read = sp804_read,
    .write = sp804_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_sp804 = {
    .name = "sp804",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_ARRAY(level, SP804State, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void sp804_init(Object *obj)
{
    SP804State *s = SP804(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &sp804_ops, s,
                          "sp804", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in_named(DEVICE(obj), sp804_set_irq,
                            "timer-in", ARRAY_SIZE(s->timer));
    for (unsigned i = 0; i < ARRAY_SIZE(s->timer); i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i], TYPE_ARM_TIMER);
    }
}

static void sp804_realize(DeviceState *dev, Error **errp)
{
    SP804State *s = SP804(dev);

    for (unsigned i = 0; i < ARRAY_SIZE(s->timer); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer[i]), 0,
                           qdev_get_gpio_in_named(dev, "timer-in", i));
    }
}

static Property sp804_properties[] = {
    DEFINE_PROP_UINT32("freq0", SP804State, timer[0].freq, 1000000),
    DEFINE_PROP_UINT32("freq1", SP804State, timer[1].freq, 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void sp804_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = sp804_realize;
    device_class_set_props(k, sp804_properties);
    k->vmsd = &vmstate_sp804;
}

/* Integrator/CP timer module.  */

#define TYPE_INTEGRATOR_PIT "integrator_pit"
OBJECT_DECLARE_SIMPLE_TYPE(IntegratorPitState, INTEGRATOR_PIT)

struct IntegratorPitState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ArmTimerState timer[3];
    qemu_irq irq[3];
};

static uint64_t icp_pit_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    IntegratorPitState *s = opaque;
    int n;

    /* ??? Don't know the PrimeCell ID for this device.  */
    n = offset >> 8;
    if (n > 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad timer %d\n", __func__, n);
        return 0;
    }

    return arm_timer_read(&s->timer[n], offset & 0xff);
}

static void icp_pit_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    IntegratorPitState *s = opaque;
    int n;

    n = offset >> 8;
    if (n > 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad timer %d\n", __func__, n);
        return;
    }

    arm_timer_write(&s->timer[n], offset & 0xff, value);
}

static const MemoryRegionOps icp_pit_ops = {
    .read = icp_pit_read,
    .write = icp_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void icp_pit_fwd_irq(void *opaque, int n, int level)
{
    IntegratorPitState *s = opaque;

    qemu_set_irq(s->irq[n], level);
}

static void icp_pit_init(Object *obj)
{
    static const uint32_t tmr_freq[] = {
        /* Timer 0 runs at the system clock speed (40MHz).  */
        40000000,
        /* The other two timers run at 1MHz.  */
        1000000, 1000000
    };
    IntegratorPitState *s = INTEGRATOR_PIT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in_named(DEVICE(obj), icp_pit_fwd_irq,
                            "timer-in", ARRAY_SIZE(s->timer));

    for (unsigned i = 0; i < ARRAY_SIZE(s->timer); i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i], TYPE_ARM_TIMER);
        qdev_prop_set_uint32(DEVICE(&s->timer[i]), "freq", tmr_freq[i]);
        sysbus_init_irq(dev, &s->irq[i]);
    }

    memory_region_init_io(&s->iomem, obj, &icp_pit_ops, s,
                          "icp_pit", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    /* This device has no state to save/restore.  The component timers will
       save themselves.  */
}

static void icp_pit_realize(DeviceState *dev, Error **errp)
{
    IntegratorPitState *s = INTEGRATOR_PIT(dev);

    for (unsigned i = 0; i < ARRAY_SIZE(s->timer); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer[i]), 0,
                           qdev_get_gpio_in_named(dev, "timer-in", i));
    }
}

static void icp_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = icp_pit_realize;
}

static const TypeInfo arm_timer_types[] = {
    {
        .name           = TYPE_ARM_TIMER,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(ArmTimerState),
        .instance_init  = arm_timer_init,
        .instance_finalize = arm_timer_finalize,
        .class_init     = arm_timer_class_init,
    }, {
        .name           = TYPE_INTEGRATOR_PIT,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(IntegratorPitState),
        .instance_init  = icp_pit_init,
        .class_init     = icp_pit_class_init,

    }, {
        .name           = TYPE_SP804,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(SP804State),
        .instance_init  = sp804_init,
        .class_init     = sp804_class_init,
    }
};

DEFINE_TYPES(arm_timer_types)
