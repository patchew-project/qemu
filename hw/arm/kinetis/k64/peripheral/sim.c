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
 
/* Kinetis K64 series SIM controller.  */
 
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
#include "hw/arm/kinetis/k64/peripheral/sim.h"

static const VMStateDescription vmstate_kinetis_k64_sim = {
    .name = TYPE_KINETIS_K64_SIM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(SOPT1, kinetis_k64_sim_state),
        VMSTATE_UINT32(SOPT1CFG, kinetis_k64_sim_state),
        VMSTATE_UINT32(SOPT2, kinetis_k64_sim_state),
        VMSTATE_UINT32(SOPT4, kinetis_k64_sim_state),
        VMSTATE_UINT32(SOPT5, kinetis_k64_sim_state),
        VMSTATE_UINT32(SOPT7, kinetis_k64_sim_state),
        VMSTATE_UINT32(SDID, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC1, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC2, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC3, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC4, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC5, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC6, kinetis_k64_sim_state),
        VMSTATE_UINT32(SCGC7, kinetis_k64_sim_state),
        VMSTATE_UINT32(CLKDIV1, kinetis_k64_sim_state),
        VMSTATE_UINT32(CLKDIV2, kinetis_k64_sim_state),
        VMSTATE_UINT32(FCFG1, kinetis_k64_sim_state),
        VMSTATE_UINT32(FCFG2, kinetis_k64_sim_state),
        VMSTATE_UINT32(UIDH, kinetis_k64_sim_state),
        VMSTATE_UINT32(UIDMH, kinetis_k64_sim_state),
        VMSTATE_UINT32(UIDML, kinetis_k64_sim_state),
        VMSTATE_UINT32(UIDL, kinetis_k64_sim_state),
        VMSTATE_END_OF_LIST()
    }
};

static void kinetis_k64_sim_reset(DeviceState *dev)
{
    kinetis_k64_sim_state *s = KINETIS_K64_SIM(dev);
    
    s->SOPT1 = 0x00008000;
    s->SOPT1CFG = 0x00000000;
    s->SOPT2 = 0x00001000;
    s->SOPT4 = 0x00000000;
    s->SOPT5 = 0x00000000;
    s->SOPT7 = 0x00000000;
    s->SDID = 0x00000000;
    s->SCGC1 = 0x00000000;
    s->SCGC2 = 0x00000000;
    s->SCGC3 = 0x00000000;
    s->SCGC4 = 0xF0100030;
    s->SCGC5 = 0x00040182;
    s->SCGC6 = 0x40000001;
    s->SCGC7 = 0x00000006;
    s->CLKDIV1 = 0x00000000;
    s->CLKDIV2 = 0x00000000;
    s->FCFG1 = 0xFF000000;
    s->FCFG2 = 0x00000000;
    s->UIDH = 0x00000000;
    s->UIDMH = 0x00000000;
    s->UIDML = 0x00000000;
    s->UIDL = 0x00000000;
}

static void kinetis_k64_sim_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    kinetis_k64_sim_state *s = (kinetis_k64_sim_state *)opaque;

    value = value & 0xFFFFFFFF;
/*    printf("kinetis_k64_sim_write: Offset = 0x%08X, Value = 0x%08X\n",
            (unsigned int)offset, (unsigned int)value);*/
    
    switch (offset) {
        case 0x0000: /**< System Options Register 1, offset: 0x0 */
            s->SOPT1 = value;
            break;
        case 0x0004: /**< SOPT1 Configuration Register, offset: 0x4 */
            s->SOPT1CFG = value;
            break;
        case 0x1004: /**< System Options Register 2, offset: 0x1004 */
            s->SOPT2 = value;
            break;
        case 0x100C: /**< System Options Register 4, offset: 0x100C */
            s->SOPT4 = value;
            break;
        case 0x1010: /**< System Options Register 5, offset: 0x1010 */
            s->SOPT5 = value;
            break;
        case 0x1018: /**< System Options Register 7, offset: 0x1018 */
            s->SOPT7 = value;
            break;
        case 0x1024: /**< System Device Id Register, offset: 0x1024 */
            s->SDID = value;
            break;
        case 0x1028: /**< System Clock Gating Ctrl Register 1, offset: 0x1028 */
            s->SCGC1 = value;
            break;
        case 0x102C: /**< System Clock Gating Ctrl Register 2, offset: 0x102C */
            s->SCGC2 = value;
            break;
        case 0x1030: /**< System Clock Gating Ctrl Register 3, offset: 0x1030 */
            s->SCGC3 = value;
            break;
        case 0x1034: /**< System Clock Gating Ctrl Register 4, offset: 0x1034 */
            s->SCGC4 = value;
            break;
        case 0x1013: /**< System Clock Gating Ctrl Register 5, offset: 0x1038 */
            s->SCGC5 = value;
            break;
        case 0x103C: /**< System Clock Gating Ctrl Register 6, offset: 0x103C */
            s->SCGC6 = value;
            break;
        case 0x1040: /**< System Clock Gating Ctrl Register 7, offset: 0x1040 */
            s->SCGC7 = value;
            break;
        case 0x1044: /**< System Clock Divider Register 1, offset: 0x1044 */
            s->CLKDIV1 = value;
            break;
        case 0x1048: /**< System Clock Divider Register 2, offset: 0x1048 */
            s->CLKDIV2 = value;
            break;
        case 0x104C: /**< Flash Configuration Register 1, offset: 0x104C */
            s->FCFG1 = value;
            break;
        case 0x1050: /**< Flash Configuration Register 2, offset: 0x1050 */
            s->FCFG2 = value;
            break;
        case 0x1054: /**< Unique Id Register High, offset: 0x1054 */
            s->UIDH = value;
            break;
        case 0x1058: /**< Unique Id Register Mid-High, offset: 0x1058 */
            s->UIDMH = value;
            break;
        case 0x105C: /**< Unique Id Register Mid Low, offset: 0x105C */
            s->UIDML = value;
            break;
        case 0x1060: /**< Unique Id Register Low, offset: 0x1060 */   
            s->UIDL = value;        
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_sim: write at bad offset 0x%x\n", (int)offset);
    }
}

static uint64_t kinetis_k64_sim_read(void *opaque, hwaddr offset, unsigned size)
{
    kinetis_k64_sim_state *s = (kinetis_k64_sim_state *)opaque;

    uint32_t value;
    
    switch (offset) {
        case 0x0000: /**< System Options Register 1, offset: 0x0 */
            value = s->SOPT1;
            break;
        case 0x0004: /**< SOPT1 Configuration Register, offset: 0x4 */
            value = s->SOPT1CFG;
            break;
        case 0x1004: /**< System Options Register 2, offset: 0x1004 */
            value = s->SOPT2;
            break;
        case 0x100C: /**< System Options Register 4, offset: 0x100C */
            value = s->SOPT4;
            break;
        case 0x1010: /**< System Options Register 5, offset: 0x1010 */
            value = s->SOPT5;
            break;
        case 0x1018: /**< System Options Register 7, offset: 0x1018 */
            value = s->SOPT7;
            break;
        case 0x1024: /**< System Device Id Register, offset: 0x1024 */
            value = s->SDID;
            break;
        case 0x1028: /**< System Clock Gating Ctrl Register 1, offset: 0x1028 */
            value = s->SCGC1;
            break;
        case 0x102C: /**< System Clock Gating Ctrl Register 2, offset: 0x102C */
            value = s->SCGC2;
            break;
        case 0x1030: /**< System Clock Gating Ctrl Register 3, offset: 0x1030 */
            value = s->SCGC3;
            break;
        case 0x1034: /**< System Clock Gating Ctrl Register 4, offset: 0x1034 */
            value = s->SCGC4;
            break;
        case 0x1013: /**< System Clock Gating Ctrl Register 5, offset: 0x1038 */
            value = s->SCGC5;
            break;
        case 0x103C: /**< System Clock Gating Ctrl Register 6, offset: 0x103C */
            value = s->SCGC6;
            break;
        case 0x1040: /**< System Clock Gating Ctrl Register 7, offset: 0x1040 */
            value = s->SCGC7;
            break;
        case 0x1044: /**< System Clock Divider Register 1, offset: 0x1044 */
            value = s->CLKDIV1;
            break;
        case 0x1048: /**< System Clock Divider Register 2, offset: 0x1048 */
            value = s->CLKDIV2;
            break;
        case 0x104C: /**< Flash Configuration Register 1, offset: 0x104C */
            value = s->FCFG1;
            break;
        case 0x1050: /**< Flash Configuration Register 2, offset: 0x1050 */
            value = s->FCFG2;
            break;
        case 0x1054: /**< Unique Id Register High, offset: 0x1054 */
            value = s->UIDH;
            break;
        case 0x1058: /**< Unique Id Register Mid-High, offset: 0x1058 */
            value = s->UIDMH;
            break;
        case 0x105C: /**< Unique Id Register Mid Low, offset: 0x105C */
            value = s->UIDML;
            break;
        case 0x1060: /**< Unique Id Register Low, offset: 0x1060 */   
            value = s->UIDL;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_sim: read at bad offset 0x%x\n", (int)offset);
            return 0;
    }
/*    printf("kinetis_k64_sim_read: Offset = 0x%08X, Value = 0x%08X\n",
            (unsigned int)offset, (unsigned int)value);*/
    return value;    
}

static const MemoryRegionOps kinetis_k64_sim_ops = {
    .read = kinetis_k64_sim_read,
    .write = kinetis_k64_sim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void kinetis_k64_sim_init(Object *obj)
{
    kinetis_k64_sim_state *s = KINETIS_K64_SIM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    
    memory_region_init_io(&s->iomem, obj, &kinetis_k64_sim_ops, s,
            TYPE_KINETIS_K64_SIM, 0x2000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void kinetis_k64_sim_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_kinetis_k64_sim;
    dc->reset = kinetis_k64_sim_reset;
    dc->desc = "Kinetis K64 series SIM";
}

static const TypeInfo kinetis_k64_sim_info = {
    .name          = TYPE_KINETIS_K64_SIM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(kinetis_k64_sim_state),
    .instance_init = kinetis_k64_sim_init,
    .class_init    = kinetis_k64_sim_class_init,
};

static void kinetis_k64_sim_register_types(void)
{
    type_register_static(&kinetis_k64_sim_info);
}

type_init(kinetis_k64_sim_register_types)