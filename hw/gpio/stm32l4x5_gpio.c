/*
 * STM32L4x5 GPIO (General Purpose Input/Ouput)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

#define GPIO_MODER 0x00
#define GPIO_OTYPER 0x04
#define GPIO_OSPEEDR 0x08
#define GPIO_PUPDR 0x0C
#define GPIO_IDR 0x10
#define GPIO_ODR 0x14
#define GPIO_BSRR 0x18
#define GPIO_LCKR 0x1C
#define GPIO_AFRL 0x20
#define GPIO_AFRH 0x24
#define GPIO_BRR 0x28
#define GPIO_ASCR 0x2C
/*
 * DISCONNECTED_PINS isn't actually a GPIO register.
 * It exists to ensure that :
 * - push-pull output pins can't be set externally
 * - open-drain output pins can only be externally set to 0
 *
 * This field is accessed for test purposes.
 */
#define GPIO_DISCONNECTED_PINS 0x30

/* 0b11111111_11111111_00000000_00000000 */
#define RESERVED_BITS_MASK 0xFFFF0000

static void update_gpio_idr(Stm32l4x5GpioState *s);

static void stm32l4x5_gpio_reset_hold(Object *obj)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_GET_CLASS(obj);

    s->moder = sc->moder_reset;
    s->otyper = 0x00000000;
    s->ospeedr = sc->ospeedr_reset;
    s->pupdr = sc->pupdr_reset;
    s->idr = 0x00000000;
    s->odr = 0x00000000;
    s->lckr = 0x00000000;
    s->afrl = 0x00000000;
    s->afrh = 0x00000000;
    s->ascr = 0x00000000;

    s->disconnected_pins = 0xFFFF;
    s->pins_connected_high = 0x0000;
    update_gpio_idr(s);
}

static void stm32l4x5_gpio_set(void *opaque, int line, int level)
{
    Stm32l4x5GpioState *s = opaque;
    /*
     * The pin isn't set if line is configured in output mode
     * except if level is 0 and the output is open-drain.
     * This way there will be no short-circuit prone situations.
     */
    if ((extract32(s->moder, 2 * line, 2) == 1) &&
        !((extract32(s->otyper, line, 1) == 1) &&
          (level == 0))) {
        qemu_log_mask(LOG_GUEST_ERROR, "Line %d can't be driven externally\n",
                      line);
        return;
    }

    s->disconnected_pins &= ~(1 << line);
    if (level) {
        s->pins_connected_high |= (1 << line);
    } else {
        s->pins_connected_high &= ~(1 << line);
    }
    trace_stm32l4x5_gpio_pins(s->disconnected_pins,
                              s->pins_connected_high);
    update_gpio_idr(s);
}


static void update_gpio_idr(Stm32l4x5GpioState *s)
{
    uint32_t new_idr_mask = 0;
    uint32_t new_idr = s->odr;

    for (int i = 0; i < 16; i++) {
        /* output mode */
        if (extract32(s->moder, 2 * i, 2) == 1) {
            if (extract32(s->otyper, i, 1) == 0) {
                /* push-pull */
                new_idr_mask |= (1 << i);
            } else if (!(s->odr & (1 << i))) {
                /* open-drain ODR 0 */
                new_idr_mask |= (1 << i);
            } else if ((s->disconnected_pins & (1 << i)) &&
                       (extract32(s->pupdr, 2 * i, 2) == 1)) {
                /* open-drain pull-up ODR 1 with disconnected pin */
                new_idr_mask |= (1 << i);
            } else if ((s->disconnected_pins & (1 << i)) &&
                       (extract32(s->pupdr, 2 * i, 2) == 2)) {
                /* open-drain pull-down ODR 1 with disconnected pin */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            } else if (!(s->pins_connected_high & (1 << i))) {
                /* open-drain ODR 1 with pin connected low */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
            /*
             * The only case left is for open-drain ODR 1
             * with disconnected pin without pull-up or pull-down :
             * the value is floating.
             */
        /* input or analog mode with connected pin */
        } else if (!(s->disconnected_pins & (1 << i))) {
            if (s->pins_connected_high & (1 << i)) {
                /* pin high */
                new_idr_mask |= (1 << i);
                new_idr |= (1 << i);
            } else {
                /* pin low */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
        /* input or analog mode with disconnected pin */
        } else {
            if (extract32(s->pupdr, 2 * i, 2) == 1) {
                /* pull-up */
                new_idr_mask |= (1 << i);
                new_idr |= (1 << i);
            } else if (extract32(s->pupdr, 2 * i, 2) == 2) {
                /* pull-down */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
            /*
             * The only case left is for a disconnected pin
             * without pull-up or pull-down :
             * the value is floating.
             */
        }
    }

    uint32_t old_idr = s->idr;
    s->idr = (old_idr & ~new_idr_mask) | (new_idr & new_idr_mask);
    trace_stm32l4x5_gpio_update_idr(old_idr, s->idr);

    for (int i = 0; i < 16; i++) {
        if (new_idr_mask & (1 << i)) {
            if ((new_idr & (1 << i)) > (old_idr & (1 << i))) {
                qemu_irq_raise(s->pin[i]);
            } else if ((new_idr & (1 << i)) < (old_idr & (1 << i))) {
                qemu_irq_lower(s->pin[i]);
            }
        }
    }
}

/*
 * Return pins both configured in output mode
 * and externally driven (except pins in open-drain
 * mode externally set to 0).
 */
static uint32_t get_gpio_pins_to_disconnect(Stm32l4x5GpioState *s)
{
    uint32_t pins_to_disconnect = 0;
    for (int i = 0; i < 16; i++) {
        /* for each connected pin in output mode */
        if ((~s->disconnected_pins & (1 << i)) &&
            (extract32(s->moder, 2 * i, 2) == 1)) {
            /* if either push-pull or high level */
            if ((extract32(s->otyper, i, 1) == 0) ||
                (extract16(s->pins_connected_high, i, 1)) == 1) {
                pins_to_disconnect |= (1 << i);
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Line %d can't be driven externally\n",
                              i);
            }
        }
    }
    return pins_to_disconnect;
}

/*
 * Set field `disconnected_pins` and call `update_gpio_idr()`
 */
static void disconnect_gpio_pins(Stm32l4x5GpioState *s, uint16_t lines)
{
    s->disconnected_pins |= lines;
    trace_stm32l4x5_gpio_pins(s->disconnected_pins,
                              s->pins_connected_high);
    update_gpio_idr(s);
}

static void stm32l4x5_gpio_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Stm32l4x5GpioState *s = opaque;

    uint32_t value = val64;
    trace_stm32l4x5_gpio_write(addr, val64);

    switch (addr) {
    case GPIO_MODER:
        s->moder = value;
        disconnect_gpio_pins(s, get_gpio_pins_to_disconnect(s));
        qemu_log_mask(LOG_UNIMP,
                      "%s: Analog and AF modes aren't supported\n\
                       Analog and AF mode behave like input mode\n",
                      __func__);
        return;
    case GPIO_OTYPER:
        s->otyper = value & ~RESERVED_BITS_MASK;
        disconnect_gpio_pins(s, get_gpio_pins_to_disconnect(s));
        return;
    case GPIO_OSPEEDR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Changing I/O output speed isn't supported\n\
                       I/O speed is already maximal\n",
                      __func__);
        s->ospeedr = value;
        return;
    case GPIO_PUPDR:
        s->pupdr = value;
        update_gpio_idr(s);
        return;
    case GPIO_IDR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: GPIO->IDR is read-only\n",
                      __func__);
        return;
    case GPIO_ODR:
        s->odr = value & ~RESERVED_BITS_MASK;
        update_gpio_idr(s);
        return;
    case GPIO_BSRR: {
        uint32_t bits_to_reset = (value & RESERVED_BITS_MASK) >> 16;
        uint32_t bits_to_set = value & ~RESERVED_BITS_MASK;
        /* If both BSx and BRx are set, BSx has priority.*/
        s->odr &= ~bits_to_reset;
        s->odr |= bits_to_set;
        update_gpio_idr(s);
        return;
    }
    case GPIO_LCKR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Locking port bits configuration isn't supported\n",
                      __func__);
        s->lckr = value & ~RESERVED_BITS_MASK;
        return;
    case GPIO_AFRL:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Alternate functions aren't supported\n",
                      __func__);
        s->afrl = value;
        return;
    case GPIO_AFRH:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Alternate functions aren't supported\n",
                      __func__);
        s->afrh = value;
        return;
    case GPIO_BRR: {
        uint32_t bits_to_reset = value & ~RESERVED_BITS_MASK;
        s->odr &= ~bits_to_reset;
        update_gpio_idr(s);
        return;
    }
    case GPIO_ASCR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: ADC function isn't supported\n",
                      __func__);
        s->ascr = value & ~RESERVED_BITS_MASK;
        return;
    /* a tweak to enable the qtest checking disconnected pins */
    case GPIO_DISCONNECTED_PINS:
        disconnect_gpio_pins(s, value);
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static uint64_t stm32l4x5_gpio_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Stm32l4x5GpioState *s = opaque;

    trace_stm32l4x5_gpio_read(addr);

    switch (addr) {
    case GPIO_MODER:
        return s->moder;
    case GPIO_OTYPER:
        return s->otyper;
    case GPIO_OSPEEDR:
        return s->ospeedr;
    case GPIO_PUPDR:
        return s->pupdr;
    case GPIO_IDR:
        return s->idr;
    case GPIO_ODR:
        return s->odr;
    case GPIO_BSRR:
        return 0;
    case GPIO_LCKR:
        return s->lckr;
    case GPIO_AFRL:
        return s->afrl;
    case GPIO_AFRH:
        return s->afrh;
    case GPIO_BRR:
        return 0;
    case GPIO_ASCR:
        return s->ascr;
    /* a tweak to enable the qtest checking disconnected pins */
    case GPIO_DISCONNECTED_PINS:
        return s->disconnected_pins;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static const MemoryRegionOps stm32l4x5_gpio_ops = {
    .read = stm32l4x5_gpio_read,
    .write = stm32l4x5_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void stm32l4x5_gpio_init(Object *obj)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_gpio_ops, s,
                          TYPE_STM32L4X5_GPIO, 0x400);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_out(DEVICE(obj), s->pin, GPIO_NUM_PINS);
    qdev_init_gpio_in(DEVICE(obj), stm32l4x5_gpio_set, GPIO_NUM_PINS);

    s->clk = qdev_init_clock_in(DEVICE(s), "clk", NULL, s, 0);
}

static void stm32l4x5_gpio_realize(DeviceState *dev, Error **errp)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(dev);
    if (!clock_has_source(s->clk)) {
        error_setg(errp, "GPIO: clk input must be connected");
        return;
    }
}

static const VMStateDescription vmstate_stm32l4x5_gpio = {
    .name = TYPE_STM32L4X5_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32(moder, Stm32l4x5GpioState),
        VMSTATE_UINT32(otyper, Stm32l4x5GpioState),
        VMSTATE_UINT32(ospeedr, Stm32l4x5GpioState),
        VMSTATE_UINT32(pupdr, Stm32l4x5GpioState),
        VMSTATE_UINT32(idr, Stm32l4x5GpioState),
        VMSTATE_UINT32(odr, Stm32l4x5GpioState),
        VMSTATE_UINT32(lckr, Stm32l4x5GpioState),
        VMSTATE_UINT32(afrl, Stm32l4x5GpioState),
        VMSTATE_UINT32(afrh, Stm32l4x5GpioState),
        VMSTATE_UINT32(ascr, Stm32l4x5GpioState),
        VMSTATE_UINT16(disconnected_pins, Stm32l4x5GpioState),
        VMSTATE_UINT16(pins_connected_high, Stm32l4x5GpioState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32l4x5_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_stm32l4x5_gpio;
    dc->realize = stm32l4x5_gpio_realize;
    rc->phases.hold = stm32l4x5_gpio_reset_hold;
}

static void stm32l4x5_gpio_a_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xABFFFFFF;
    sc->ospeedr_reset = 0x0C000000;
    sc->pupdr_reset = 0x64000000;
}

static void stm32l4x5_gpio_b_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFEBF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000100;
}

static void stm32l4x5_gpio_c_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFFFF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static void stm32l4x5_gpio_d_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFFFF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static void stm32l4x5_gpio_e_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFFFF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static void stm32l4x5_gpio_f_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFFFF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static void stm32l4x5_gpio_g_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0xFFFFFFFF;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static void stm32l4x5_gpio_h_class_init(ObjectClass *klass, void *data)
{
    Stm32l4x5GpioClass *sc = STM32L4X5_GPIO_CLASS(klass);

    sc->moder_reset = 0x0000000F;
    sc->ospeedr_reset = 0x00000000;
    sc->pupdr_reset = 0x00000000;
}

static const TypeInfo stm32l4x5_gpio_types[] = {
    {
        .name = TYPE_STM32L4X5_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Stm32l4x5GpioState),
        .instance_init = stm32l4x5_gpio_init,
        .class_size     = sizeof(Stm32l4x5GpioClass),
        .class_init = stm32l4x5_gpio_class_init,
        .abstract = true,
    }, {
        .name = TYPE_STM32L4X5_GPIO_A,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_a_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_B,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_b_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_C,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_c_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_D,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_d_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_E,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_e_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_F,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_f_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_G,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_g_class_init,
    }, {
        .name = TYPE_STM32L4X5_GPIO_H,
        .parent = TYPE_STM32L4X5_GPIO,
        .class_init = stm32l4x5_gpio_h_class_init,
    },
};

DEFINE_TYPES(stm32l4x5_gpio_types)
