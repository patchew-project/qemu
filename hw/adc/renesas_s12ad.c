/*
 * Renesas RX65N 12-bit Successive Approximation A/D Converter (S12AD)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Section 40
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/adc/renesas_s12ad.h"
#include "migration/vmstate.h"

/*
 * Register offsets from device base (0x00089000).
 * Reference: RX65N Group Hardware Manual Table 40.1
 */
REG16(ADCSR,   0x00)
  FIELD(ADCSR, DBLANS,  0, 5)   /* double trigger channel */
  FIELD(ADCSR, GBADIE,  6, 1)   /* group B scan end interrupt enable */
  FIELD(ADCSR, DBLE,    7, 1)   /* double trigger enable */
  FIELD(ADCSR, EXTRG,   8, 1)   /* trigger select (0=sync, 1=async) */
  FIELD(ADCSR, TRGE,    9, 1)   /* trigger start enable */
  FIELD(ADCSR, ADHSC,  10, 1)   /* high-speed conversion */
  FIELD(ADCSR, ADIE,   12, 1)   /* group A end interrupt enable */
  FIELD(ADCSR, ADCS,   13, 2)   /* scan mode */
  FIELD(ADCSR, ADST,   15, 1)   /* start conversion (W1 to start, hw clears) */

REG16(ADANSA0, 0x04)   /* channel select 0 (AN000-AN015) */
REG16(ADANSA1, 0x06)   /* channel select 1 (AN016-AN020) */
REG16(ADADS0,  0x08)   /* average enable 0 */
REG16(ADADS1,  0x0A)   /* average enable 1 */
REG8(ADADC,    0x0C)   /* average count */
REG16(ADCER,   0x0E)   /* control extended */
REG16(ADSTRGR, 0x10)   /* start trigger select */
REG16(ADEXICR, 0x12)   /* extended input */
REG16(ADANSB0, 0x14)   /* group B channel select 0 */
REG16(ADANSB1, 0x16)   /* group B channel select 1 */
REG16(ADDBLDR, 0x18)   /* double-buffer result */
REG16(ADRD,    0x1E)   /* temperature sensor / internal Vref result */
/* ADDR0–ADDR15 at offsets 0x20–0x3E (16-bit each) */
#define A_ADDR_BASE   0x20
#define A_ADDR_END    0x3E   /* inclusive */
#define A_ADSAMPR     0x63
#define A_ADSAM       0x6e

/*
 * Simulated conversion time: ~1 µs (typical for 12-bit at 60 MHz PCLK).
 * Real hardware takes ~1–17 µs depending on mode and clock.
 */
#define CONV_TIME_NS   1000

/* Fixed mid-scale return value for all channels: 0x0800 = 2048 (12-bit) */
#define DUMMY_RESULT   0x0800

static void s12ad_conv_done(void *opaque)
{
    RX65NS12ADState *s = opaque;

    /* Clear ADST, set results */
    s->adcsr = FIELD_DP16(s->adcsr, ADCSR, ADST, 0);

    /* Fill result registers with mid-scale dummy value */
    for (int i = 0; i < S12AD_NR_CHANNELS; i++) {
        if (s->adansa0 & (1u << i)) {
            s->addr[i] = DUMMY_RESULT << 4; /* left-justified by default */
        }
    }
    s->adrd = DUMMY_RESULT << 4;

    /* Fire end-of-scan interrupt if enabled */
    if (FIELD_EX16(s->adcsr, ADCSR, ADIE)) {
        qemu_irq_pulse(s->irq[S12AD_IRQ_S12ADI]);
    }
    if (FIELD_EX16(s->adcsr, ADCSR, GBADIE)) {
        qemu_irq_pulse(s->irq[S12AD_IRQ_GBADI]);
    }
}

static uint64_t s12ad_read(void *opaque, hwaddr addr, unsigned size)
{
    RX65NS12ADState *s = opaque;

    if (addr >= 0x70 && addr + size <= 0xf0) {
        uint8_t *p = &s->ext_regs[addr - 0x70];

        return size == 1 ? *p : lduw_le_p(p);
    }

    if (addr >= A_ADDR_BASE && addr <= A_ADDR_END) {
        int ch = (addr - A_ADDR_BASE) / 2;
        return (ch < S12AD_NR_CHANNELS) ? s->addr[ch] : UINT64_MAX;
    }

    switch (addr) {
    case A_ADCSR:
        return s->adcsr;
    case A_ADANSA0:
        return s->adansa0;
    case A_ADANSA1:
        return s->adansa1;
    case A_ADADS0:
        return s->adads0;
    case A_ADADS1:
        return s->adads1;
    case A_ADADC:
        return s->adadc;
    case A_ADCER:
        return s->adcer;
    case A_ADSTRGR:
        return s->adstrgr;
    case A_ADEXICR:
        return s->adexicr;
    case A_ADANSB0:
        return s->adansb0;
    case A_ADANSB1:
        return s->adansb1;
    case A_ADDBLDR:
        return 0;
    case 0x1a:  /* ADTSDR */
    case 0x1c:  /* ADOCDR */
        return DUMMY_RESULT << 4;
    case A_ADRD:
        return s->adrd;
    case A_ADSAMPR:
        return s->adsampr == 3 ? 1 : 0;
    case A_ADSAM:
        return s->adsampr == 3 ? s->adsam : 0;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_s12ad: read 0x%" HWADDR_PRIX
                      " not implemented\n",
                      addr);
        return UINT64_MAX;
    }
}

static void s12ad_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RX65NS12ADState *s = opaque;

    if (addr >= 0x70 && addr + size <= 0xf0) {
        uint8_t *p = &s->ext_regs[addr - 0x70];

        if (size == 1) {
            *p = val;
        } else {
            stw_le_p(p, val);
        }
        return;
    }

    switch (addr) {
    case A_ADCSR:
        s->adcsr = val & ~FIELD_DP16(0, ADCSR, ADST, 1); /* keep ADST for now */
        s->adcsr |= (val & FIELD_DP16(0, ADCSR, ADST, 1));
        if (FIELD_EX16(val, ADCSR, ADST)) {
            /* Software-triggered start: schedule conversion completion */
            timer_mod(&s->conv_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + CONV_TIME_NS);
        }
        break;
    case A_ADANSA0:
        s->adansa0  = val;
        break;
    case A_ADANSA1:
        s->adansa1  = val;
        break;
    case A_ADADS0:
        s->adads0   = val;
        break;
    case A_ADADS1:
        s->adads1   = val;
        break;
    case A_ADADC:
        s->adadc    = val;
        break;
    case A_ADCER:
        s->adcer    = val;
        break;
    case A_ADSTRGR:
        s->adstrgr  = val;
        break;
    case A_ADEXICR:
        s->adexicr  = val;
        break;
    case A_ADANSB0:
        s->adansb0  = val;
        break;
    case A_ADANSB1:
        s->adansb1  = val;
        break;
    case A_ADSAMPR:
        s->adsampr = val & 0x3;
        break;
    case A_ADSAM:
        if (s->adsampr == 3) {
            s->adsam = val & 0x20;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_s12ad: write 0x%" HWADDR_PRIX
                      " not implemented\n",
                      addr);
        break;
    }
}

static const MemoryRegionOps s12ad_ops = {
    .read  = s12ad_read,
    .write = s12ad_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void s12ad_reset(DeviceState *dev)
{
    RX65NS12ADState *s = RENESAS_S12AD(dev);
    int i;

    s->adcsr   = 0x0000;
    s->adansa0 = 0x0000;
    s->adansa1 = 0x0000;
    s->adads0  = 0x0000;
    s->adads1  = 0x0000;
    s->adadc   = 0x00;
    s->adcer   = 0x0000;
    s->adstrgr = 0x0000;
    s->adexicr = 0x0000;
    s->adansb0 = 0x0000;
    s->adansb1 = 0x0000;
    s->adrd    = 0x0000;
    s->adsam   = 0x0000;
    s->adsampr = 0x00;
    memset(s->ext_regs, 0, sizeof(s->ext_regs));
    for (i = 0; i < S12AD_NR_CHANNELS; i++) {
        s->addr[i] = 0x0000;
    }
    timer_del(&s->conv_timer);
}

static void s12ad_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RX65NS12ADState *s = RENESAS_S12AD(obj);
    int i;

    /* Includes the conversion-time registers at offsets 0x63 and 0x6e. */
    memory_region_init_io(&s->memory, obj, &s12ad_ops, s,
                          "renesas-s12ad", 0xf0);
    sysbus_init_mmio(d, &s->memory);

    for (i = 0; i < S12AD_NR_IRQ; i++) {
        sysbus_init_irq(d, &s->irq[i]);
    }
    timer_init_ns(&s->conv_timer, QEMU_CLOCK_VIRTUAL, s12ad_conv_done, s);
}

static const VMStateDescription vmstate_s12ad = {
    .name = "renesas-s12ad",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(adcsr,   RX65NS12ADState),
        VMSTATE_UINT16(adansa0, RX65NS12ADState),
        VMSTATE_UINT16(adansa1, RX65NS12ADState),
        VMSTATE_UINT16(adads0,  RX65NS12ADState),
        VMSTATE_UINT16(adads1,  RX65NS12ADState),
        VMSTATE_UINT8(adadc,    RX65NS12ADState),
        VMSTATE_UINT16(adcer,   RX65NS12ADState),
        VMSTATE_UINT16(adstrgr, RX65NS12ADState),
        VMSTATE_UINT16(adexicr, RX65NS12ADState),
        VMSTATE_UINT16(adansb0, RX65NS12ADState),
        VMSTATE_UINT16(adansb1, RX65NS12ADState),
        VMSTATE_UINT16(adrd,    RX65NS12ADState),
        VMSTATE_UINT16(adsam,   RX65NS12ADState),
        VMSTATE_UINT8(adsampr,  RX65NS12ADState),
        VMSTATE_UINT8_ARRAY(ext_regs, RX65NS12ADState, 0x80),
        VMSTATE_UINT16_ARRAY(addr, RX65NS12ADState, S12AD_NR_CHANNELS),
        VMSTATE_TIMER(conv_timer, RX65NS12ADState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property s12ad_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RX65NS12ADState, input_freq, 0),
};

static void s12ad_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_s12ad;
    device_class_set_legacy_reset(dc, s12ad_reset);
    device_class_set_props(dc, s12ad_properties);
}

static const TypeInfo s12ad_info = {
    .name          = TYPE_RENESAS_S12AD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RX65NS12ADState),
    .instance_init = s12ad_init,
    .class_init    = s12ad_class_init,
};

static void s12ad_register_types(void)
{
    type_register_static(&s12ad_info);
}

type_init(s12ad_register_types)
