/*
 * RP2040 GPIO IO bank emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_iobank0.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define IOBANK0_GPIO_STRIDE         0x8
#define IOBANK0_GPIO_STATUS         0x0
#define IOBANK0_GPIO_CTRL           0x4

#define IOBANK0_INTR                0x0f0
#define IOBANK0_PROC0_INTE          0x100
#define IOBANK0_PROC0_INTF          0x110
#define IOBANK0_PROC0_INTS          0x120
#define IOBANK0_PROC1_INTE          0x130
#define IOBANK0_PROC1_INTF          0x140
#define IOBANK0_PROC1_INTS          0x150
#define IOBANK0_DORMANT_INTE        0x160
#define IOBANK0_DORMANT_INTF        0x170
#define IOBANK0_DORMANT_INTS        0x180

#define IOBANK0_CTRL_RESET          0x1f
#define IOBANK0_CTRL_RW_MASK        0x33333f
#define IOBANK0_CTRL_FUNCSEL_MASK   0x1f
#define IOBANK0_FUNCSEL_UART        2
#define IOBANK0_INTR_EDGE_MASK      0xcccccccc
#define IOBANK0_INTR_LAST_EDGE_MASK 0x00cccccc
#define IOBANK0_IRQ_MASK            0xffffffff
#define IOBANK0_IRQ_LAST_MASK       0x00ffffff

#define ATOMIC_ALIAS_MASK           0x3000
#define ATOMIC_XOR                  0x1000
#define ATOMIC_SET                  0x2000
#define ATOMIC_CLR                  0x3000

static uint32_t rp2040_iobank0_apply_alias(uint32_t old, uint32_t value,
                                           hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static bool rp2040_iobank0_gpio_offset(hwaddr offset, unsigned *gpio,
                                       unsigned *reg)
{
    if (offset >= RP2040_IOBANK0_NUM_GPIOS * IOBANK0_GPIO_STRIDE) {
        return false;
    }

    *gpio = offset / IOBANK0_GPIO_STRIDE;
    *reg = offset & (IOBANK0_GPIO_STRIDE - 1);
    return *reg == IOBANK0_GPIO_STATUS || *reg == IOBANK0_GPIO_CTRL;
}

static bool rp2040_iobank0_bank_offset(hwaddr offset, hwaddr base,
                                       unsigned *bank)
{
    if (offset < base || offset >= base + RP2040_IOBANK0_IRQ_BANKS * 4 ||
        (offset & 0x3)) {
        return false;
    }

    *bank = (offset - base) / 4;
    return true;
}

static uint32_t rp2040_iobank0_irq_mask(unsigned bank)
{
    return bank == 3 ? IOBANK0_IRQ_LAST_MASK : IOBANK0_IRQ_MASK;
}

static uint32_t rp2040_iobank0_intr_edge_mask(unsigned bank)
{
    return bank == 3 ? IOBANK0_INTR_LAST_EDGE_MASK : IOBANK0_INTR_EDGE_MASK;
}

static uint32_t rp2040_iobank0_ints(uint32_t intr, uint32_t inte,
                                    uint32_t intf)
{
    return (intr & inte) | intf;
}

static bool rp2040_iobank0_gpio_is_uart(RP2040IoBank0State *s, unsigned gpio)
{
    return (s->ctrl[gpio] & IOBANK0_CTRL_FUNCSEL_MASK) == IOBANK0_FUNCSEL_UART;
}

bool rp2040_iobank0_uart0_tx_enabled(RP2040IoBank0State *s)
{
    return rp2040_iobank0_gpio_is_uart(s, 0);
}

bool rp2040_iobank0_uart0_rx_enabled(RP2040IoBank0State *s)
{
    return rp2040_iobank0_gpio_is_uart(s, 1);
}

bool rp2040_iobank0_uart1_tx_enabled(RP2040IoBank0State *s)
{
    return rp2040_iobank0_gpio_is_uart(s, 4);
}

bool rp2040_iobank0_uart1_rx_enabled(RP2040IoBank0State *s)
{
    return rp2040_iobank0_gpio_is_uart(s, 5);
}

static void rp2040_iobank0_update_uart_pins(RP2040IoBank0State *s)
{
    qemu_set_irq(s->uart0_pin[0], rp2040_iobank0_uart0_tx_enabled(s));
    qemu_set_irq(s->uart0_pin[1], rp2040_iobank0_uart0_rx_enabled(s));
    qemu_set_irq(s->uart1_pin[0], rp2040_iobank0_uart1_tx_enabled(s));
    qemu_set_irq(s->uart1_pin[1], rp2040_iobank0_uart1_rx_enabled(s));
}

static void rp2040_iobank0_update_irq(RP2040IoBank0State *s)
{
    bool proc0_level = false;
    bool proc1_level = false;
    int i;

    for (i = 0; i < RP2040_IOBANK0_IRQ_BANKS; i++) {
        proc0_level |= rp2040_iobank0_ints(s->intr[i], s->proc0_inte[i],
                                           s->proc0_intf[i]) != 0;
        proc1_level |= rp2040_iobank0_ints(s->intr[i], s->proc1_inte[i],
                                           s->proc1_intf[i]) != 0;
    }

    qemu_set_irq(s->proc0_irq, proc0_level);
    qemu_set_irq(s->proc1_irq, proc1_level);
}

static uint64_t rp2040_iobank0_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040IoBank0State *s = opaque;
    hwaddr offset = addr & 0xfff;
    unsigned gpio;
    unsigned reg;
    unsigned bank;
    uint64_t value;

    if (rp2040_iobank0_gpio_offset(offset, &gpio, &reg)) {
        value = reg == IOBANK0_GPIO_STATUS ? 0 : s->ctrl[gpio];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_INTR, &bank)) {
        value = s->intr[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTE, &bank)) {
        value = s->proc0_inte[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTF, &bank)) {
        value = s->proc0_intf[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTS, &bank)) {
        value = rp2040_iobank0_ints(s->intr[bank], s->proc0_inte[bank],
                                    s->proc0_intf[bank]);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTE, &bank)) {
        value = s->proc1_inte[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTF, &bank)) {
        value = s->proc1_intf[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTS, &bank)) {
        value = rp2040_iobank0_ints(s->intr[bank], s->proc1_inte[bank],
                                    s->proc1_intf[bank]);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTE,
                                          &bank)) {
        value = s->dormant_wake_inte[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTF,
                                          &bank)) {
        value = s->dormant_wake_intf[bank];
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTS,
                                          &bank)) {
        value = rp2040_iobank0_ints(s->intr[bank],
                                    s->dormant_wake_inte[bank],
                                    s->dormant_wake_intf[bank]);
    } else {
        value = 0;
        rp2040_log_unimplemented_read("iobank0", size,
                                      RP2040_IOBANK0_BASE + addr, offset,
                                      value);
    }

    return value;
}

static void rp2040_iobank0_write_irq_reg(uint32_t *reg, uint64_t value64,
                                         hwaddr alias, unsigned bank)
{
    *reg = rp2040_iobank0_apply_alias(*reg, value64, alias) &
           rp2040_iobank0_irq_mask(bank);
}

static void rp2040_iobank0_write(void *opaque, hwaddr addr, uint64_t value64,
                                 unsigned size)
{
    RP2040IoBank0State *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    unsigned gpio;
    unsigned reg;
    unsigned bank;
    uint32_t value = value64;

    if (rp2040_iobank0_gpio_offset(offset, &gpio, &reg)) {
        if (reg == IOBANK0_GPIO_CTRL) {
            s->ctrl[gpio] =
                rp2040_iobank0_apply_alias(s->ctrl[gpio], value, alias) &
                IOBANK0_CTRL_RW_MASK;
            if (gpio <= 1 || gpio == 4 || gpio == 5) {
                rp2040_iobank0_update_uart_pins(s);
            }
        }
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_INTR, &bank)) {
        s->intr[bank] &= ~(value & rp2040_iobank0_intr_edge_mask(bank));
        rp2040_iobank0_update_irq(s);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTE, &bank)) {
        rp2040_iobank0_write_irq_reg(&s->proc0_inte[bank], value, alias,
                                     bank);
        rp2040_iobank0_update_irq(s);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTF, &bank)) {
        rp2040_iobank0_write_irq_reg(&s->proc0_intf[bank], value, alias,
                                     bank);
        rp2040_iobank0_update_irq(s);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTE, &bank)) {
        rp2040_iobank0_write_irq_reg(&s->proc1_inte[bank], value, alias,
                                     bank);
        rp2040_iobank0_update_irq(s);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTF, &bank)) {
        rp2040_iobank0_write_irq_reg(&s->proc1_intf[bank], value, alias,
                                     bank);
        rp2040_iobank0_update_irq(s);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTE,
                                          &bank)) {
        rp2040_iobank0_write_irq_reg(&s->dormant_wake_inte[bank], value,
                                     alias, bank);
    } else if (rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTF,
                                          &bank)) {
        rp2040_iobank0_write_irq_reg(&s->dormant_wake_intf[bank], value,
                                     alias, bank);
    } else if (!rp2040_iobank0_bank_offset(offset, IOBANK0_PROC0_INTS,
                                           &bank) &&
               !rp2040_iobank0_bank_offset(offset, IOBANK0_PROC1_INTS,
                                           &bank) &&
               !rp2040_iobank0_bank_offset(offset, IOBANK0_DORMANT_INTS,
                                           &bank)) {
        rp2040_log_unimplemented_write("iobank0", size,
                                       RP2040_IOBANK0_BASE + addr, offset,
                                       value64);
    }
}

static const MemoryRegionOps rp2040_iobank0_ops = {
    .read = rp2040_iobank0_read,
    .write = rp2040_iobank0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_iobank0_reset(DeviceState *dev)
{
    RP2040IoBank0State *s = RP2040_IOBANK0(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->ctrl); i++) {
        s->ctrl[i] = IOBANK0_CTRL_RESET;
    }

    memset(s->intr, 0, sizeof(s->intr));
    memset(s->proc0_inte, 0, sizeof(s->proc0_inte));
    memset(s->proc0_intf, 0, sizeof(s->proc0_intf));
    memset(s->proc1_inte, 0, sizeof(s->proc1_inte));
    memset(s->proc1_intf, 0, sizeof(s->proc1_intf));
    memset(s->dormant_wake_inte, 0, sizeof(s->dormant_wake_inte));
    memset(s->dormant_wake_intf, 0, sizeof(s->dormant_wake_intf));
    rp2040_iobank0_update_irq(s);
    rp2040_iobank0_update_uart_pins(s);
}

static void rp2040_iobank0_init(Object *obj)
{
    RP2040IoBank0State *s = RP2040_IOBANK0(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_iobank0_ops, s,
                          "rp2040.iobank0", RP2040_IOBANK0_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->proc0_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->proc1_irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->uart0_pin, "uart0-pin",
                             ARRAY_SIZE(s->uart0_pin));
    qdev_init_gpio_out_named(DEVICE(obj), s->uart1_pin, "uart1-pin",
                             ARRAY_SIZE(s->uart1_pin));
}

static const VMStateDescription rp2040_iobank0_vmstate = {
    .name = TYPE_RP2040_IOBANK0,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ctrl, RP2040IoBank0State,
                             RP2040_IOBANK0_NUM_GPIOS),
        VMSTATE_UINT32_ARRAY(intr, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(proc0_inte, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(proc0_intf, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(proc1_inte, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(proc1_intf, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(dormant_wake_inte, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_UINT32_ARRAY(dormant_wake_intf, RP2040IoBank0State,
                             RP2040_IOBANK0_IRQ_BANKS),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_iobank0_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_iobank0_reset);
    dc->vmsd = &rp2040_iobank0_vmstate;
}

static const TypeInfo rp2040_iobank0_info = {
    .name          = TYPE_RP2040_IOBANK0,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040IoBank0State),
    .instance_init = rp2040_iobank0_init,
    .class_init    = rp2040_iobank0_class_init,
};

static void rp2040_iobank0_register_types(void)
{
    type_register_static(&rp2040_iobank0_info);
}
type_init(rp2040_iobank0_register_types)
