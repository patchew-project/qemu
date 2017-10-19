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
 
/* Kinetis K64 series MCG controller.  */

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
#include "hw/arm/kinetis/k64/peripheral/mcg.h"

static const VMStateDescription vmstate_kinetis_k64_mcg = {
    .name = TYPE_KINETIS_K64_MCG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(C1, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C2, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C3, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C4, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C5, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C6, kinetis_k64_mcg_state),
        VMSTATE_UINT8(S, kinetis_k64_mcg_state),
        VMSTATE_UINT8(SC, kinetis_k64_mcg_state),
        VMSTATE_UINT8(ATCVH, kinetis_k64_mcg_state),
        VMSTATE_UINT8(ATCVL, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C7, kinetis_k64_mcg_state),
        VMSTATE_UINT8(C8, kinetis_k64_mcg_state),
        VMSTATE_END_OF_LIST()
    }
};

static void kinetis_k64_mcg_reset(DeviceState *dev)
{
    kinetis_k64_mcg_state *s = KINETIS_K64_MCG(dev);
    
    s->C1 = 0x04;
    s->C2 = 0x80;
    s->C3 = 0x00;
    s->C4 = 0x00;
    s->C5 = 0x00;
    s->C6 = 0x00;
    s->S = 0x10;
    s->SC = 0x02;
    s->ATCVH = 0x00;
    s->ATCVL = 0x00;
    s->C7 = 0x00;
    s->C8 = 0x80;
}

static void kinetis_k64_mcg_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    kinetis_k64_mcg_state *s = (kinetis_k64_mcg_state *)opaque;

    value &= 0xFF;
/*    printf("kinetis_k64_mcg_write: Offset = 0x%02X, Value = 0x%02X\n", 
            (unsigned int)offset, (unsigned int)value);*/    
    
    switch (offset) {
        case 0x00: /**< MCG Control 1 Register, offset: 0x0 */
            if (value & 1<<2){ //IREFS
                s->S = 0;    
                s->S |= 1<<3; // 10 Enconding 2 - External ref clk is selected    
            }
            if ((s->C1 & 0x80) && (value>>6 == 0)){
                s->S |= 1<<2; // 11 Enconding 3 - Output of the PLL is selected    
            }
            s->C1 = value;
            break;
        case 0x01: /**< MCG Control 2 Register, offset: 0x1 */
            s->C2 = value;
            break;
        case 0x02: /**< MCG Control 3 Register, offset: 0x2 */
            s->C3 = value;
            break;
        case 0x03: /**< MCG Control 4 Register, offset: 0x3 */
            s->C4 = value;
            break;
        case 0x04: /**< MCG Control 5 Register, offset: 0x4 */
            s->C5 = value;
            if (s->C5 & 1<<6){ //PLLCLKEN0
                s->S |= 1<<6;  //LOCK0   
            }
            break;
        case 0x05: /**< MCG Control 6 Register, offset: 0x5 */
            s->C6 = value;
            if (s->C6 & 1<<6){ //PLLS
                s->S |= 1<<5;  //PLLST 
            }            
            break;
        case 0x06: /**< MCG Status Register, offset: 0x6 */
            s->S = value;
            break;
        case 0x08: /**< MCG Status and Control Register, offset: 0x8 */
            s->SC = value;
            break;
        case 0x0A: /**< MCG Auto Trim Compare Value High Register, offset: 0xA*/
            s->ATCVH = value;
            break;
        case 0x0B: /**< MCG Auto Trim Compare Value Low Register, offset: 0xB */
            s->ATCVL = value;
            break;
        case 0x0C: /**< MCG Control 7 Register, offset: 0xC */
            s->C7 = value;
            break;
        case 0x0D: /**< MCG Control 8 Register, offset: 0xD */
            s->C8 = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_mcg: write at bad offset 0x%x\n", (int)offset);
    }
}

static uint64_t kinetis_k64_mcg_read(void *opaque, hwaddr offset, unsigned size)
{
    kinetis_k64_mcg_state *s = (kinetis_k64_mcg_state *)opaque;
    uint8_t value;
    
    switch (offset) {
        case 0x00: /**< MCG Control 1 Register, offset: 0x0 */
            value = s->C1;
            break;
        case 0x01: /**< MCG Control 2 Register, offset: 0x1 */
            value = s->C2;
            break;
        case 0x02: /**< MCG Control 3 Register, offset: 0x2 */
            value = s->C3;
            break;
        case 0x03: /**< MCG Control 4 Register, offset: 0x3 */
            value = s->C4;
            break;
        case 0x04: /**< MCG Control 5 Register, offset: 0x4 */
            value = s->C5;
            break;
        case 0x05: /**< MCG Control 6 Register, offset: 0x5 */
            value = s->C6;
            break;
        case 0x06: /**< MCG Status Register, offset: 0x6 */
            value = s->S;
            break;
        case 0x08: /**< MCG Status and Control Register, offset: 0x8 */
            value = s->SC;
            break;
        case 0x0A: /**< MCG Auto Trim Compare Value High Register, offset: 0xA*/
            value = s->ATCVH;
            break;
        case 0x0B: /**< MCG Auto Trim Compare Value Low Register, offset: 0xB */
            value = s->ATCVL;
            break;
        case 0x0C: /**< MCG Control 7 Register, offset: 0xC */
            value = s->C7;
            break;
        case 0x0D: /**< MCG Control 8 Register, offset: 0xD */
            value = s->C8;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, 
                    "kinetis_k64_mcg: read at bad offset 0x%x\n", (int)offset);
            return 0;
    }
/*    printf("kinetis_k64_mcg_read: Offset = 0x%02X, Value = 0x%02X\n",
            (unsigned int)offset, (unsigned int)value);*/
    return value;    
}

static const MemoryRegionOps kinetis_k64_mcg_ops = {
    .read = kinetis_k64_mcg_read,
    .write = kinetis_k64_mcg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void kinetis_k64_mcg_init(Object *obj)
{
    kinetis_k64_mcg_state *s = KINETIS_K64_MCG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    
    memory_region_init_io(&s->iomem, obj, &kinetis_k64_mcg_ops, s,
            TYPE_KINETIS_K64_MCG, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void kinetis_k64_mcg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_kinetis_k64_mcg;
    dc->reset = kinetis_k64_mcg_reset;
    dc->desc = "Kinetis K64 series MCG";      
}

static const TypeInfo kinetis_k64_mcg_info = {
    .name          = TYPE_KINETIS_K64_MCG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(kinetis_k64_mcg_state),
    .instance_init = kinetis_k64_mcg_init,
    .class_init    = kinetis_k64_mcg_class_init,
};

static void kinetis_k64_mcg_register_types(void)
{
    type_register_static(&kinetis_k64_mcg_info);
}

type_init(kinetis_k64_mcg_register_types)