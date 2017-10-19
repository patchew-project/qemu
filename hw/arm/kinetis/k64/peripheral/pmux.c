/*
 * Kinetis K64 peripheral microcontroller emulation.
 *
 * Copyright (c) 2017 Advantech Wireless
 * Written by Gabriel Costa <gabriel291075@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */
 
/* Kinetis K64 series PMUX controller.  */
 
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/char/pl011.h"
#include "hw/misc/unimp.h"
#include "hw/arm/kinetis/k64/peripheral/pmux.h"

static const VMStateDescription vmstate_kinetis_k64_pmux = {
    .name = TYPE_KINETIS_K64_PMUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(PCR00, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR01, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR02, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR03, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR04, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR05, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR06, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR07, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR08, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR09, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR10, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR11, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR12, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR13, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR14, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR15, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR16, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR17, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR18, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR19, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR20, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR21, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR22, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR23, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR24, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR25, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR26, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR27, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR28, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR29, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR30, kinetis_k64_pmux_state),
        VMSTATE_UINT32(PCR31, kinetis_k64_pmux_state),
        VMSTATE_UINT32(GPCLR, kinetis_k64_pmux_state),
        VMSTATE_UINT32(GPCHR, kinetis_k64_pmux_state),
        VMSTATE_UINT32(ISFR, kinetis_k64_pmux_state),
        VMSTATE_UINT32(DFER, kinetis_k64_pmux_state),
        VMSTATE_UINT32(DFCR, kinetis_k64_pmux_state),
        VMSTATE_UINT32(DFWR, kinetis_k64_pmux_state),
        VMSTATE_END_OF_LIST()
    }
};

static void kinetis_k64_pmux_reset(DeviceState *dev)
{
    kinetis_k64_pmux_state *s = KINETIS_K64_PMUX(dev);
    
    s->PCR00 = 0x00000000;  
    s->PCR01 = 0x00000000;  
    s->PCR02 = 0x00000000; 
    s->PCR03 = 0x00000000;  
    s->PCR04 = 0x00000000;  
    s->PCR05 = 0x00000000; 
    s->PCR06 = 0x00000000; 
    s->PCR07 = 0x00000000;  
    s->PCR08 = 0x00000000; 
    s->PCR09 = 0x00000000; 
    s->PCR10 = 0x00000000;  
    s->PCR11 = 0x00000000; 
    s->PCR12 = 0x00000000;  
    s->PCR13 = 0x00000000;  
    s->PCR14 = 0x00000000; 
    s->PCR15 = 0x00000000; 
    s->PCR16 = 0x00000000; 
    s->PCR17 = 0x00000000;  
    s->PCR18 = 0x00000000; 
    s->PCR19 = 0x00000000; 
    s->PCR20 = 0x00000000; 
    s->PCR21 = 0x00000000;  
    s->PCR22 = 0x00000000; 
    s->PCR23 = 0x00000000;  
    s->PCR24 = 0x00000000; 
    s->PCR25 = 0x00000000; 
    s->PCR26 = 0x00000000; 
    s->PCR27 = 0x00000000;  
    s->PCR28 = 0x00000000; 
    s->PCR29 = 0x00000000;  
    s->PCR30 = 0x00000000;  
    s->PCR31 = 0x00000000;  
    s->GPCLR = 0x00000000; 
    s->GPCHR = 0x00000000; 
    s->ISFR = 0x00000000;  
    s->DFER = 0x00000000;   
    s->DFCR = 0x00000000;   
    s->DFWR = 0x00000000;
}

static void kinetis_k64_pmux_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    kinetis_k64_pmux_state *s = (kinetis_k64_pmux_state *)opaque;

    value = value & 0xFFFFFFFF;
/*    printf("kinetis_k64_pmux_write: Offset = 0x%08X, Value = 0x%08X\n",
            (unsigned int)offset, (unsigned int)value);*/
    
    switch (offset) {
        case 0x00: /**< Pin Control Register n, offset: 0x0, step: 0x4 */
            s->PCR00 = value;
            break;
        case 0x04: 
            s->PCR01 = value;
            break;
        case 0x08: 
            s->PCR02 = value;
            break;
        case 0x0C: 
            s->PCR03 = value;
            break;
        case 0x10: 
            s->PCR04 = value;
            break;
        case 0x14: 
            s->PCR05 = value;
            break;
        case 0x18: 
            s->PCR06 = value;
            break;
        case 0x1C: 
            s->PCR07 = value;
            break;
        case 0x20: 
            s->PCR08 = value;
            break;
        case 0x24: 
            s->PCR09 = value;
            break;
        case 0x28: 
            s->PCR10 = value;
            break;
        case 0x2C: 
            s->PCR11 = value;
            break;
        case 0x30: 
            s->PCR12 = value;
            break;
        case 0x34: 
            s->PCR13 = value;
            break;
        case 0x38: 
            s->PCR14 = value;
            break;
        case 0x3C: 
            s->PCR15 = value;
            break;
        case 0x40: 
            s->PCR16 = value;
            break;
        case 0x44: 
            s->PCR17 = value;
            break;
        case 0x48: 
            s->PCR18 = value;
            break;
        case 0x4C:
            s->PCR19 = value;
            break;
        case 0x50:
            s->PCR20 = value;
            break;
        case 0x54:
            s->PCR21 = value;
            break;
        case 0x58:
            s->PCR22 = value;
            break;
        case 0x5C:
            s->PCR23 = value;
            break;
        case 0x60:
            s->PCR24 = value;
            break;
        case 0x64:
            s->PCR25 = value;
            break;
        case 0x68:
            s->PCR26 = value;
            break;
        case 0x6C:
            s->PCR27 = value;
            break;
        case 0x70:
            s->PCR28 = value;
            break;
        case 0x74:
            s->PCR29 = value;
            break;
        case 0x78:
            s->PCR30 = value;
            break;
        case 0x7C:
            s->PCR31 = value;
            break;
        case 0x80: /**< Global Pin Control Low Register, offset: 0x80 */
            s->GPCLR = value;
            break;
        case 0x84:  /**< Global Pin Control High Register, offset: 0x84 */
            s->GPCHR = value;
            break;
        case 0xA0: /**< Interrupt Status Flag Register, offset: 0xA0 */
            s->ISFR = value;
            break;
        case 0xC0: /**< Digital Filter Enable Register, offset: 0xC0 */
            s->DFER = value;
            break;
        case 0xC4: /**< Digital Filter Clock Register, offset: 0xC4 */
            s->DFCR = value;
            break;
        case 0xC8: /**< Digital Filter Width Register, offset: 0xC8 */
            s->DFWR = value;
            break;
        
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_pmux: write at bad offset 0x%x\n",
                    (int)offset);
    }
}

static uint64_t kinetis_k64_pmux_read(void *opaque, hwaddr offset,
        unsigned size)
{
    kinetis_k64_pmux_state *s = (kinetis_k64_pmux_state *)opaque;
    uint8_t value;
    
    switch (offset) {
        case 0x00: /**< Pin Control Register n, offset: 0x0, step: 0x4 */
            value = s->PCR00;
            break;
        case 0x04: 
            value = s->PCR01;
            break;
        case 0x08: 
            value = s->PCR02;
            break;
        case 0x0C: 
            value = s->PCR03;
            break;
        case 0x10: 
            value = s->PCR04;
            break;
        case 0x14: 
            value = s->PCR05;
            break;
        case 0x18: 
            value = s->PCR06;
            break;
        case 0x1C: 
            value = s->PCR07;
            break;
        case 0x20: 
            value = s->PCR08;
            break;
        case 0x24: 
            value = s->PCR09;
            break;
        case 0x28: 
            value = s->PCR10;
            break;
        case 0x2C: 
            value = s->PCR11;
            break;
        case 0x30: 
            value = s->PCR12;
            break;
        case 0x34: 
            value = s->PCR13;
            break;
        case 0x38: 
            value = s->PCR14;
            break;
        case 0x3C: 
            value = s->PCR15;
            break;
        case 0x40: 
            value = s->PCR16;
            break;
        case 0x44: 
            value = s->PCR17;
            break;
        case 0x48: 
            value = s->PCR18;
            break;
        case 0x4C:
            value = s->PCR19;
            break;
        case 0x50:
            value = s->PCR20;
            break;
        case 0x54:
            value = s->PCR21;
            break;
        case 0x58:
            value = s->PCR22;
            break;
        case 0x5C:
            value = s->PCR23;
            break;
        case 0x60:
            value = s->PCR24;
            break;
        case 0x64:
            value = s->PCR25;
            break;
        case 0x68:
            value = s->PCR26;
            break;
        case 0x6C:
            value = s->PCR27;
            break;
        case 0x70:
            value = s->PCR28;
            break;
        case 0x74:
            value = s->PCR29;
            break;
        case 0x78:
            value = s->PCR30;
            break;
        case 0x7C:
            value = s->PCR31;
            break;
        case 0x80: /**< Global Pin Control Low Register, offset: 0x80 */
            value = s->GPCLR;
            break;
        case 0x84:  /**< Global Pin Control High Register, offset: 0x84 */
            value = s->GPCHR;
            break;
        case 0xA0: /**< Interrupt Status Flag Register, offset: 0xA0 */
            value = s->ISFR;
            break;
        case 0xC0: /**< Digital Filter Enable Register, offset: 0xC0 */
            value = s->DFER;
            break;
        case 0xC4: /**< Digital Filter Clock Register, offset: 0xC4 */
            value = s->DFCR;
            break;
        case 0xC8: /**< Digital Filter Width Register, offset: 0xC8 */
            value = s->DFWR;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_pmux: read at bad offset 0x%x\n", (int)offset);
            return 0;
    }
/*    printf("kinetis_k64_pmux_read: Offset = 0x%08X, Value = 0x%08X\n", 
            (unsigned int)offset, (unsigned int)value);*/
    return value;
}

static const MemoryRegionOps kinetis_k64_pmux_ops = {
    .read = kinetis_k64_pmux_read,
    .write = kinetis_k64_pmux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void kinetis_k64_pmux_init(Object *obj)
{
    kinetis_k64_pmux_state *s = KINETIS_K64_PMUX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    
    memory_region_init_io(&s->iomem, obj, &kinetis_k64_pmux_ops, s,
            TYPE_KINETIS_K64_PMUX, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void kinetis_k64_pmux_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_kinetis_k64_pmux;
    dc->reset = kinetis_k64_pmux_reset;
    dc->desc = "Kinetis K64 series PMUX";    
}

static const TypeInfo kinetis_k64_pmux_info = {
    .name          = TYPE_KINETIS_K64_PMUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(kinetis_k64_pmux_state),
    .instance_init = kinetis_k64_pmux_init,
    .class_init    = kinetis_k64_pmux_class_init,
};

static void kinetis_k64_pmux_register_types(void)
{
    type_register_static(&kinetis_k64_pmux_info);
}

type_init(kinetis_k64_pmux_register_types)