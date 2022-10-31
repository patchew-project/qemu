/*
 * IMX EPIT Timer
 *
 * Copyright (c) 2008 OK Labs
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Peter Chubb
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This code is licensed under GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/timer/imx_epit.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/imx_ccm.h"
#include "qemu/module.h"
#include "qemu/log.h"

#ifndef DEBUG_IMX_EPIT
#define DEBUG_IMX_EPIT 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_EPIT) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_EPIT, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_epit_reg_name(uint32_t reg)
{
    switch (reg) {
    case 0:
        return "CR";
    case 1:
        return "SR";
    case 2:
        return "LR";
    case 3:
        return "CMP";
    case 4:
        return "CNT";
    default:
        return "[?]";
    }
}

/*
 * Exact clock frequencies vary from board to board.
 * These are typical.
 */
static const IMXClk imx_epit_clocks[] =  {
    CLK_NONE,      /* 00 disabled */
    CLK_IPG,       /* 01 ipg_clk, ~532MHz */
    CLK_IPG_HIGH,  /* 10 ipg_clk_highfreq */
    CLK_32k,       /* 11 ipg_clk_32k -- ~32kHz */
};

static uint32_t imx_epit_get_freq(IMXEPITState *s)
{
    uint32_t clksrc = extract32(s->cr, CR_CLKSRC_SHIFT, CR_CLKSRC_BITS);
    uint32_t prescaler = 1 + extract32(s->cr, CR_PRESCALE_SHIFT, CR_PRESCALE_BITS);
    uint32_t f_in = imx_ccm_get_clock_frequency(s->ccm, imx_epit_clocks[clksrc]);
    return f_in / prescaler;
}

static uint64_t imx_epit_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);
    uint32_t reg_value = 0;

    switch (offset >> 2) {
    case 0: /* Control Register */
        reg_value = s->cr;
        break;

    case 1: /* Status Register */
        reg_value = s->sr;
        break;

    case 2: /* LR - ticks*/
        reg_value = s->lr;
        break;

    case 3: /* CMP */
        reg_value = s->cmp;
        break;

    case 4: /* CNT */
        reg_value = ptimer_get_count(s->timer_reload);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);
        break;
    }

    DPRINTF("(%s) = 0x%08x\n", imx_epit_reg_name(offset >> 2), reg_value);

    return reg_value;
}

/*
 * Must be called from a ptimer_transaction_begin/commit block for
 * s->timer_cmp, but outside of a transaction block of s->timer_reload,
 * so the proper counter value is read.
 */
static void imx_epit_update_compare_timer(IMXEPITState *s)
{
    int is_oneshot = 0;

    /*
     * The compare time will only be active when the EPIT timer is enabled
     * (CR_EN), the compare interrupt generation is enabled (CR_OCIEN) and
     *  the input clock if not off.
     */
    uint32_t freq = imx_epit_get_freq(s);
    if (!freq || ((s->cr & (CR_EN | CR_OCIEN)) != (CR_EN | CR_OCIEN))) {
        ptimer_stop(s->timer_cmp);
        return;
    }

    /* calculate the next timeout for the compare timer. */
    uint64_t tmp = ptimer_get_count(s->timer_reload);
    uint64_t max = (s->cr & CR_RLD) ? EPIT_TIMER_MAX : s->lr;
    if (s->cmp <= tmp) {
        /* fire in this round */
        tmp -= s->cmp;
        /* if the reload value is less than the compare value, the timer will
         * only fire once
         */
        is_oneshot = (s->cmp > max);
    } else {
        /*
         * fire after a reload - but only if the reload value is equal
         * or higher than the compare value.
         */
        if (s->cmp > max) {
            ptimer_stop(s->timer_cmp);
            return;
        }
        tmp += max - s->cmp;
    }

    /* re-initialize the compare timer and run it */
    ptimer_set_count(s->timer_cmp, tmp);
    ptimer_run(s->timer_cmp, is_oneshot);
}

static void imx_epit_write_cr(IMXEPITState *s, uint32_t value)
{
    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);

    uint32_t oldcr = s->cr;
    s->cr = (value & ~CR_SWR) & 0x03ffffff;

    if (value & CR_SWR) {
        /*
         * Soft reset doesn't touch some bits, just a hard reset clears all
         * of them. Clearing CLKSRC disables the input clock, which will
         * happen when we re-init of the timer frequency below.
         */
        s->cr &= (CR_EN|CR_ENMOD|CR_STOPEN|CR_DOZEN|CR_WAITEN|CR_DBGEN);
        /*
         * We have applied the new CR value and then cleared most bits,
         * thus some bits from the write request are now lost. The TRM
         * is not clear about the behavior, maybe these bits are to be
         * applied after the reset (e.g. for selecting a new clock
         * source). However, it seem this is undefined behavior and a
         * it's assumed a reset does not try to do anything else.
         */
        s->sr = 0;
        s->lr = EPIT_TIMER_MAX;
        s->cmp = 0;
        /* turn interrupt off since SR and the OCIEN bit is cleared */
        qemu_irq_lower(s->irq);
        /* reset timer limits, set timer values to the limits */
        ptimer_set_limit(s->timer_cmp, EPIT_TIMER_MAX, 1);
        ptimer_set_limit(s->timer_reload, EPIT_TIMER_MAX, 1);
    }

    /* re-initialize frequency, or turn off timers if input clock is off */
    uint32_t freq = imx_epit_get_freq(s);
    if (freq) {
        DPRINTF("Setting ptimer frequency to %u\n", freq);
        ptimer_set_freq(s->timer_reload, freq);
        ptimer_set_freq(s->timer_cmp, freq);
    }

    if (!freq || !(s->cr & CR_EN)) {
        /*
         * The EPIT timer is effectively disabled if it is not enabled or
         * the input clock is off. In this case we can stop the ptimers.
         */
        ptimer_stop(s->timer_cmp);
        ptimer_stop(s->timer_reload);
    } else {
        /* The EPIT timer is active. */
        if (!(oldcr & CR_EN)) {
            /* The EPI timer has just been enabled, initialize and start it. */
            if (s->cr & CR_ENMOD) {
                uint64_t limit = (s->cr & CR_RLD) ? s->lr : EPIT_TIMER_MAX;
                /* set new limit and also set timer to this value right now */
                ptimer_set_limit(s->timer_reload, limit, 1);
                ptimer_set_limit(s->timer_cmp, limit, 1);
            }
            ptimer_run(s->timer_reload, 0);
        }
    }
    /*
     * Commit the change to s->timer_reload, so it can propagate and the
     * updated value will be read in imx_epit_update_compare_timer(),
     * Otherwise a stale value will be seen and the compare interrupt is
     * set up wrongly.
     */
    ptimer_transaction_commit(s->timer_reload);
    imx_epit_update_compare_timer(s);

    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write_sr(IMXEPITState *s, uint32_t value)
{
    /* writing 1 to OCIF clears the OCIF bit */
    if (value & 0x01) {
        s->sr = 0;
        qemu_irq_lower(s->irq);
    }
}

static void imx_epit_write_lr(IMXEPITState *s, uint32_t value)
{
    s->lr = value;

    ptimer_transaction_begin(s->timer_cmp);
    ptimer_transaction_begin(s->timer_reload);
    if (s->cr & CR_RLD) {
        /* Also set the limit if the LRD bit is set */
        /* If IOVW bit is set then set the timer value */
        ptimer_set_limit(s->timer_reload, s->lr, s->cr & CR_IOVW);
        ptimer_set_limit(s->timer_cmp, s->lr, 0);
    } else if (s->cr & CR_IOVW) {
        /* If IOVW bit is set then set the timer value */
        ptimer_set_count(s->timer_reload, s->lr);
    }
    /*
     * Commit the change to s->timer_reload, so it can propagate and the
     * updated value will be read in imx_epit_update_compare_timer(),
     * Otherwise a stale value will be seen and the compare interrupt is
     * set up wrongly.
     */
    ptimer_transaction_commit(s->timer_reload);
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write_cmp(IMXEPITState *s, uint32_t value)
{
    s->cmp = value;

    ptimer_transaction_begin(s->timer_cmp);
    imx_epit_update_compare_timer(s);
    ptimer_transaction_commit(s->timer_cmp);
}

static void imx_epit_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    DPRINTF("(%s, value = 0x%08x)\n", imx_epit_reg_name(offset >> 2),
            (uint32_t)value);

    switch (offset >> 2) {
    case 0: /* CR */
        imx_epit_write_cr(s, (uint32_t)value);
        break;

    case 1: /* SR - ACK*/
        imx_epit_write_sr(s, (uint32_t)value);
        break;

    case 2: /* LR - set ticks */
        imx_epit_write_lr(s, (uint32_t)value);
        break;

    case 3: /* CMP */
        imx_epit_write_cmp(s, (uint32_t)value);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_EPIT, __func__, offset);

        break;
    }
}
static void imx_epit_cmp(void *opaque)
{
    IMXEPITState *s = IMX_EPIT(opaque);

    DPRINTF("sr was %d\n", s->sr);

    s->sr = 1;

    /*
     * An interrupt is generated only if both the peripheral is enabled and the
     * interrupt generation is enabled.
     */
    if ((s->cr & (CR_EN | CR_OCIEN)) == (CR_EN | CR_OCIEN)) {
        qemu_irq_raise(s->irq);
    }
}

static void imx_epit_reload(void *opaque)
{
    /* No action required on rollover of timer_reload */
}

static const MemoryRegionOps imx_epit_ops = {
    .read = imx_epit_read,
    .write = imx_epit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_imx_timer_epit = {
    .name = TYPE_IMX_EPIT,
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr, IMXEPITState),
        VMSTATE_UINT32(sr, IMXEPITState),
        VMSTATE_UINT32(lr, IMXEPITState),
        VMSTATE_UINT32(cmp, IMXEPITState),
        VMSTATE_PTIMER(timer_reload, IMXEPITState),
        VMSTATE_PTIMER(timer_cmp, IMXEPITState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx_epit_realize(DeviceState *dev, Error **errp)
{
    IMXEPITState *s = IMX_EPIT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DPRINTF("\n");

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &imx_epit_ops, s, TYPE_IMX_EPIT,
                          0x00001000);
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * The reload timer keeps running when the peripheral is enabled. It is a
     * kind of wall clock that does not generate any interrupts. The callback
     * needs to be provided, but it does nothing as the ptimer already supports
     * all necessary reloading functionality.
     */
    s->timer_reload = ptimer_init(imx_epit_reload, s, PTIMER_POLICY_LEGACY);

    /*
     * The compare timer is running only when the peripheral configuration is
     * in a state that will generate compare interrupts.
     */
    s->timer_cmp = ptimer_init(imx_epit_cmp, s, PTIMER_POLICY_LEGACY);
}

static void imx_epit_reset(DeviceState *dev)
{
    IMXEPITState *s = IMX_EPIT(dev);

    /* initialize CR and perform a software reset */
    s->cr = 0;
    imx_epit_write_cr(s, CR_SWR);
}

static void imx_epit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);

    dc->realize = imx_epit_realize;
    dc->reset = imx_epit_reset;
    dc->vmsd = &vmstate_imx_timer_epit;
    dc->desc = "i.MX periodic timer";
}

static const TypeInfo imx_epit_info = {
    .name = TYPE_IMX_EPIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXEPITState),
    .class_init = imx_epit_class_init,
};

static void imx_epit_register_types(void)
{
    type_register_static(&imx_epit_info);
}

type_init(imx_epit_register_types)
