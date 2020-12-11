/*
 * Nuvoton NPCM7xx ADC Module
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "hw/adc/npcm7xx_adc.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/units.h"

/* 32-bit register indices. */
enum NPCM7xxADCRegisters {
    NPCM7XX_ADC_CON,
    NPCM7XX_ADC_DATA,
    NPCM7XX_ADC_REGS_END,
};

/* Register field definitions. */
#define NPCM7XX_ADC_CON_MUX(rv) extract32(rv, 24, 4)
#define NPCM7XX_ADC_CON_INT_EN  BIT(21)
#define NPCM7XX_ADC_CON_REFSEL  BIT(19)
#define NPCM7XX_ADC_CON_INT     BIT(18)
#define NPCM7XX_ADC_CON_EN      BIT(17)
#define NPCM7XX_ADC_CON_RST     BIT(16)
#define NPCM7XX_ADC_CON_CONV    BIT(14)
#define NPCM7XX_ADC_CON_DIV(rv) extract32(rv, 1, 8)

#define NPCM7XX_ADC_MAX_RESULT      1023
#define NPCM7XX_ADC_DEFAULT_IREF    2000000
#define NPCM7XX_ADC_CONV_CYCLES     20
#define NPCM7XX_ADC_RESET_CYCLES    10
#define NPCM7XX_ADC_R0_INPUT        500000
#define NPCM7XX_ADC_R1_INPUT        1500000

static void npcm7xx_adc_reset(NPCM7xxADCState *s)
{
    timer_del(&s->conv_timer);
    timer_del(&s->reset_timer);
    s->con = 0x000c0001;
    s->data = 0x00000000;
}

static uint32_t npcm7xx_adc_convert(uint32_t input, uint32_t ref)
{
    uint32_t result;

    result = input * (NPCM7XX_ADC_MAX_RESULT + 1) / ref;
    if (result > NPCM7XX_ADC_MAX_RESULT) {
        result = NPCM7XX_ADC_MAX_RESULT;
    }

    return result;
}

static uint32_t npcm7xx_adc_prescaler(NPCM7xxADCState *s)
{
    return 2 * (NPCM7XX_ADC_CON_DIV(s->con) + 1);
}

static void npcm7xx_adc_start_timer(Clock *clk, QEMUTimer *timer,
        uint32_t cycles, uint32_t prescaler)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t freq = clock_get_hz(clk);
    int64_t ns;

    ns = (NANOSECONDS_PER_SECOND * cycles * prescaler / freq);
    ns += now;
    timer_mod(timer, ns);
}

static void npcm7xx_adc_start_reset(NPCM7xxADCState *s)
{
    uint32_t prescaler = npcm7xx_adc_prescaler(s);

    npcm7xx_adc_start_timer(s->clock, &s->reset_timer, NPCM7XX_ADC_RESET_CYCLES,
            prescaler);
}

static void npcm7xx_adc_start_convert(NPCM7xxADCState *s)
{
    uint32_t prescaler = npcm7xx_adc_prescaler(s);

    npcm7xx_adc_start_timer(s->clock, &s->conv_timer, NPCM7XX_ADC_CONV_CYCLES,
            prescaler);
}

static void npcm7xx_adc_reset_done(void *opaque)
{
    NPCM7xxADCState *s = opaque;

    npcm7xx_adc_reset(s);
}

static void npcm7xx_adc_convert_done(void *opaque)
{
    NPCM7xxADCState *s = opaque;
    uint32_t input = NPCM7XX_ADC_CON_MUX(s->con);
    uint32_t ref = (s->con & NPCM7XX_ADC_CON_REFSEL)
        ? s->iref : s->vref;

    g_assert(input < NPCM7XX_ADC_NUM_INPUTS);
    s->data = npcm7xx_adc_convert(s->adci[input], ref);
    if (s->con & NPCM7XX_ADC_CON_INT_EN) {
        s->con |= NPCM7XX_ADC_CON_INT;
        qemu_irq_raise(s->irq);
    }
    s->con &= ~NPCM7XX_ADC_CON_CONV;
}

static void npcm7xx_adc_calibrate(NPCM7xxADCState *adc)
{
    adc->calibration_r_values[0] = npcm7xx_adc_convert(NPCM7XX_ADC_R0_INPUT,
            adc->iref);
    adc->calibration_r_values[1] = npcm7xx_adc_convert(NPCM7XX_ADC_R1_INPUT,
            adc->iref);
}

static void npcm7xx_adc_write_con(NPCM7xxADCState *s, uint32_t new_con)
{
    uint32_t old_con = s->con;

    /* Write ADC_INT to 1 to clear it */
    if (new_con & NPCM7XX_ADC_CON_INT) {
        new_con &= ~NPCM7XX_ADC_CON_INT;
    } else if (old_con & NPCM7XX_ADC_CON_INT) {
        new_con |= NPCM7XX_ADC_CON_INT;
    }

    s->con = new_con;

    if (s->con & NPCM7XX_ADC_CON_RST) {
        if (!(old_con & NPCM7XX_ADC_CON_RST)) {
            npcm7xx_adc_start_reset(s);
        }
    } else {
        timer_del(&s->reset_timer);
    }

    if ((s->con & NPCM7XX_ADC_CON_EN)) {
        if (s->con & NPCM7XX_ADC_CON_CONV) {
            if (!(old_con & NPCM7XX_ADC_CON_CONV)) {
                npcm7xx_adc_start_convert(s);
            }
        } else {
            timer_del(&s->conv_timer);
        }
    }
}

static uint64_t npcm7xx_adc_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value = 0;
    NPCM7xxADCState *s = opaque;
    hwaddr reg = offset / sizeof(uint32_t);

    switch (reg) {
    case NPCM7XX_ADC_CON:
        value = s->con;
        break;

    case NPCM7XX_ADC_DATA:
        value = s->data;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    return value;
}

static void npcm7xx_adc_write(void *opaque, hwaddr offset, uint64_t v,
        unsigned size)
{
    NPCM7xxADCState *s = opaque;
    hwaddr reg = offset / sizeof(uint32_t);

    switch (reg) {
    case NPCM7XX_ADC_CON:
        npcm7xx_adc_write_con(s, v);
        break;

    case NPCM7XX_ADC_DATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is read-only\n",
                      __func__, offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid offset 0x%04" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

}

static const struct MemoryRegionOps npcm7xx_adc_ops = {
    .read       = npcm7xx_adc_read,
    .write      = npcm7xx_adc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_adc_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxADCState *s = NPCM7XX_ADC(obj);

    npcm7xx_adc_reset(s);
}

static void npcm7xx_adc_hold_reset(Object *obj)
{
    NPCM7xxADCState *s = NPCM7XX_ADC(obj);

    qemu_irq_lower(s->irq);
}

static void npcm7xx_adc_init(Object *obj)
{
    NPCM7xxADCState *s = NPCM7XX_ADC(obj);
    SysBusDevice *sbd = &s->parent;
    int i;

    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->conv_timer, QEMU_CLOCK_VIRTUAL,
            npcm7xx_adc_convert_done, s);
    timer_init_ns(&s->reset_timer, QEMU_CLOCK_VIRTUAL,
            npcm7xx_adc_reset_done, s);
    memory_region_init_io(&s->iomem, obj, &npcm7xx_adc_ops, s,
                          TYPE_NPCM7XX_ADC, 4 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
    s->clock = qdev_init_clock_in(DEVICE(s), "clock", NULL, NULL);

    for (i = 0; i < NPCM7XX_ADC_NUM_INPUTS; ++i) {
        object_property_add_uint32_ptr(obj, "adci[*]",
                &s->adci[i], OBJ_PROP_FLAG_WRITE);
    }
    object_property_add_uint32_ptr(obj, "vref",
            &s->vref, OBJ_PROP_FLAG_WRITE);
    npcm7xx_adc_calibrate(s);
}

static const VMStateDescription vmstate_npcm7xx_adc = {
    .name = "npcm7xx-adc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(conv_timer, NPCM7xxADCState),
        VMSTATE_TIMER(reset_timer, NPCM7xxADCState),
        VMSTATE_UINT32(con, NPCM7xxADCState),
        VMSTATE_UINT32(data, NPCM7xxADCState),
        VMSTATE_CLOCK(clock, NPCM7xxADCState),
        VMSTATE_UINT32_ARRAY(adci, NPCM7xxADCState, NPCM7XX_ADC_NUM_INPUTS),
        VMSTATE_UINT32(vref, NPCM7xxADCState),
        VMSTATE_UINT32(iref, NPCM7xxADCState),
        VMSTATE_UINT16_ARRAY(calibration_r_values, NPCM7xxADCState,
                NPCM7XX_ADC_NUM_CALIB),
        VMSTATE_END_OF_LIST(),
    },
};

static Property npcm7xx_timer_properties[] = {
    DEFINE_PROP_UINT32("iref", NPCM7xxADCState, iref, NPCM7XX_ADC_DEFAULT_IREF),
    DEFINE_PROP_END_OF_LIST(),
};

static void npcm7xx_adc_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NPCM7xx ADC Module";
    dc->vmsd = &vmstate_npcm7xx_adc;
    rc->phases.enter = npcm7xx_adc_enter_reset;
    rc->phases.hold = npcm7xx_adc_hold_reset;

    device_class_set_props(dc, npcm7xx_timer_properties);
}

static const TypeInfo npcm7xx_adc_info = {
    .name               = TYPE_NPCM7XX_ADC,
    .parent             = TYPE_SYS_BUS_DEVICE,
    .instance_size      = sizeof(NPCM7xxADCState),
    .class_init         = npcm7xx_adc_class_init,
    .instance_init      = npcm7xx_adc_init,
};

static void npcm7xx_adc_register_types(void)
{
    type_register_static(&npcm7xx_adc_info);
}

type_init(npcm7xx_adc_register_types);
