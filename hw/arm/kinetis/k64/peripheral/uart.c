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
 
/* Kinetis K64 series UART controller.  */

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
#include "hw/arm/kinetis/k64/peripheral/uart.h"

static const VMStateDescription vmstate_kinetis_k64_uart = {
    .name = TYPE_KINETIS_K64_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(BDH, kinetis_k64_uart_state),
        VMSTATE_UINT8(BDL, kinetis_k64_uart_state),
        VMSTATE_UINT8(C1, kinetis_k64_uart_state),
        VMSTATE_UINT8(C2, kinetis_k64_uart_state),
        VMSTATE_UINT8(S1, kinetis_k64_uart_state),
        VMSTATE_UINT8(S2, kinetis_k64_uart_state),
        VMSTATE_UINT8(C3, kinetis_k64_uart_state),
        VMSTATE_UINT8(D, kinetis_k64_uart_state),
        VMSTATE_UINT8(MA1, kinetis_k64_uart_state),
        VMSTATE_UINT8(MA2, kinetis_k64_uart_state),
        VMSTATE_UINT8(C4, kinetis_k64_uart_state),
        VMSTATE_UINT8(C5, kinetis_k64_uart_state),
        VMSTATE_UINT8(ED, kinetis_k64_uart_state),
        VMSTATE_UINT8(MODEM, kinetis_k64_uart_state),
        VMSTATE_UINT8(IR, kinetis_k64_uart_state),
        VMSTATE_UINT8(PFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(CFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(SFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(TWFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(TCFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(RWFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(RCFIFO, kinetis_k64_uart_state),
        VMSTATE_UINT8(C7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(IE7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(IS7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(WP7816T0, kinetis_k64_uart_state),
        VMSTATE_UINT8(WN7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(WF7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(ET7816, kinetis_k64_uart_state),
        VMSTATE_UINT8(TL7816, kinetis_k64_uart_state),         
        VMSTATE_END_OF_LIST()
    }
};

static void kinetis_k64_uart_reset(DeviceState *dev)
{
    kinetis_k64_uart_state *s = KINETIS_K64_UART(dev);
    
    s->BDH = 0x00;
    s->BDL = 0x04;
    s->C1 = 0x00;
    s->C2 = 0x00;
    s->S1 = 0xC0;
    s->S2 = 0x00;
    s->C3 = 0x00;
    s->D = 0x00;
    s->MA1 = 0x00;
    s->MA2 = 0x00;
    s->C4 = 0x00;
    s->C5 = 0x00;
    s->ED = 0x00;
    s->MODEM = 0x00;
    s->IR = 0x00;
    s->PFIFO = 0x00;
    s->CFIFO = 0x00;
    s->SFIFO = 0xC0;
    s->TWFIFO = 0x00;
    s->TCFIFO = 0x00;
    s->RWFIFO = 0x01;
    s->RCFIFO = 0x00;
    s->C7816 = 0x00;
    s->IE7816 = 0x00;
    s->IS7816 = 0x00;
    s->WP7816T0 = 0x0A;
    s->WN7816 = 0x00;
    s->WF7816 = 0x01;
    s->ET7816 = 0x00;
    s->TL7816 = 0x00;
    
    qemu_set_irq(s->irq, 0);
}

static void kinetis_k64_uart_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned size)
{
    kinetis_k64_uart_state *s = (kinetis_k64_uart_state *)opaque;

    value &= 0xFF;
/*    printf("kinetis_k64_uart_write: Offset = 0x%02X, Value = 0x%02X, ch=%c\n",
            (unsigned int)offset, (unsigned int)value, (unsigned int)value);*/    
    
    switch (offset) {
        case 0x00: /**< UART Baud Rate Registers: High, offset: 0x0 */
            s->BDH = value;
            break;
        case 0x01: /**< UART Baud Rate Registers: Low, offset: 0x1 */
            s->BDL = value;
            break;
        case 0x02: /**< UART Control Register 1, offset: 0x2 */
            s->C1 = value;
            break;
        case 0x03: /**< UART Control Register 2, offset: 0x3 */
            s->C2 = value;
            break;
        case 0x04: /**< UART Status Register 1, offset: 0x4 */
            s->S1 = value;
            break;
        case 0x05: /**< UART Status Register 2, offset: 0x5 */
            s->S2 = value;
            break;
        case 0x06: /**< UART Control Register 3, offset: 0x6 */
            s->C3 = value;
            break;
        case 0x07: /**< UART Data Register, offset: 0x7 */
            s->D = value;
            qemu_chr_fe_write_all(&s->chr, &s->D, 1);
            break;
        case 0x08: /**< UART Match Address Registers 1, offset: 0x8 */
            s->MA1 = value;
            break;
        case 0x09: /**< UART Match Address Registers 2, offset: 0x9 */
            s->MA2 = value;
            break;
        case 0x0A: /**< UART Control Register 4, offset: 0xA */
            s->C4 = value;
            break;
        case 0x0B: /**< UART Control Register 5, offset: 0xB */
            s->C5 = value;
            break;
        case 0x0C: /**< UART Extended Data Register, offset: 0xC */
            s->ED = value;
            break;
        case 0x0D: /**< UART Modem Register, offset: 0xD */
            s->MODEM = value;
            break;
        case 0x0E: /**< UART Infrared Register, offset: 0xE */
            s->IR = value;
            break;
        case 0x10: /**< UART FIFO Parameters, offset: 0x10 */
            s->PFIFO = value;
            break;
        case 0x11: /**< UART FIFO Control Register, offset: 0x11 */
            s->CFIFO = value;
            break;
        case 0x12: /**< UART FIFO Status Register, offset: 0x12 */
            s->SFIFO = value;
            break;
        case 0x13: /**< UART FIFO Transmit Watermark, offset: 0x13 */
            s->TWFIFO = value;
            break;
        case 0x14: /**< UART FIFO Transmit Count, offset: 0x14 */
            s->TCFIFO = value;
            break;
        case 0x15: /**< UART FIFO Receive Watermark, offset: 0x15 */
            s->RWFIFO = value;
            break;
        case 0x16: /**< UART FIFO Receive Count, offset: 0x16 */
            s->RCFIFO = value;
            break;
        case 0x18: /**< UART 7816 Control Register, offset: 0x18 */
            s->C7816 = value;
            break;
        case 0x19: /**< UART 7816 Interrupt Enable Register, offset: 0x19 */
            s->IE7816 = value;
            break;
        case 0x1A: /**< UART 7816 Interrupt Status Register, offset: 0x1A */
            s->IS7816 = value;
            break;
        case 0x1B: /**< UART 7816 Wait Parameter Register, offset: 0x1B */
            s->WP7816T0 = value;
            break;
        case 0x1C: /**< UART 7816 Wait N Register, offset: 0x1C */
            s->WN7816 = value;
            break;
        case 0x1D: /**< UART 7816 Wait FD Register, offset: 0x1D */
            s->WF7816 = value;
            break;
        case 0x1E: /**< UART 7816 Error Threshold Register, offset: 0x1E */
            s->ET7816 = value;
            break;
        case 0x1F: /**< UART 7816 Transmit Length Register, offset: 0x1F */
            s->TL7816 = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_uart: write at bad offset 0x%x\n",
                    (int)offset);
    }
}

static uint64_t kinetis_k64_uart_read(void *opaque, hwaddr offset,
        unsigned size)
{
    kinetis_k64_uart_state *s = (kinetis_k64_uart_state *)opaque;

    switch (offset) {
        case 0x00: /**< UART Baud Rate Registers: High, offset: 0x0 */
            return s->BDH;
        case 0x01: /**< UART Baud Rate Registers: Low, offset: 0x1 */
            return s->BDL;
        case 0x02: /**< UART Control Register 1, offset: 0x2 */
            return s->C1;
        case 0x03: /**< UART Control Register 2, offset: 0x3 */
            return s->C2;
        case 0x04: /**< UART Status Register 1, offset: 0x4 */
            return s->S1;
        case 0x05: /**< UART Status Register 2, offset: 0x5 */
            return s->S2;
        case 0x06: /**< UART Control Register 3, offset: 0x6 */
            return s->C3;
        case 0x07: /**< UART Data Register, offset: 0x7 */
            s->RCFIFO = 0;
            qemu_chr_fe_accept_input(&s->chr);
            return s->D;
        case 0x08: /**< UART Match Address Registers 1, offset: 0x8 */
            return s->MA1;
        case 0x09: /**< UART Match Address Registers 2, offset: 0x9 */
            return s->MA2;
        case 0x0A: /**< UART Control Register 4, offset: 0xA */
            return s->C4;
        case 0x0B: /**< UART Control Register 5, offset: 0xB */
            return s->C5;
        case 0x0C: /**< UART Extended Data Register, offset: 0xC */
            return s->ED;
        case 0x0D: /**< UART Modem Register, offset: 0xD */
            return s->MODEM;
        case 0x0E: /**< UART Infrared Register, offset: 0xE */
            return s->IR;
        case 0x10: /**< UART FIFO Parameters, offset: 0x10 */
            return s->PFIFO;
        case 0x11: /**< UART FIFO Control Register, offset: 0x11 */
            return s->CFIFO;
        case 0x12: /**< UART FIFO Status Register, offset: 0x12 */
            return s->SFIFO;
        case 0x13: /**< UART FIFO Transmit Watermark, offset: 0x13 */
            return s->TWFIFO;
        case 0x14: /**< UART FIFO Transmit Count, offset: 0x14 */
            return s->TCFIFO;
        case 0x15: /**< UART FIFO Receive Watermark, offset: 0x15 */
            return s->RWFIFO;
        case 0x16: /**< UART FIFO Receive Count, offset: 0x16 */
            return s->RCFIFO;
        case 0x18: /**< UART 7816 Control Register, offset: 0x18 */
            return s->C7816;
        case 0x19: /**< UART 7816 Interrupt Enable Register, offset: 0x19 */
            return s->IE7816;
        case 0x1A: /**< UART 7816 Interrupt Status Register, offset: 0x1A */
            return s->IS7816;
        case 0x1B: /**< UART 7816 Wait Parameter Register, offset: 0x1B */
            return s->WP7816T0;
        case 0x1C: /**< UART 7816 Wait N Register, offset: 0x1C */
            return s->WN7816;
        case 0x1D: /**< UART 7816 Wait FD Register, offset: 0x1D */
            return s->WF7816;
        case 0x1E: /**< UART 7816 Error Threshold Register, offset: 0x1E */
            return s->ET7816;
        case 0x1F: /**< UART 7816 Transmit Length Register, offset: 0x1F */
            return s->TL7816;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "kinetis_k64_uart: read at bad offset 0x%x\n", (int)offset);
            return 0;
    }
}

static const MemoryRegionOps kinetis_k64_uart_ops = {
    .read = kinetis_k64_uart_read,
    .write = kinetis_k64_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int kinetis_k64_uart_can_receive(void *opaque)
{
    kinetis_k64_uart_state *s = (kinetis_k64_uart_state *)opaque;
    
    if (s->RCFIFO == 0){
        return 1; //Can read a byte
    }else{
        return 0; //Cannot read a byte
    }
}

static void kinetis_k64_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    kinetis_k64_uart_state *s = (kinetis_k64_uart_state *)opaque;
    
    if (size > 0){
        if (buf != NULL){
            s->D = buf[0];
            s->RCFIFO = 1;
        }
    }
}

static void kinetis_k64_uart_realize(DeviceState *dev, Error **errp)
{
    kinetis_k64_uart_state *s = KINETIS_K64_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, kinetis_k64_uart_can_receive,
                             kinetis_k64_uart_receive, NULL, NULL,
                             s, NULL, true);
}

static Property kinetis_k64_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", kinetis_k64_uart_state, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void kinetis_k64_uart_init(Object *obj)
{
    kinetis_k64_uart_state *s = KINETIS_K64_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    
    memory_region_init_io(&s->iomem, obj, &kinetis_k64_uart_ops, s,
            TYPE_KINETIS_K64_UART, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void kinetis_k64_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_kinetis_k64_uart;
    dc->reset = kinetis_k64_uart_reset;
    dc->desc = "Kinetis K64 series UART";
    dc->hotpluggable = false;
    dc->props = kinetis_k64_uart_properties;
    dc->realize = kinetis_k64_uart_realize;
}

static const TypeInfo kinetis_k64_uart_info = {
    .name          = TYPE_KINETIS_K64_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(kinetis_k64_uart_state),
    .instance_init = kinetis_k64_uart_init,
    .class_init    = kinetis_k64_uart_class_init,
};

static void kinetis_k64_uart_register_types(void)
{
    type_register_static(&kinetis_k64_uart_info);
}

type_init(kinetis_k64_uart_register_types)