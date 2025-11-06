/*
 * ASPEED Serial GPIO Controller
 *
 * Copyright 2025 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/aspeed_sgpio.h"
#include "hw/registerfields.h"

/* AST2700 SGPIO Register Address Offsets */
REG32(SGPIO_INT_STATUS_0, 0x40)
REG32(SGPIO_INT_STATUS_1, 0x44)
REG32(SGPIO_INT_STATUS_2, 0x48)
REG32(SGPIO_INT_STATUS_3, 0x4C)
REG32(SGPIO_INT_STATUS_4, 0x50)
REG32(SGPIO_INT_STATUS_5, 0x54)
REG32(SGPIO_INT_STATUS_6, 0x58)
REG32(SGPIO_INT_STATUS_7, 0x5C)
/* AST2700 SGPIO_0 - SGPIO_255 Control Register */
REG32(SGPIO_0_CONTROL, 0x80)
    SHARED_FIELD(SGPIO_SERIAL_OUT_VAL, 0, 1)
    SHARED_FIELD(SGPIO_PARALLEL_OUT_VAL, 1, 1)
    SHARED_FIELD(SGPIO_INT_EN, 2, 1)
    SHARED_FIELD(SGPIO_INT_TYPE0, 3, 1)
    SHARED_FIELD(SGPIO_INT_TYPE1, 4, 1)
    SHARED_FIELD(SGPIO_INT_TYPE2, 5, 1)
    SHARED_FIELD(SGPIO_RESET_POLARITY, 6, 1)
    SHARED_FIELD(SGPIO_RESERVED_1, 7, 2)
    SHARED_FIELD(SGPIO_INPUT_MASK, 9, 1)
    SHARED_FIELD(SGPIO_PARALLEL_EN, 10, 1)
    SHARED_FIELD(SGPIO_PARALLEL_IN_MODE, 11, 1)
    SHARED_FIELD(SGPIO_INTERRUPT_STATUS, 12, 1)
    SHARED_FIELD(SGPIO_SERIAL_IN_VAL, 13, 1)
    SHARED_FIELD(SGPIO_PARALLEL_IN_VAL, 14, 1)
    SHARED_FIELD(SGPIO_RESERVED_2, 15, 12)
    SHARED_FIELD(SGPIO_WRITE_PROTECT, 31, 1)
REG32(SGPIO_255_CONTROL, 0x47C)

static uint64_t aspeed_sgpio_2700_read_int_status_reg(AspeedSGPIOState *s,
                                uint32_t reg)
{
    /* TODO: b/430606659 - Implement aspeed_sgpio_2700_read_int_status_reg */
    return 0;
}

static uint64_t aspeed_sgpio_2700_read_control_reg(AspeedSGPIOState *s,
                                uint32_t reg)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t pin = reg - R_SGPIO_0_CONTROL;
    if (pin >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, pin);
        return 0;
    }
    return s->ctrl_regs[pin];
}

static void aspeed_sgpio_2700_write_control_reg(AspeedSGPIOState *s,
                                uint32_t reg, uint64_t data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t pin = reg - R_SGPIO_0_CONTROL;
    if (pin >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, pin);
        return;
    }
    s->ctrl_regs[pin] = data;
}

static uint64_t aspeed_sgpio_2700_read(void *opaque, hwaddr offset,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t value = 0;
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_INT_STATUS_0 ... R_SGPIO_INT_STATUS_7:
        aspeed_sgpio_2700_read_int_status_reg(s, reg);
        break;
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        value = aspeed_sgpio_2700_read_control_reg(s, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return 0;
    }

    return value;
}

static void aspeed_sgpio_2700_write(void *opaque, hwaddr offset, uint64_t data,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_INT_STATUS_0 ... R_SGPIO_INT_STATUS_7:
        break;
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        aspeed_sgpio_2700_write_control_reg(s, reg, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      PRIx64"\n", __func__, offset);
        return;
    }
}

static bool aspeed_sgpio_get_pin_level(AspeedSGPIOState *s, int pin)
{
    uint32_t value = s->ctrl_regs[pin >> 1];
    bool is_input = !(pin % 2);
    uint32_t bit_mask = 0;

    if (is_input) {
        bit_mask = SGPIO_SERIAL_IN_VAL_MASK;
    } else {
        bit_mask = SGPIO_SERIAL_OUT_VAL_MASK;
    }

    return value & bit_mask;
}

static void aspeed_sgpio_set_pin_level(AspeedSGPIOState *s, int pin, bool level)
{
    uint32_t value = s->ctrl_regs[pin >> 1];
    bool is_input = !(pin % 2);
    uint32_t bit_mask = 0;

    if (is_input) {
        bit_mask = SGPIO_SERIAL_IN_VAL_MASK;
    } else {
        bit_mask = SGPIO_SERIAL_OUT_VAL_MASK;
    }

    if (level) {
        value |= bit_mask;
    } else {
        value &= ~bit_mask;
    }
    s->ctrl_regs[pin >> 1] = value;
    /* TODO: b/430606659 - Implement the SGPIO Interrupt */
}

static void aspeed_sgpio_get_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    bool level = true;
    int pin = 0xfff;
    AspeedSGPIOState *s = ASPEED_SGPIO(obj);

    if (sscanf(name, "sgpio%d", &pin) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    level = aspeed_sgpio_get_pin_level(s, pin);
    visit_type_bool(v, name, &level, errp);
}

static void aspeed_sgpio_set_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    bool level;
    int pin = 0xfff;
    AspeedSGPIOState *s = ASPEED_SGPIO(obj);

    if (!visit_type_bool(v, name, &level, errp)) {
        return;
    }
    if (sscanf(name, "sgpio%d", &pin) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    aspeed_sgpio_set_pin_level(s, pin, level);

}
static const MemoryRegionOps aspeed_gpio_2700_ops = {
  .read       = aspeed_sgpio_2700_read,
  .write      = aspeed_sgpio_2700_write,
  .endianness = DEVICE_LITTLE_ENDIAN,
  .valid.min_access_size = 4,
  .valid.max_access_size = 4,
};

static void aspeed_sgpio_realize(DeviceState *dev, Error **errp)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);

    /* Interrupt parent line */
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), agc->reg_ops, s,
                          TYPE_ASPEED_SGPIO, agc->mem_size);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_sgpio_init(Object *obj)
{
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR * 2; i++) {
        char *name = g_strdup_printf("sgpio%d", i);
        object_property_add(obj, name, "bool", aspeed_sgpio_get_pin,
                            aspeed_sgpio_set_pin, NULL, NULL);
        g_free(name);
    }
}

static void aspeed_sgpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sgpio_realize;
    dc->desc = "Aspeed SGPIO Controller";
}

static void aspeed_sgpio_2700_class_init(ObjectClass *klass, const void *data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_CLASS(klass);
    agc->nr_sgpio_pin_pairs = 256;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_gpio_2700_ops;
}

static const TypeInfo aspeed_sgpio_info = {
    .name           = TYPE_ASPEED_SGPIO,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AspeedSGPIOState),
    .class_size     = sizeof(AspeedSGPIOClass),
    .class_init     = aspeed_sgpio_class_init,
    .abstract       = true,
};

static const TypeInfo aspeed_sgpio_ast2700_info = {
  .name           = TYPE_ASPEED_SGPIO "-ast2700",
  .parent         = TYPE_ASPEED_SGPIO,
  .class_init     = aspeed_sgpio_2700_class_init,
  .instance_init  = aspeed_sgpio_init,
};

static void aspeed_sgpio_register_types(void)
{
    type_register_static(&aspeed_sgpio_info);
    type_register_static(&aspeed_sgpio_ast2700_info);
}

type_init(aspeed_sgpio_register_types);
