/*
 * QEMU K230 GPIO Controller
 *
 * Copyright (c) 2025 Wang Guochun <wdasn99@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18), section 12.5 GPIO
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 */

#include "qemu/osdep.h"
#include "hw/gpio/k230_gpio.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static void k230_gpio_update_int(K230GPIOState *s)
{
    uint32_t masked_status = s->raw_intstatus & ~s->intmask & s->inten;
    for (int i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
        if (s->irq[i]) {
            qemu_set_irq(s->irq[i], extract32(masked_status, i, 1));
        }
    }
}

static void k230_gpio_set_int_line(K230GPIOState *s, int line, int level)
{
    if (line >= K230_GPIO_PINS_PER_GROUP) {
        return;
    }

    uint32_t prev_level = extract32(s->ext_porta, line, 1);
    uint32_t curr_level = level;

    s->ext_porta = deposit32(s->ext_porta, line, 1, curr_level);

    if (extract32(s->swporta_ddr, line, 1)) {
        return;
    }

    if (extract32(s->swporta_ctl, line, 1)) {
        return;
    }

    uint32_t pol = extract32(s->int_polarity, line, 1);
    uint32_t both = extract32(s->int_bothedge, line, 1);
    uint32_t is_level = !extract32(s->inttype_level, line, 1);

    if (both) {
        if (prev_level != curr_level) {
            s->raw_intstatus |= (1U << line);
        }
    } else if (is_level) {
        if (curr_level == pol) {
            s->raw_intstatus |= (1U << line);
        } else {
            s->raw_intstatus &= ~(1U << line);
        }
    } else {
        if (prev_level != curr_level && curr_level == pol) {
            s->raw_intstatus |= (1U << line);
        }
    }
}

static void k230_gpio_set(void *opaque, int line, int level)
{
    K230GPIOState *s = K230_GPIO(opaque);

    k230_gpio_set_int_line(s, line, level);
    k230_gpio_update_int(s);
}

static void k230_gpio_set_all_output_lines(K230GPIOState *s)
{
    int i;

    for (i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
        if (extract32(s->swporta_ddr, i, 1) && s->output[i]) {
            qemu_set_irq(s->output[i], extract32(s->swporta_dr, i, 1));
        }
    }
}

static uint64_t k230_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    K230GPIOState *s = K230_GPIO(opaque);

    switch (offset) {
    case K230_GPIO_SWPORTA_DR:
        return s->swporta_dr;
    case K230_GPIO_SWPORTA_DDR:
        return s->swporta_ddr;
    case K230_GPIO_SWPORTA_CTL:
        return s->swporta_ctl;
    case K230_GPIO_INTEN:
        return s->inten;
    case K230_GPIO_INTMASK:
        return s->intmask;
    case K230_GPIO_INTTYPE_LEVEL:
        return s->inttype_level;
    case K230_GPIO_INT_POLARITY:
        return s->int_polarity;
    case K230_GPIO_INTSTATUS:
        return s->raw_intstatus & ~s->intmask;
    case K230_GPIO_RAW_INTSTATUS:
        return s->raw_intstatus;
    case K230_GPIO_DEBOUNCE:
        return s->debounce;
    case K230_GPIO_EXT_PORTA:
        return (s->ext_porta & ~s->swporta_ddr) |
               (s->swporta_dr & s->swporta_ddr);
    case K230_GPIO_LS_SYNC:
        return s->ls_sync;
    case K230_GPIO_ID_CODE:
        return s->id_code;
    case K230_GPIO_INT_BOTHEDGE:
        return s->int_bothedge;
    case K230_GPIO_VER_ID_CODE:
        return s->ver_id_code;
    case K230_GPIO_CONFIG_REG2:
        return s->config_reg2;
    case K230_GPIO_CONFIG_REG1:
        return s->config_reg1;
    case K230_GPIO_PORTA_EOI:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_K230_GPIO, __func__, offset);
        return 0;
    }
}

static void k230_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    K230GPIOState *s = K230_GPIO(opaque);
    uint32_t curr_level, pol;

    switch (offset) {
    case K230_GPIO_SWPORTA_DR:
        s->swporta_dr = value;
        k230_gpio_set_all_output_lines(s);
        break;
    case K230_GPIO_SWPORTA_DDR: {
        uint32_t prev_ddr = s->swporta_ddr;
        s->swporta_ddr = value;
        k230_gpio_set_all_output_lines(s);
        for (int i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
            /* Skip pins whose direction did not change */
            if (extract32(prev_ddr, i, 1) == extract32(s->swporta_ddr, i, 1)) {
                continue;
            }
            /*
             * When switching back to input mode (and still in software
             * control), re-evaluate level-sensitive interrupts. Edge
             * interrupts are left untouched, matching the manual TRM v0.3.1.
             */
            if (extract32(s->swporta_ddr, i, 1) ||
                extract32(s->swporta_ctl, i, 1) ||
                extract32(s->inttype_level, i, 1)) {
                continue;
            }

            curr_level = extract32(s->ext_porta, i, 1);
            pol = extract32(s->int_polarity, i, 1);
            if (curr_level == pol) {
                s->raw_intstatus |= (1U << i);
            } else {
                s->raw_intstatus &= ~(1U << i);
            }
        }
        k230_gpio_update_int(s);
        break;
    }
    case K230_GPIO_SWPORTA_CTL: {
        uint32_t prev_ctl = s->swporta_ctl;
        s->swporta_ctl = value;
        for (int i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
            /* Skip pins whose control mode did not change */
            if (extract32(prev_ctl, i, 1) == extract32(s->swporta_ctl, i, 1)) {
                continue;
            }
            /*
             * When switching back from hardware to software control (and DDR
             * is input), re-evaluate level-sensitive interrupts. Edge
             * interrupts are left untouched, matching the manual TRM v0.3.1.
             */
            if (extract32(s->swporta_ddr, i, 1) ||
                extract32(s->swporta_ctl, i, 1) ||
                extract32(s->inttype_level, i, 1)) {
                continue;
            }

            curr_level = extract32(s->ext_porta, i, 1);
            pol = extract32(s->int_polarity, i, 1);
            if (curr_level == pol) {
                s->raw_intstatus |= (1U << i);
            } else {
                s->raw_intstatus &= ~(1U << i);
            }
        }
        k230_gpio_update_int(s);
        break;
    }
    case K230_GPIO_INTEN:
        s->inten = value;
        k230_gpio_update_int(s);
        break;
    case K230_GPIO_INTMASK:
        s->intmask = value;
        k230_gpio_update_int(s);
        break;
    case K230_GPIO_INTTYPE_LEVEL:
        s->inttype_level = value;
        break;
    case K230_GPIO_INT_POLARITY:
        s->int_polarity = value;
        break;
    case K230_GPIO_DEBOUNCE:
        s->debounce = value;
        break;
    case K230_GPIO_PORTA_EOI:
        for (int i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
            if (extract32(value, i, 1) && extract32(s->inttype_level, i, 1)) {
                s->raw_intstatus = deposit32(s->raw_intstatus, i, 1, 0);
            }
        }
        k230_gpio_update_int(s);
        break;
    case K230_GPIO_LS_SYNC:
        s->ls_sync = value;
        break;
    case K230_GPIO_ID_CODE:
        break;
    case K230_GPIO_INT_BOTHEDGE:
        s->int_bothedge = value;
        break;
    case K230_GPIO_VER_ID_CODE:
        break;
    case K230_GPIO_CONFIG_REG2:
        break;
    case K230_GPIO_CONFIG_REG1:
        break;
    case K230_GPIO_INTSTATUS:
        break;
    case K230_GPIO_RAW_INTSTATUS:
        break;
    case K230_GPIO_EXT_PORTA:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_K230_GPIO, __func__, offset);
        break;
    }
}

static const MemoryRegionOps k230_gpio_ops = {
    .read = k230_gpio_read,
    .write = k230_gpio_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_k230_gpio = {
    .name = TYPE_K230_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(swporta_dr, K230GPIOState),
        VMSTATE_UINT32(swporta_ddr, K230GPIOState),
        VMSTATE_UINT32(swporta_ctl, K230GPIOState),
        VMSTATE_UINT32(inten, K230GPIOState),
        VMSTATE_UINT32(intmask, K230GPIOState),
        VMSTATE_UINT32(inttype_level, K230GPIOState),
        VMSTATE_UINT32(int_polarity, K230GPIOState),
        VMSTATE_UINT32(raw_intstatus, K230GPIOState),
        VMSTATE_UINT32(debounce, K230GPIOState),
        VMSTATE_UINT32(ext_porta, K230GPIOState),
        VMSTATE_UINT32(ls_sync, K230GPIOState),
        VMSTATE_UINT32(int_bothedge, K230GPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void k230_gpio_enter_reset(Object *obj, ResetType type)
{
    K230GPIOState *s = K230_GPIO(obj);

    s->swporta_dr = 0;
    s->swporta_ddr = 0;
    s->swporta_ctl = 0;
    s->inten = 0;
    s->intmask = 0;
    s->inttype_level = 0;
    s->int_polarity = 0;
    s->raw_intstatus = 0;
    s->debounce = 0;
    s->porta_eoi = 0;
    s->ext_porta = 0;
    s->ls_sync = 0;
    s->id_code = 0;
    s->int_bothedge = 0;
    s->ver_id_code = 0;
    s->config_reg2 = 0;
    s->config_reg1 = 0;
}

static void k230_gpio_hold_reset(Object *obj, ResetType type)
{
    K230GPIOState *s = K230_GPIO(obj);

    k230_gpio_update_int(s);
}

static void k230_gpio_realize(DeviceState *dev, Error **errp)
{
    K230GPIOState *s = K230_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    memory_region_init_io(&s->iomem, OBJECT(s), &k230_gpio_ops, s,
                          TYPE_K230_GPIO, K230_GPIO_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(DEVICE(s), k230_gpio_set, K230_GPIO_PINS_PER_GROUP);
    qdev_init_gpio_out(DEVICE(s), s->output, K230_GPIO_PINS_PER_GROUP);

    for (i = 0; i < K230_GPIO_PINS_PER_GROUP; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static void k230_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_gpio_realize;
    rc->phases.enter = k230_gpio_enter_reset;
    rc->phases.hold = k230_gpio_hold_reset;
    dc->vmsd = &vmstate_k230_gpio;
    dc->desc = "K230 GPIO controller";
}

static const TypeInfo k230_gpio_info = {
    .name = TYPE_K230_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230GPIOState),
    .class_init = k230_gpio_class_init,
};

static void k230_gpio_register_types(void)
{
    type_register_static(&k230_gpio_info);
}

type_init(k230_gpio_register_types)
