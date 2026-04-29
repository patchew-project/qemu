/*
 * PIC32MK ADCHS — High-Speed ADC peripheral emulation
 * Datasheet: DS60001519E, §22
 *
 * Emulates the 12-bit pipelined SAR ADC found on PIC32MK GPK/MCM devices.
 * 46 data channels (0–27, 33–41, 45–53), 7 ADC modules (0–5, 7).
 *
 * Key features implemented:
 *   - All SFR registers with SET/CLR/INV sub-register support
 *   - Software-triggered conversions (GSWTRG, GLSWTRG, RQCNVRT)
 *   - Instant conversion model (no timing delays)
 *   - Host-injectable analog values via QOM properties (adc-ch0 … adc-ch53)
 *   - End-of-Scan (EOS) + main ADC interrupt outputs to EVIC
 *   - BGVRRDY and WKRDYx bits always report ready (emulation shortcut)
 *
 * Stubs (LOG_UNIMP): digital filters, digital comparators, DMA, early IRQ.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/pic32mk.h"
#include "hw/mips/pic32mk_adchs.h"

/*
 * Channel validity table — true for channels present on PIC32MK1024MCM100
 * Channels: 0–27, 33–41, 45–53.  Missing: 28–32, 42–44.
 * -----------------------------------------------------------------------
 */

static bool adchs_channel_valid(unsigned ch)
{
    if (ch <= 27) {
        return true;
    }
    if (ch >= 33 && ch <= 41) {
        return true;
    }
    if (ch >= 45 && ch <= 53) {
        return true;
    }
    return false;
}

/*
 * Map channel number to its data-ready IRQ number.
 * DATA0=106, DATA1=107, …, DATA27=133, (gap), DATA33=139, …, DATA41=147,
 * (gap), DATA45=151, …, DATA53=159.
 */
static int G_GNUC_UNUSED adchs_channel_irq(unsigned ch)
{
    if (ch <= 27) {
        return PIC32MK_IRQ_ADC_DATA0 + (int)ch;
    }
    if (ch >= 33 && ch <= 41) {
        return 139 + (int)(ch - 33);
    }
    if (ch >= 45 && ch <= 53) {
        return 151 + (int)(ch - 45);
    }
    return -1;
}

/*
 * SET/CLR/INV helper (PIC32MK convention: +0=REG, +4=CLR, +8=SET, +C=INV)
 * -----------------------------------------------------------------------
 */

static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

/*
 * Conversion engine
 * -----------------------------------------------------------------------
 */

/*
 * Perform conversion for a single channel: copy the host-injected analog
 * value into the data register, set the data-ready status bit, and fire
 * the per-channel IRQ if the global interrupt-enable bit is set.
 */
static void adchs_convert_channel(PIC32MKADCHSState *s, unsigned ch)
{
    if (ch >= PIC32MK_ADC_MAX_CH || !adchs_channel_valid(ch)) {
        return;
    }

    /* Store 12-bit result in data register (upper bits zero) */
    s->adcdata[ch] = s->analog_input[ch] & 0xFFF;

    /* Set data-ready status bit */
    if (ch < 32) {
        s->adcdstat[0] |= (1u << ch);
    } else {
        s->adcdstat[1] |= (1u << (ch - 32));
    }

    /* Fire per-channel IRQ if ADCGIRQEN bit is set */
    bool girq_en;
    if (ch < 32) {
        girq_en = !!(s->adcgirqen[0] & (1u << ch));
    } else {
        girq_en = !!(s->adcgirqen[1] & (1u << (ch - 32)));
    }

    if (girq_en) {
        /*
         * Per-channel data IRQs go directly to EVIC IFS bits.
         * We pulse the main ADC IRQ output for simplicity — the EVIC
         * will latch IFS[irq_num].  Individual data channel IRQ wiring
         * can be extended if firmware requires it.
         */
        qemu_irq_pulse(s->irq_main);
    }
}

/*
 * Scan conversion: iterate over all channels enabled in ADCCSS1/2,
 * convert each, then signal End-of-Scan.
 */
static void adchs_scan_convert(PIC32MKADCHSState *s)
{
    /* Only convert if module is ON */
    if (!(s->adccon1 & PIC32MK_ADCCON1_ON)) {
        return;
    }

    /* Scan CSS1 (channels 0–31) */
    for (int ch = 0; ch < 32; ch++) {
        if (s->adccss[0] & (1u << ch)) {
            adchs_convert_channel(s, ch);
        }
    }

    /* Scan CSS2 (channels 32–53) */
    for (int ch = 0; ch < 22; ch++) {
        if (s->adccss[1] & (1u << ch)) {
            adchs_convert_channel(s, ch + 32);
        }
    }

    /* Signal End-of-Scan */
    qemu_irq_pulse(s->irq_eos);
}

/*
 * Register dispatch
 * -----------------------------------------------------------------------
 */

/*
 * Map an offset (base, i.e. aligned to 0x10) to a register pointer.
 * Returns NULL for offsets that are read-only, data, or unimplemented.
 */
static uint32_t *adchs_find_reg(PIC32MKADCHSState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_ADCCON1:
        return &s->adccon1;
    case PIC32MK_ADCCON2:
        return &s->adccon2;
    case PIC32MK_ADCCON3:
        return &s->adccon3;
    case PIC32MK_ADCTRGMODE:
        return &s->adctrgmode;

    case PIC32MK_ADCIMCON1:
        return &s->adcimcon[0];
    case PIC32MK_ADCIMCON2:
        return &s->adcimcon[1];
    case PIC32MK_ADCIMCON3:
        return &s->adcimcon[2];
    case PIC32MK_ADCIMCON4:
        return &s->adcimcon[3];

    case PIC32MK_ADCGIRQEN1:
        return &s->adcgirqen[0];
    case PIC32MK_ADCGIRQEN2:
        return &s->adcgirqen[1];

    case PIC32MK_ADCCSS1:
        return &s->adccss[0];
    case PIC32MK_ADCCSS2:
        return &s->adccss[1];

    case PIC32MK_ADCDSTAT1:
        return &s->adcdstat[0];
    case PIC32MK_ADCDSTAT2:
        return &s->adcdstat[1];

    case PIC32MK_ADCCMPEN1:
        return &s->adccmpen[0];
    case PIC32MK_ADCCMPEN2:
        return &s->adccmpen[1];
    case PIC32MK_ADCCMPEN3:
        return &s->adccmpen[2];
    case PIC32MK_ADCCMPEN4:
        return &s->adccmpen[3];

    case PIC32MK_ADCCMP1:
        return &s->adccmp[0];
    case PIC32MK_ADCCMP2:
        return &s->adccmp[1];
    case PIC32MK_ADCCMP3:
        return &s->adccmp[2];
    case PIC32MK_ADCCMP4:
        return &s->adccmp[3];

    case PIC32MK_ADCFLTR1:
        return &s->adcfltr[0];
    case PIC32MK_ADCFLTR2:
        return &s->adcfltr[1];
    case PIC32MK_ADCFLTR3:
        return &s->adcfltr[2];
    case PIC32MK_ADCFLTR4:
        return &s->adcfltr[3];

    case PIC32MK_ADCTRG1:
        return &s->adctrg[0];
    case PIC32MK_ADCTRG2:
        return &s->adctrg[1];
    case PIC32MK_ADCTRG3:
        return &s->adctrg[2];
    case PIC32MK_ADCTRG4:
        return &s->adctrg[3];
    case PIC32MK_ADCTRG5:
        return &s->adctrg[4];
    case PIC32MK_ADCTRG6:
        return &s->adctrg[5];
    case PIC32MK_ADCTRG7:
        return &s->adctrg[6];

    case PIC32MK_ADCCMPCON1:
        return &s->adccmpcon[0];
    case PIC32MK_ADCCMPCON2:
        return &s->adccmpcon[1];
    case PIC32MK_ADCCMPCON3:
        return &s->adccmpcon[2];
    case PIC32MK_ADCCMPCON4:
        return &s->adccmpcon[3];

    case PIC32MK_ADCBASE:
        return &s->adcbase;
    case PIC32MK_ADCTRGSNS:
        return &s->adctrgsns;

    case PIC32MK_ADC0TIME:
        return &s->adctime[0];
    case PIC32MK_ADC1TIME:
        return &s->adctime[1];
    case PIC32MK_ADC2TIME:
        return &s->adctime[2];
    case PIC32MK_ADC3TIME:
        return &s->adctime[3];
    case PIC32MK_ADC4TIME:
        return &s->adctime[4];
    case PIC32MK_ADC5TIME:
        return &s->adctime[5];

    case PIC32MK_ADCEIEN1:
        return &s->adceien[0];
    case PIC32MK_ADCEIEN2:
        return &s->adceien[1];
    case PIC32MK_ADCEISTAT1:
        return &s->adceistat[0];
    case PIC32MK_ADCEISTAT2:
        return &s->adceistat[1];

    case PIC32MK_ADCANCON:
        return &s->adcancon;

    case PIC32MK_ADC0CFG:
        return &s->adccfg[0];
    case PIC32MK_ADC1CFG:
        return &s->adccfg[1];
    case PIC32MK_ADC2CFG:
        return &s->adccfg[2];
    case PIC32MK_ADC3CFG:
        return &s->adccfg[3];
    case PIC32MK_ADC4CFG:
        return &s->adccfg[4];
    case PIC32MK_ADC5CFG:
        return &s->adccfg[5];
    case PIC32MK_ADC6CFG:
        return &s->adccfg[6];
    case PIC32MK_ADC7CFG:
        return &s->adccfg[7];

    case PIC32MK_ADCSYSCFG0:
        return &s->adcsyscfg[0];
    case PIC32MK_ADCSYSCFG1:
        return &s->adcsyscfg[1];

    default:
        return NULL;
    }
}

/*
 * MMIO read
 * -----------------------------------------------------------------------
 */

static uint64_t adchs_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKADCHSState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    /*
     * ADCCON2: always report reference voltage ready and no fault.
     * Firmware polls these bits during initialization.
     */
    if (base == PIC32MK_ADCCON2) {
        return (s->adccon2 | PIC32MK_ADCCON2_BGVRRDY)
               & ~PIC32MK_ADCCON2_REFFLT;
    }

    /*
     * ADCANCON: mirror ANENx bits into WKRDYx positions.
     * Firmware enables ANENx then polls WKRDYx until ready.
     */
    if (base == PIC32MK_ADCANCON) {
        uint32_t anen = s->adcancon & 0xFFu;
        return (s->adcancon & ~0xFF00u) | (anen << 8);
    }

    /* ADCDATA registers (0x600–0x950 range, stride 0x10) */
    if (base >= PIC32MK_ADCDATA_BASE &&
        base < PIC32MK_ADCDATA_BASE + PIC32MK_ADC_MAX_CH * PIC32MK_ADCDATA_STRIDE) {
        unsigned ch = (base - PIC32MK_ADCDATA_BASE) / PIC32MK_ADCDATA_STRIDE;
        if (ch < PIC32MK_ADC_MAX_CH && adchs_channel_valid(ch)) {
            /* Auto-clear data-ready status on read */
            if (ch < 32) {
                s->adcdstat[0] &= ~(1u << ch);
            } else {
                s->adcdstat[1] &= ~(1u << (ch - 32));
            }
            return s->adcdata[ch];
        }
    }

    /* Standard register dispatch */
    uint32_t *reg = adchs_find_reg(s, base);
    if (reg) {
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_adchs: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

/*
 * MMIO write
 * -----------------------------------------------------------------------
 */

static void adchs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKADCHSState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;

    /* ADCDATA registers are read-only from firmware perspective */
    if (base >= PIC32MK_ADCDATA_BASE &&
        base < PIC32MK_ADCDATA_BASE + PIC32MK_ADC_MAX_CH * PIC32MK_ADCDATA_STRIDE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_adchs: write to read-only ADCDATA @ 0x%04"
                      HWADDR_PRIx "\n", addr);
        return;
    }

    /* Special handling for ADCCON3 — trigger bits */
    if (base == PIC32MK_ADCCON3) {
        uint32_t old = s->adccon3;
        apply_sci(&s->adccon3, (uint32_t)val, sub);

        /* GSWTRG: global software trigger → scan conversion */
        if ((s->adccon3 & PIC32MK_ADCCON3_GSWTRG) &&
            !(old & PIC32MK_ADCCON3_GSWTRG)) {
            adchs_scan_convert(s);
            /* GSWTRG is self-clearing */
            s->adccon3 &= ~PIC32MK_ADCCON3_GSWTRG;
        }

        /* GLSWTRG: global level software trigger → also scan */
        if ((s->adccon3 & PIC32MK_ADCCON3_GLSWTRG) &&
            !(old & PIC32MK_ADCCON3_GLSWTRG)) {
            adchs_scan_convert(s);
        }

        /* RQCNVRT: request single-channel conversion */
        if ((s->adccon3 & PIC32MK_ADCCON3_RQCNVRT) &&
            !(old & PIC32MK_ADCCON3_RQCNVRT)) {
            unsigned ch = s->adccon3 & PIC32MK_ADCCON3_ADINSEL_MASK;
            adchs_convert_channel(s, ch);
            /* RQCNVRT is self-clearing */
            s->adccon3 &= ~PIC32MK_ADCCON3_RQCNVRT;
        }
        return;
    }

    /* Standard register dispatch */
    uint32_t *reg = adchs_find_reg(s, base);
    if (reg) {
        apply_sci(reg, (uint32_t)val, sub);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_adchs: unimplemented write @ 0x%04" HWADDR_PRIx
                  " = 0x%08" PRIx64 "\n",
                  addr, val);
}

static const MemoryRegionOps adchs_ops = {
    .read       = adchs_read,
    .write      = adchs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * QOM properties — host-side analog value injection
 * -----------------------------------------------------------------------
 */

static void adchs_prop_ch_get(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PIC32MKADCHSState *s = PIC32MK_ADCHS(obj);
    unsigned ch = (unsigned)(uintptr_t)opaque;
    int64_t val = s->analog_input[ch];
    visit_type_int(v, name, &val, errp);
}

static void adchs_prop_ch_set(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PIC32MKADCHSState *s = PIC32MK_ADCHS(obj);
    unsigned ch = (unsigned)(uintptr_t)opaque;
    int64_t val;

    if (!visit_type_int(v, name, &val, errp)) {
        return;
    }
    if (val < 0 || val > 4095) {
        error_setg(errp, "adc-ch%u value must be 0–4095 (12-bit)", ch);
        return;
    }
    s->analog_input[ch] = (uint16_t)val;
    s->adcdata[ch]      = (uint16_t)(val & 0xFFF);
}

static void adchs_prop_data_get(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    PIC32MKADCHSState *s = PIC32MK_ADCHS(obj);
    unsigned ch = (unsigned)(uintptr_t)opaque;
    int64_t val = s->adcdata[ch];
    visit_type_int(v, name, &val, errp);
}

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_adchs_reset(DeviceState *dev)
{
    PIC32MKADCHSState *s = PIC32MK_ADCHS(dev);

    s->adccon1    = 0;
    s->adccon2    = 0;
    s->adccon3    = 0;
    s->adctrgmode = 0;

    memset(s->adcimcon, 0, sizeof(s->adcimcon));
    memset(s->adcgirqen, 0, sizeof(s->adcgirqen));
    memset(s->adccss, 0, sizeof(s->adccss));
    memset(s->adcdstat, 0, sizeof(s->adcdstat));
    memset(s->adccmpen, 0, sizeof(s->adccmpen));
    memset(s->adccmp, 0, sizeof(s->adccmp));
    memset(s->adccmpcon, 0, sizeof(s->adccmpcon));
    memset(s->adcfltr, 0, sizeof(s->adcfltr));
    memset(s->adctrg, 0, sizeof(s->adctrg));
    s->adctrgsns = 0;
    memset(s->adctime, 0, sizeof(s->adctime));
    memset(s->adceien, 0, sizeof(s->adceien));
    memset(s->adceistat, 0, sizeof(s->adceistat));
    s->adcancon  = 0;
    s->adcbase   = 0;
    memset(s->adccfg, 0, sizeof(s->adccfg));
    memset(s->adcsyscfg, 0, sizeof(s->adcsyscfg));
    memset(s->adcdata, 0, sizeof(s->adcdata));
    /* analog_input[] is NOT reset — host injections persist across resets */
}

static void pic32mk_adchs_init(Object *obj)
{
    PIC32MKADCHSState *s = PIC32MK_ADCHS(obj);

    memory_region_init_io(&s->mr, obj, &adchs_ops, s,
                          TYPE_PIC32MK_ADCHS, PIC32MK_ADC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    /* IRQ outputs: 0=EOS, 1=main */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_eos);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_main);

    /*
     * QOM properties for host-side analog value injection.
     * adc-ch<N>   — int r/w — set the 12-bit analog value for channel N
     * adc-data<N> — int r/o — read the last conversion result for channel N
     */
    for (unsigned ch = 0; ch < PIC32MK_ADC_MAX_CH; ch++) {
        if (!adchs_channel_valid(ch)) {
            continue;
        }

        char name_ch[16], name_data[16];
        snprintf(name_ch, sizeof(name_ch), "adc-ch%u", ch);
        snprintf(name_data, sizeof(name_data), "adc-data%u", ch);

        object_property_add(obj, name_ch, "int",
                            adchs_prop_ch_get,
                            adchs_prop_ch_set,
                            NULL, (void *)(uintptr_t)ch);

        object_property_add(obj, name_data, "int",
                            adchs_prop_data_get,
                            NULL,   /* read-only */
                            NULL, (void *)(uintptr_t)ch);
    }
}

static void pic32mk_adchs_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_adchs_reset);
}

static const TypeInfo pic32mk_adchs_info = {
    .name          = TYPE_PIC32MK_ADCHS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKADCHSState),
    .instance_init = pic32mk_adchs_init,
    .class_init    = pic32mk_adchs_class_init,
};

static void pic32mk_adchs_register_types(void)
{
    type_register_static(&pic32mk_adchs_info);
}

type_init(pic32mk_adchs_register_types)
