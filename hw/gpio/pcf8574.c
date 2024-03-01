/*
 * NXP PCF8574 8-port I2C GPIO expansion chip.
 *
 * Copyright (c) 2024 KNS Group (YADRO).
 * Written by Dmitrii Sharikhin <d.sharikhin@yadro.com>
 *
 * This file is licensed under GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/gpio/pcf8574.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

/**
 * PCF8574 and compatible chips incorporate quasi-bidirectional
 * IO. Electrically it means that device sustain pull-up to line
 * unless IO port is configured as output _and_ driven low.
 * 
 * IO access is implemented as simple I2C single-byte read
 * or write operation. So, to configure line to input user write 1
 * to corresponding bit. To configure line to output and drive it low
 * user write 0 to corresponding bit.
 * 
 * In essence, user can think of quasi-bidirectional IO as
 * open-drain line, except presence of builtin rising edge acceleration
 * embedded in PCF8574 IC
 **/

OBJECT_DECLARE_SIMPLE_TYPE(PCF8574State, PCF8574)

struct PCF8574State {
    I2CSlave parent_obj;
    uint8_t  input;      /* external electrical line state */
    uint8_t  output;     /* Pull-up (1) or drive low (0) on bit */
    qemu_irq handler[8];
    qemu_irq *gpio_in;
};

static void pcf8574_reset(DeviceState *dev)
{
    PCF8574State *s = PCF8574(dev);
    s->input  = 0xFF;
    s->output = 0xFF;
}

static inline uint8_t pcf8574_line_state(PCF8574State *s)
{
    // we driving line low or external circuit does that
    return s->input & s->output; 
}

static uint8_t pcf8574_rx(I2CSlave *i2c)
{
    return pcf8574_line_state(PCF8574(i2c));
}

static int pcf8574_tx(I2CSlave *i2c, uint8_t data)
{
    PCF8574State *s = PCF8574(i2c);
    uint8_t prev;
    uint8_t diff;
    uint8_t actual;
    int line = 0;

    prev = pcf8574_line_state(s);
    s->output = data;
    actual = pcf8574_line_state(s);

    for (diff = (actual ^ prev); diff; diff &= ~(1 << line))
    {
        line = ctz32(diff);
        if (s->handler[line])
            qemu_set_irq(s->handler[line], (actual >> line) & 1);
    }

    return 0;
}

static const VMStateDescription vmstate_pcf8574 = {
    .name = "pcf8574",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(input,  PCF8574State),
        VMSTATE_UINT8(output, PCF8574State),
        VMSTATE_I2C_SLAVE(parent_obj, PCF8574State),
        VMSTATE_END_OF_LIST()
    }
};

static void pcf8574_gpio_set(void *opaque, int line, int level)
{
    PCF8574State *s = (PCF8574State *) opaque;
    assert(line >= 0 && line < ARRAY_SIZE(s->handler)); 

    if (level)
        s->input |=  (1 << line);
    else
        s->input &= ~(1 << line);
}

static void pcf8574_realize(DeviceState *dev, Error **errp)
{
    PCF8574State *s = PCF8574(dev);

    qdev_init_gpio_in(dev, pcf8574_gpio_set, ARRAY_SIZE(s->handler));
    qdev_init_gpio_out(dev, s->handler, ARRAY_SIZE(s->handler));
}

static void pcf8574_class_init(ObjectClass *klass, void *data)
{
    DeviceClass   *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k  = I2C_SLAVE_CLASS(klass);

    k->recv     = pcf8574_rx;
    k->send     = pcf8574_tx;
    dc->realize = pcf8574_realize;
    dc->reset   = pcf8574_reset;
    dc->vmsd    = &vmstate_pcf8574;
}

static const TypeInfo pcf8574_info = {
    .name          = TYPE_PCF8574,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCF8574State),
    .class_init    = pcf8574_class_init,
};

static void pcf8574_register_types(void)
{
    type_register_static(&pcf8574_info);
}

type_init(pcf8574_register_types)
