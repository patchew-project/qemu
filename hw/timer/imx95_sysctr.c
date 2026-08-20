/*
 * NXP i.MX 95 System Counter (sysctr) timer
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The system counter is a free-running up-counter plus a compare block that
 * raises an interrupt when the counter reaches a programmed value. Linux uses
 * it as the tick BROADCAST clockevent: the imx95 idle state `cpu-pd-wait`
 * carries `local-timer-stop`, so a core entering cpuidle shuts down its
 * per-CPU arch timer and depends entirely on this counter's compare interrupt
 * to be woken. Modelling it as plain RAM (the previous stub) left idle cores
 * with no wake source -> RCU stalls -> the boot needed `cpuidle.off=1`. This
 * model gives a live counter + working compare IRQ so the broadcast-timer
 * wake path works (deep cpuidle still needs cpuidle.off=1 - see the docs).
 *
 * Register layout (from Linux drivers/clocksource/timer-imx-sysctr.c, the
 * imx95 quirk path which QEMU always takes - the IMX_SIP_GET_SOC_INFO SiP
 * SMC is unimplemented here, so the driver sets SYS_CTR_IMX95_QUIRK):
 *   - read frame  @ 0x20000: CNTCV_LO 0x20008 / CNTCV_HI 0x2000c (RO counter)
 *     (also exposed at 0x8 / 0xc for the non-quirk read path)
 *   - cmp frame   @ 0x10000: CMPCV_LO 0x10020 / CMPCV_HI 0x10024 (compare
 *     value, RW, read-back-verified by the driver) and CMPCR 0x1002c
 *     (control: EN = bit0; the driver acks the IRQ by clearing EN, which
 *     drops the status bit and negates the interrupt).
 *
 * Only the master compare channel + its single interrupt are modelled (the
 * DT exposes one IRQ; the broadcast framework needs only one channel).
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_SYSCTR "imx95.sysctr"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95SysctrState, IMX95_SYSCTR)

#define IMX95_SYSCTR_REG_SIZE   0x30000

/* The system counter reference is the 24 MHz oscillator (nxp,no-divider). */
#define IMX95_SYSCTR_FREQ_HZ    24000000ULL

/* Register offsets (see file header). */
#define SYSCTR_CNTCV_LO         0x8
#define SYSCTR_CNTCV_HI         0xc
#define SYSCTR_CMPCV_LO         0x10020
#define SYSCTR_CMPCV_HI         0x10024
#define SYSCTR_CMPCR            0x1002c
#define SYSCTR_CNTCV_LO_RD      0x20008
#define SYSCTR_CNTCV_HI_RD      0x2000c

#define SYSCTR_CMPCR_EN         0x1

struct IMX95SysctrState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;
    qemu_irq        irq;
    QEMUTimer       timer;

    uint32_t        cmpcv_lo;
    uint32_t        cmpcv_hi;
    uint32_t        cmpcr;
};

static uint64_t imx95_sysctr_count(void)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    IMX95_SYSCTR_FREQ_HZ, NANOSECONDS_PER_SECOND);
}

static uint64_t imx95_sysctr_cmpcv(IMX95SysctrState *s)
{
    return ((uint64_t)s->cmpcv_hi << 32) | s->cmpcv_lo;
}

/*
 * (Re)arm or disarm the compare. When the compare is enabled, schedule the
 * timer for the virtual-time instant the counter reaches CMPCV; a value
 * already in the past makes timer_mod fire at the next opportunity. When
 * disabled, cancel the timer and negate the interrupt (this is how the
 * driver's ISR acks: it clears EN).
 */
static void imx95_sysctr_update(IMX95SysctrState *s)
{
    if (s->cmpcr & SYSCTR_CMPCR_EN) {
        uint64_t deadline = muldiv64(imx95_sysctr_cmpcv(s),
                                     NANOSECONDS_PER_SECOND,
                                     IMX95_SYSCTR_FREQ_HZ);
        trace_imx95_sysctr_cmp(imx95_sysctr_cmpcv(s));
        timer_mod(&s->timer, deadline);
    } else {
        timer_del(&s->timer);
        qemu_set_irq(s->irq, 0);
    }
}

static void imx95_sysctr_timer_cb(void *opaque)
{
    IMX95SysctrState *s = opaque;

    if (s->cmpcr & SYSCTR_CMPCR_EN) {
        trace_imx95_sysctr_expire();
        qemu_set_irq(s->irq, 1);
    }
}

static uint64_t imx95_sysctr_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95SysctrState *s = opaque;
    uint64_t cnt = imx95_sysctr_count();

    switch (offset) {
    case SYSCTR_CNTCV_LO:
    case SYSCTR_CNTCV_LO_RD:
        return cnt & 0xffffffff;
    case SYSCTR_CNTCV_HI:
    case SYSCTR_CNTCV_HI_RD:
        return (cnt >> 32) & 0xffffffff;
    case SYSCTR_CMPCV_LO:
        return s->cmpcv_lo;
    case SYSCTR_CMPCV_HI:
        return s->cmpcv_hi;
    case SYSCTR_CMPCR:
        return s->cmpcr;
    default:
        return 0;
    }
}

static void imx95_sysctr_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    IMX95SysctrState *s = opaque;

    switch (offset) {
    case SYSCTR_CMPCV_LO:
        s->cmpcv_lo = value;
        imx95_sysctr_update(s);
        break;
    case SYSCTR_CMPCV_HI:
        s->cmpcv_hi = value;
        imx95_sysctr_update(s);
        break;
    case SYSCTR_CMPCR:
        s->cmpcr = value;
        imx95_sysctr_update(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps imx95_sysctr_ops = {
    .read = imx95_sysctr_read,
    .write = imx95_sysctr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_sysctr_reset_hold(Object *obj, ResetType type)
{
    IMX95SysctrState *s = IMX95_SYSCTR(obj);

    s->cmpcv_lo = 0;
    s->cmpcv_hi = 0;
    s->cmpcr = 0;
    timer_del(&s->timer);
    qemu_set_irq(s->irq, 0);
}

static void imx95_sysctr_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95SysctrState *s = IMX95_SYSCTR(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_sysctr_ops, s,
                          TYPE_IMX95_SYSCTR, IMX95_SYSCTR_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, imx95_sysctr_timer_cb, s);
}

static int imx95_sysctr_post_load(void *opaque, int version_id)
{
    /* Re-arm the compare (and re-raise a past-due IRQ) from restored state. */
    imx95_sysctr_update(opaque);
    return 0;
}

static const VMStateDescription vmstate_imx95_sysctr = {
    .name = TYPE_IMX95_SYSCTR,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = imx95_sysctr_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cmpcv_lo, IMX95SysctrState),
        VMSTATE_UINT32(cmpcv_hi, IMX95SysctrState),
        VMSTATE_UINT32(cmpcr, IMX95SysctrState),
        VMSTATE_TIMER(timer, IMX95SysctrState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_sysctr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_sysctr;
    rc->phases.hold = imx95_sysctr_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 system counter timer";
}

static const TypeInfo imx95_sysctr_info = {
    .name           = TYPE_IMX95_SYSCTR,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95SysctrState),
    .instance_init  = imx95_sysctr_init,
    .class_init     = imx95_sysctr_class_init,
};

static void imx95_sysctr_register_types(void)
{
    type_register_static(&imx95_sysctr_info);
}

type_init(imx95_sysctr_register_types)
