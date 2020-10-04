/*
 * AVR processors GPIO registers emulation.
 *
 * Copyright (C) 2020 Heecheol Yang <heecheol.yang@outlook.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/gpio/avr_gpio.h"
#include "hw/qdev-properties.h"

static void avr_gpio_reset(DeviceState *dev)
{
    AVRGPIOState *gpio = AVR_GPIO(dev);
    gpio->ddr_val = 0u;
    gpio->port_val = 0u;
}
static uint64_t avr_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    AVRGPIOState *s = (AVRGPIOState *)opaque;
    switch (offset) {
    case GPIO_PIN:
        /* Not implemented yet */
        break;
    case GPIO_DDR:
        return s->ddr_val;
        break;
    case GPIO_PORT:
        return s->port_val;
    default:
        g_assert_not_reached();
        break;
    }
    return 0;
}

static void avr_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned int size)
{
    AVRGPIOState *s = (AVRGPIOState *)opaque;
    switch (offset) {
    case GPIO_PIN:
        /* Not implemented yet */
        break;
    case GPIO_DDR:
        s->ddr_val = value & 0xF;
        break;
    case GPIO_PORT:
        s->port_val = value & 0xF;
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

static const MemoryRegionOps avr_gpio_ops = {
    .read = avr_gpio_read,
    .write = avr_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void avr_gpio_init(Object *obj)
{
    AVRGPIOState *s = AVR_GPIO(obj);
    memory_region_init_io(&s->mmio, obj, &avr_gpio_ops, s, TYPE_AVR_GPIO, 3);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}
static void avr_gpio_realize(DeviceState *dev, Error **errp)
{
    avr_gpio_reset(dev);
}


static void avr_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = avr_gpio_reset;
    dc->realize = avr_gpio_realize;
}

static const TypeInfo avr_gpio_info = {
    .name          = TYPE_AVR_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AVRGPIOState),
    .instance_init = avr_gpio_init,
    .class_init    = avr_gpio_class_init,
};

static void avr_gpio_register_types(void)
{
    type_register_static(&avr_gpio_info);
}

type_init(avr_gpio_register_types)
