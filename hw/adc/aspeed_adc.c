/*
 * Aspeed ADC
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/adc/aspeed_adc.h"
#include "qapi/error.h"
#include "qemu/log.h"

#define ASPEED_ADC_ENGINE_CTRL                  0x00
#define  ASPEED_ADC_ENGINE_CH_EN_MASK           0xffff0000
#define   ASPEED_ADC_ENGINE_CH_EN(x)            ((BIT(x)) << 16)
#define  ASPEED_ADC_ENGINE_INIT                 BIT(8)
#define  ASPEED_ADC_ENGINE_AUTO_COMP            BIT(5)
#define  ASPEED_ADC_ENGINE_COMP                 BIT(4)
#define  ASPEED_ADC_ENGINE_MODE_MASK            0x0000000e
#define   ASPEED_ADC_ENGINE_MODE_OFF            (0b000 << 1)
#define   ASPEED_ADC_ENGINE_MODE_STANDBY        (0b001 << 1)
#define   ASPEED_ADC_ENGINE_MODE_NORMAL         (0b111 << 1)
#define  ASPEED_ADC_ENGINE_EN                   BIT(0)

#define ASPEED_ADC_L_MASK       ((1 << 10) - 1)
#define ASPEED_ADC_L(x)         ((x) & ASPEED_ADC_L_MASK)
#define ASPEED_ADC_H(x)         (((x) >> 16) & ASPEED_ADC_L_MASK)
#define ASPEED_ADC_LH_MASK      (ASPEED_ADC_L_MASK << 16 | ASPEED_ADC_L_MASK)

static inline uint32_t update_channels(uint32_t current)
{
    const uint32_t next = (current + 7) & 0x3ff;

    return (next << 16) | next;
}

static bool breaks_threshold(AspeedADCState *s, int ch_off)
{
    const uint32_t a = ASPEED_ADC_L(s->channels[ch_off]);
    const uint32_t a_lower = ASPEED_ADC_L(s->bounds[2 * ch_off]);
    const uint32_t a_upper = ASPEED_ADC_H(s->bounds[2 * ch_off]);
    const uint32_t b = ASPEED_ADC_H(s->channels[ch_off]);
    const uint32_t b_lower = ASPEED_ADC_L(s->bounds[2 * ch_off + 1]);
    const uint32_t b_upper = ASPEED_ADC_H(s->bounds[2 * ch_off + 1]);

    return ((a < a_lower || a > a_upper)) ||
           ((b < b_lower || b > b_upper));
}

static uint32_t read_channel_sample(AspeedADCState *s, int ch_off)
{
    uint32_t ret;

    /* Poor man's sampling */
    ret = s->channels[ch_off];
    s->channels[ch_off] = update_channels(s->channels[ch_off]);

    if (breaks_threshold(s, ch_off)) {
        qemu_irq_raise(s->irq);
    }

    return ret;
}

#define TO_INDEX(addr, base) (((addr) - (base)) >> 2)

static uint64_t aspeed_adc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    AspeedADCState *s = ASPEED_ADC(opaque);
    uint64_t ret;

    switch (addr) {
    case 0x00:
        ret = s->engine_ctrl;
        break;
    case 0x04:
        ret = s->irq_ctrl;
        break;
    case 0x08:
        ret = s->vga_detect_ctrl;
        break;
    case 0x0c:
        ret = s->adc_clk_ctrl;
        break;
    case 0x10 ... 0x2e:
        ret = read_channel_sample(s, TO_INDEX(addr, 0x10));
        break;
    case 0x30 ... 0x6e:
        ret = s->bounds[TO_INDEX(addr, 0x30)];
        break;
    case 0x70 ... 0xae:
        ret = s->hysteresis[TO_INDEX(addr, 0x70)];
        break;
    case 0xc0:
        ret = s->irq_src;
        break;
    case 0xc4:
        ret = s->comp_trim;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: addr: 0x%lx, size: %u\n", __func__, addr,
                size);
        ret = 0;
        break;
    }

    return ret;
}

static void aspeed_adc_write(void *opaque, hwaddr addr,
                       uint64_t val, unsigned int size)
{
    AspeedADCState *s = ASPEED_ADC(opaque);

    switch (addr) {
    case 0x00:
        {
            uint32_t init;

            init = !!(val & ASPEED_ADC_ENGINE_EN);
            init *= ASPEED_ADC_ENGINE_INIT;

            val &= ~ASPEED_ADC_ENGINE_INIT;
            val |= init;
        }

        val &= ~ASPEED_ADC_ENGINE_AUTO_COMP;
        s->engine_ctrl = val;

        break;
    case 0x04:
        s->irq_ctrl = val;
        break;
    case 0x08:
        s->vga_detect_ctrl = val;
        break;
    case 0x0c:
        s->adc_clk_ctrl = val;
        break;
    case 0x10 ... 0x2e:
        s->channels[TO_INDEX(addr, 0x10)] = val;
        break;
    case 0x30 ... 0x6e:
        s->bounds[TO_INDEX(addr, 0x30)] = (val & ASPEED_ADC_LH_MASK);
        break;
    case 0x70 ... 0xae:
        s->hysteresis[TO_INDEX(addr, 0x70)] =
            (val & (BIT(31) | ASPEED_ADC_LH_MASK));
        break;
    case 0xc0:
        s->irq_src = (val & 0xffff);
        break;
    case 0xc4:
        s->comp_trim = (val & 0xf);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: %lu\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps aspeed_adc_ops = {
    .read = aspeed_adc_read,
    .write = aspeed_adc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_adc_reset(DeviceState *dev)
{
    struct AspeedADCState *s = ASPEED_ADC(dev);

    s->engine_ctrl = 0;
    s->irq_ctrl = 0;
    s->vga_detect_ctrl = 0x0000000f;
    s->adc_clk_ctrl = 0x0000000f;
    memset(s->channels, 0, sizeof(s->channels));
    memset(s->bounds, 0, sizeof(s->bounds));
    memset(s->hysteresis, 0, sizeof(s->hysteresis));
    s->irq_src = 0;
    s->comp_trim = 0;
}

static void aspeed_adc_realize(DeviceState *dev, Error **errp)
{
    AspeedADCState *s = ASPEED_ADC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &aspeed_adc_ops, s,
            TYPE_ASPEED_ADC, 0x1000);

    sysbus_init_mmio(sbd, &s->mmio);
}

static const VMStateDescription vmstate_aspeed_adc = {
    .name = TYPE_ASPEED_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(engine_ctrl, AspeedADCState),
        VMSTATE_UINT32(irq_ctrl, AspeedADCState),
        VMSTATE_UINT32(vga_detect_ctrl, AspeedADCState),
        VMSTATE_UINT32(adc_clk_ctrl, AspeedADCState),
        VMSTATE_UINT32_ARRAY(channels, AspeedADCState,
                ASPEED_ADC_NR_CHANNELS / 2),
        VMSTATE_UINT32_ARRAY(bounds, AspeedADCState, ASPEED_ADC_NR_CHANNELS),
        VMSTATE_UINT32_ARRAY(hysteresis, AspeedADCState,
                ASPEED_ADC_NR_CHANNELS),
        VMSTATE_UINT32(irq_src, AspeedADCState),
        VMSTATE_UINT32(comp_trim, AspeedADCState),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_adc_realize;
    dc->reset = aspeed_adc_reset;
    dc->desc = "Aspeed Analog-to-Digital Converter",
    dc->vmsd = &vmstate_aspeed_adc;
}

static const TypeInfo aspeed_adc_info = {
    .name = TYPE_ASPEED_ADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedADCState),
    .class_init = aspeed_adc_class_init,
};

static void aspeed_adc_register_types(void)
{
    type_register_static(&aspeed_adc_info);
}

type_init(aspeed_adc_register_types);
