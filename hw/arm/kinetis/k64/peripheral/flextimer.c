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
 
/* Kinetis K64 series FLEXTIMER controller.  */

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
#include "hw/arm/kinetis/k64/peripheral/flextimer.h" 

static const VMStateDescription vmstate_kinetis_k64_flextimer = {
    .name = TYPE_KINETIS_K64_FLEXTIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(SC, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(CNT, kinetis_k64_flextimer_state),
//        VMSTATE_UINT32(CONTROLS[0], kinetis_k64_flextimer_state),
        VMSTATE_UINT32(CNTIN, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(STATUS, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(MODE, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(SYNC, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(OUTINIT, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(OUTMASK, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(COMBINE, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(DEADTIME, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(EXTTRIG, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(POL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(FMS, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(FILTER, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(FLTCTRL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(QDCTRL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(CONF, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(FLTPOL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(SYNCONF, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(INVCTRL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(SWOCTRL, kinetis_k64_flextimer_state),
        VMSTATE_UINT32(PWMLOAD, kinetis_k64_flextimer_state),
        VMSTATE_END_OF_LIST()
    }
};

static void kinetis_k64_flextimer_reset(DeviceState *dev)
{
    kinetis_k64_flextimer_state *s = KINETIS_K64_FLEXTIMER(dev);
    
    s->CNT = 0x00;
}

static void kinetis_k64_flextimer_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
//    kinetis_k64_flextimer_state *s = (kinetis_k64_flextimer_state *)opaque;
	
    value = value & 0xFF;
/*    printf("kinetis_k64_flextimer_write: Offset = 0x%02X, Value = 0x%02X\n",
        (unsigned int)offset, (unsigned int)value);*/
    
    switch (offset) {
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "kinetis_k64_flextimer: write at bad offset 0x%x\n",
                    (int)offset);
    }
}

static uint64_t kinetis_k64_flextimer_read(void *opaque, hwaddr offset,
        unsigned size)
{
//    kinetis_k64_flextimer_state *s = (kinetis_k64_flextimer_state *)opaque;

    switch (offset) {
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "kinetis_k64_flextimer: read at bad offset 0x%x\n",
                    (int)offset);
            return 0;
    }
}

static const MemoryRegionOps kinetis_k64_flextimer_ops = {
    .read = kinetis_k64_flextimer_read,
    .write = kinetis_k64_flextimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void kinetis_k64_flextimer_init(Object *obj)
{
    kinetis_k64_flextimer_state *s = KINETIS_K64_FLEXTIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    
    memory_region_init_io(&s->iomem, obj, &kinetis_k64_flextimer_ops, s,
            TYPE_KINETIS_K64_FLEXTIMER, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void kinetis_k64_flextimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_kinetis_k64_flextimer;
    dc->reset = kinetis_k64_flextimer_reset;
    dc->desc = "Kinetis K64 series FlexTimer";      
}

static const TypeInfo kinetis_k64_flextimer_info = {
    .name          = TYPE_KINETIS_K64_FLEXTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(kinetis_k64_flextimer_state),
    .instance_init = kinetis_k64_flextimer_init,
    .class_init    = kinetis_k64_flextimer_class_init,
};

static void kinetis_k64_flextimer_register_types(void)
{
    type_register_static(&kinetis_k64_flextimer_info);
}

type_init(kinetis_k64_flextimer_register_types)