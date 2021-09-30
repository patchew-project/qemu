/*
 * Aspeed ADC Controller
 *
 * Copyright 2021 Facebook, Inc.
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

#include "qemu/osdep.h"
#include "hw/adc/aspeed_adc.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qemu/log.h"

#define TO_REG(offset) ((offset) >> 2)
#define ENGINE_CONTROL TO_REG(0x00)

static uint64_t aspeed_adc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedADCState *s = ASPEED_ADC(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_ADC_MAX_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read 0x%04" HWADDR_PRIX "\n",
                      __func__, offset);
        return 0;
    }

    int value = s->regs[reg];

    trace_aspeed_adc_read(offset, value);
    return value;
}

static void aspeed_adc_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedADCState *s = ASPEED_ADC(opaque);
    int reg = TO_REG(offset);

    if (reg >= ASPEED_ADC_MAX_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write 0x%04" HWADDR_PRIX "\n",
                      __func__, offset);
        return;
    }

    trace_aspeed_adc_write(offset, data);

    switch (reg) {
    case ENGINE_CONTROL:
        switch (data) {
        case 0xF:
            s->regs[ENGINE_CONTROL] = 0x10F;
            return;
        case 0x2F:
            s->regs[ENGINE_CONTROL] = 0xF;
            return;
        }
        break;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_adc_ops = {
    .read = aspeed_adc_read,
    .write = aspeed_adc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_adc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedADCState *s = ASPEED_ADC(dev);

    sysbus_init_irq(sbd, &s->irq);
    // The memory region is actually 4KB (0x1000), but there's 2 ADC's in the
    // AST2600 that are offset by 0x100.
    memory_region_init_io(&s->mmio, OBJECT(s), &aspeed_adc_ops, s,
                          TYPE_ASPEED_ADC, 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void aspeed_adc_reset(DeviceState *dev)
{
    AspeedADCState *s = ASPEED_ADC(dev);
    AspeedADCClass *aac = ASPEED_ADC_GET_CLASS(dev);

    memcpy(s->regs, aac->resets, aac->nr_regs << 2);
}

static const uint32_t aspeed_2400_resets[ASPEED_2400_ADC_NR_REGS] = {
    [ENGINE_CONTROL] = 0x00000000,
};

static const uint32_t aspeed_2500_resets[ASPEED_2500_ADC_NR_REGS] = {
    [ENGINE_CONTROL] = 0x00000000,
};

static const uint32_t aspeed_2600_resets[ASPEED_2600_ADC_NR_REGS] = {
    [ENGINE_CONTROL] = 0x00000000,
};

static const VMStateDescription aspeed_adc_vmstate = {
    .name = TYPE_ASPEED_ADC,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedADCState, ASPEED_ADC_MAX_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void aspeed_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_adc_realize;
    dc->reset = aspeed_adc_reset;
    dc->desc = "Aspeed Analog-to-Digital Converter";
    dc->vmsd = &aspeed_adc_vmstate;
}

static void aspeed_2400_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedADCClass *aac = ASPEED_ADC_CLASS(klass);

    dc->desc = "Aspeed 2400 Analog-to-Digital Converter";
    aac->resets = aspeed_2400_resets;
    aac->nr_regs = ASPEED_2400_ADC_NR_REGS;
}

static void aspeed_2500_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedADCClass *aac = ASPEED_ADC_CLASS(klass);

    dc->desc = "Aspeed 2500 Analog-to-Digital Converter";
    aac->resets = aspeed_2500_resets;
    aac->nr_regs = ASPEED_2500_ADC_NR_REGS;
}

static void aspeed_2600_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedADCClass *aac = ASPEED_ADC_CLASS(klass);

    dc->desc = "Aspeed 2600 Analog-to-Digital Converter";
    aac->resets = aspeed_2600_resets;
    aac->nr_regs = ASPEED_2600_ADC_NR_REGS;
}

static const TypeInfo aspeed_adc_info = {
    .name = TYPE_ASPEED_ADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedADCState),
    .class_init = aspeed_adc_class_init,
    .class_size = sizeof(AspeedADCClass),
    .abstract = true,
};

static const TypeInfo aspeed_2400_adc_info = {
    .name = TYPE_ASPEED_2400_ADC,
    .parent = TYPE_ASPEED_ADC,
    .instance_size = sizeof(AspeedADCState),
    .class_init = aspeed_2400_adc_class_init,
};

static const TypeInfo aspeed_2500_adc_info = {
    .name = TYPE_ASPEED_2500_ADC,
    .parent = TYPE_ASPEED_ADC,
    .instance_size = sizeof(AspeedADCState),
    .class_init = aspeed_2500_adc_class_init,
};

static const TypeInfo aspeed_2600_adc_info = {
    .name = TYPE_ASPEED_2600_ADC,
    .parent = TYPE_ASPEED_ADC,
    .instance_size = sizeof(AspeedADCState),
    .class_init = aspeed_2600_adc_class_init,
};

static void aspeed_adc_register_types(void)
{
    type_register_static(&aspeed_adc_info);
    type_register_static(&aspeed_2400_adc_info);
    type_register_static(&aspeed_2500_adc_info);
    type_register_static(&aspeed_2600_adc_info);
}

type_init(aspeed_adc_register_types);
