/*
 * PCA9552 I2C LED blinker
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/misc/pca9552.h"

#define PCA9552_INPUT0   0 /* read only input register 0 */
#define PCA9552_INPUT1   1 /* read only input register 1  */
#define PCA9552_PSC0     2 /* read/write frequency prescaler 0 */
#define PCA9552_PWM0     3 /* read/write PWM register 0 */
#define PCA9552_PSC1     4 /* read/write frequency prescaler 1 */
#define PCA9552_PWM1     5 /* read/write PWM register 1 */
#define PCA9552_LS0      6 /* read/write LED0 to LED3 selector */
#define PCA9552_LS1      7 /* read/write LED4 to LED7 selector */
#define PCA9552_LS2      8 /* read/write LED8 to LED11 selector */
#define PCA9552_LS3      9 /* read/write LED12 to LED15 selector */

#define PCA9552_LED_ON   0x0
#define PCA9552_LED_OFF  0x1
#define PCA9552_LED_PWM0 0x2
#define PCA9552_LED_PWM1 0x3

static uint8_t pca9552_pin_get_config(PCA9552State *s, int pin)
{
    uint8_t reg   = PCA9552_LS0 + (pin / 4);
    uint8_t shift = (pin % 4) << 1;

    return (s->regs[reg] >> shift) & 0x3;
}

static void pca9552_update_pin_input(PCA9552State *s)
{
    int i;

    for (i = 0; i < 16; i++) {
        uint8_t input_reg = PCA9552_INPUT0 + (i / 8);
        uint8_t input_shift = (i % 8);
        uint8_t config = pca9552_pin_get_config(s, i);

        switch (config) {
        case PCA9552_LED_ON:
            s->regs[input_reg] |= 1 << input_shift;
            break;
        case PCA9552_LED_OFF:
            s->regs[input_reg] &= ~(1 << input_shift);
            break;
        case PCA9552_LED_PWM0:
        case PCA9552_LED_PWM1:
            /* ??? */
        default:
            break;
        }
    }
}

static void pca9552_read(PCA9552State *s)
{
    uint8_t reg = s->pointer & 0xf;

    s->len = 0;

    switch (reg) {
    case PCA9552_INPUT0:
    case PCA9552_INPUT1:
    case PCA9552_PSC0:
    case PCA9552_PWM0:
    case PCA9552_PSC1:
    case PCA9552_PWM1:
    case PCA9552_LS0:
    case PCA9552_LS1:
    case PCA9552_LS2:
    case PCA9552_LS3:
        s->buf[s->len++] = s->regs[reg];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read to register %d\n",
                      __func__, reg);
    }
}

static void pca9552_write(PCA9552State *s)
{
    uint8_t reg = s->pointer & 0xf;

    switch (reg) {
    case PCA9552_PSC0:
    case PCA9552_PWM0:
    case PCA9552_PSC1:
    case PCA9552_PWM1:
        s->regs[reg] = s->buf[0];
        break;

    case PCA9552_LS0:
    case PCA9552_LS1:
    case PCA9552_LS2:
    case PCA9552_LS3:
        s->regs[reg] = s->buf[0];
        pca9552_update_pin_input(s);
        break;

    case PCA9552_INPUT0:
    case PCA9552_INPUT1:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write to register %d\n",
                      __func__, reg);
    }
}

static int pca9552_recv(I2CSlave *i2c)
{
    PCA9552State *s = PCA9552(i2c);

    if (s->len < sizeof(s->buf)) {
        return s->buf[s->len++];
    } else {
        return 0xff;
    }
}

static int pca9552_send(I2CSlave *i2c, uint8_t data)
{
    PCA9552State *s = PCA9552(i2c);

    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else {
        if (s->len <= sizeof(s->buf)) {
            s->buf[s->len - 1] = data;
        }
        s->len++;
        pca9552_write(s);
    }

    return 0;
}

static int pca9552_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA9552State *s = PCA9552(i2c);

    if (event == I2C_START_RECV) {
        pca9552_read(s);
    }

    s->len = 0;
    return 0;
}

static const VMStateDescription pca9552_vmstate = {
    .name = "PCA9552",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, PCA9552State),
        VMSTATE_UINT8(pointer, PCA9552State),
        VMSTATE_UINT8_ARRAY(buf, PCA9552State, 1),
        VMSTATE_UINT8_ARRAY(regs, PCA9552State, PCA9552_NR_REGS),
        VMSTATE_I2C_SLAVE(i2c, PCA9552State),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9552_reset(DeviceState *dev)
{
    PCA9552State *s = PCA9552(dev);

    s->regs[PCA9552_PSC0] = 0xFF;
    s->regs[PCA9552_PWM0] = 0x80;
    s->regs[PCA9552_PSC1] = 0xFF;
    s->regs[PCA9552_PWM1] = 0x80;
    s->regs[PCA9552_LS0] = 0x55; /* all OFF */
    s->regs[PCA9552_LS1] = 0x55;
    s->regs[PCA9552_LS2] = 0x55;
    s->regs[PCA9552_LS3] = 0x55;

    pca9552_update_pin_input(s);
}

static void pca9552_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pca9552_event;
    k->recv = pca9552_recv;
    k->send = pca9552_send;
    dc->reset = pca9552_reset;
    dc->vmsd = &pca9552_vmstate;
}

static const TypeInfo pca9552_info = {
    .name          = TYPE_PCA9552,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCA9552State),
    .class_init    = pca9552_class_init,
};

static void pca9552_register_types(void)
{
    type_register_static(&pca9552_info);
}

type_init(pca9552_register_types)
