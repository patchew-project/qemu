/*
 * Synopsys Desgignware general purpose input/output register definition
 *
 * Based on sifive_gpio.c and imx_gpio.c
 *
 * Copyright 2022 Sifive, Inc.
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/designware_gpio.h"
#include "migration/vmstate.h"
#include "trace.h"

/* only bank A can provide interrupts */
static void update_output_irqs(DESIGNWAREGPIOState *s)
{
    struct DESIGNWAREGPIOBank *bank = &s->bank[0];
    uint32_t level_irqs, edge_irqs = 0;

    /* re-calculate interrupts for raw_int_status */
    level_irqs = bank->dr_val ^ s->int_polarity;
    level_irqs &= ~s->int_level;

    edge_irqs = bank->dr_val ^ bank->last_dr_val;
    edge_irqs &= s->int_level;
    bank->last_dr_val = bank->dr_val;

    /* update irq from raw-status and the mask */
    s->int_status_raw = level_irqs | edge_irqs;
    s->int_status = s->int_status_raw & s->int_mask;

    qemu_set_irq(s->irq, s->int_status ? 1 : 0);
    trace_designware_gpio_update_output_irq(s->int_status);
}

static void update_state(DESIGNWAREGPIOState *s)
{
    struct DESIGNWAREGPIOBank *bank;
    int banknr, basenr, nr;

    for (banknr = 0; banknr < DESIGNWARE_GPIO_BANKS; banknr++) {
        basenr = banknr * DESIGNWARE_GPIO_NR_PER_BANK;
        bank = &s->bank[banknr];

        /* check for data-direction differences */
        if (bank->ddr & bank->in) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "GPIO bank %d: pins shorted, DDR=%x, IN=%x, overlap=%x\n",
                          banknr, bank->ddr, bank->in, bank->ddr & bank->in);
        }

        bank->dr_val = (bank->dr & bank->ddr) | (bank->in & ~bank->ddr);

        /* update any outputs marked as outputs */
        for (nr = 0; nr < DESIGNWARE_GPIO_NR_PER_BANK; nr++) {
            if (!extract32(bank->ddr, nr, 1))
                continue;
            qemu_set_irq(s->output[basenr+nr], extract32(bank->dr_val, nr, 1));
        }
    }

    update_output_irqs(s);
}


static uint64_t designware_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    struct DESIGNWAREGPIOState *s = DESIGNWARE_GPIO(opaque);
    struct DESIGNWAREGPIOBank *bank;
    hwaddr banknr, reg;
    uint64_t r = 0;
    bool handled = true;

    if (offset < (REG_SWPORTD_DDR + 4)) {
        banknr = offset / REG_SWPORT_DR_STRIDE;
        reg = offset % REG_SWPORT_DR_STRIDE;
        bank = &s->bank[banknr];

        switch (reg) {
        case REG_SWPORTA_DR:
            r = bank->dr;
            break;
        case REG_SWPORTA_DDR:
            r = bank->ddr;
            break;
        default:
            handled = false;
        }
    } else {
        switch (offset) {
        case REG_INTEN:
            r= s->int_en;
            break;
        case REG_INTMASK:
            r = s->int_mask;
            break;
        case REG_INTTYPE_LEVEL:
            r = s->int_level;
            break;
        case REG_INT_POLARITY:
            r = s->int_polarity;
            break;
        case REG_INTSTATUS:
            r = s->int_status;
            break;
        case REG_INTSTATUS_RAW:
            r = s->int_status_raw;
            break;
        case REG_PORTA_DEBOUNCE:
            r = s->porta_debounce;
            break;
        case REG_PORTA_EOI:
            r = 0x0;    /* write only */
            break;
        case REG_EXT_PORTA:
            r = s->bank[0].dr_val;
            break;
        case REG_EXT_PORTB:
            r = s->bank[1].dr_val;
            break;
        case REG_EXT_PORTC:
            r = s->bank[2].dr_val;
            break;
        case REG_EXT_PORTD:
            r = s->bank[3].dr_val;
            break;
        case REG_ID:
            r = 0x0;
            break;
        default:
            handled = false;
        }
    }

    if (!handled)
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);

    trace_designware_gpio_read(offset, r);

    return r;
}

static void designware_gpio_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    struct DESIGNWAREGPIOState *s = DESIGNWARE_GPIO(opaque);
    struct DESIGNWAREGPIOBank *bank;
    hwaddr banknr, reg;
    bool handled = true;

    trace_designware_gpio_write(offset, value);

    if (offset < (REG_SWPORTD_DDR + 4)) {
        banknr = offset / REG_SWPORT_DR_STRIDE;
        reg = offset % REG_SWPORT_DR_STRIDE;
        bank = &s->bank[banknr];

        switch (reg) {
        case REG_SWPORTA_DR:
            bank->dr = value;
            break;
        case REG_SWPORTA_DDR:
            bank->ddr = value;
            break;
        default:
            handled = false;
        }
    } else {
        switch (offset) {
        case REG_INTEN:
            s->int_en = value;
            break;
        case REG_INTMASK:
            s->int_mask = value;
            break;
        case REG_INTTYPE_LEVEL:
            s->int_level = value;
            break;
        case REG_INT_POLARITY:
            s->int_polarity = value;
            break;
        case REG_INTSTATUS:
            /* read only */
        case REG_INTSTATUS_RAW:
            /* read only */
            break;
        case REG_PORTA_DEBOUNCE:
            s->porta_debounce = value;
            break;
        case REG_PORTA_EOI:
            /* assume level irqs will just re-trigger */
            s->int_status_raw &= ~value;
            break;
        case REG_EXT_PORTA:
        case REG_EXT_PORTB:
        case REG_EXT_PORTC:
        case REG_EXT_PORTD:
            /* read only, ignore */
            break;
        }
    }

    if (!handled)
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);

    update_state(s);
}


static const MemoryRegionOps gpio_ops = {
    .read =  designware_gpio_read,
    .write = designware_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void designware_gpio_set(void *opaque, int line, int value)
{
    DESIGNWAREGPIOState *s = DESIGNWARE_GPIO(opaque);
    struct DESIGNWAREGPIOBank *bank = &s->bank[line / DESIGNWARE_GPIO_NR_PER_BANK];

    trace_designware_gpio_set(line, value);
    assert(line >= 0 && line < DESIGNWARE_GPIO_PINS);

    bank->in_mask = deposit32(bank->in_mask, line, 1, value >= 0);
    if (value >= 0) {
        bank->in = deposit32(bank->in, line, 1, value != 0);
    }

    update_state(s);
}

static void designware_gpio_reset(DeviceState *dev)
{
    DESIGNWAREGPIOState *s = DESIGNWARE_GPIO(dev);

    memset(s->bank, 0, sizeof(s->bank));
    s->int_en = 0;
    s->int_mask = 0;
    s->int_level = 0;
    s->int_polarity = 0;
    s->int_status = 0;
    s->porta_debounce = 0;
}

#define STATE_BANK(__nr) \
    VMSTATE_UINT32(bank[__nr].dr,      DESIGNWAREGPIOState), \
    VMSTATE_UINT32(bank[__nr].dr_val,  DESIGNWAREGPIOState), \
    VMSTATE_UINT32(bank[__nr].ddr,     DESIGNWAREGPIOState), \
    VMSTATE_UINT32(bank[__nr].in,      DESIGNWAREGPIOState), \
    VMSTATE_UINT32(bank[__nr].in_mask,  DESIGNWAREGPIOState)

static const VMStateDescription vmstate_designware_gpio = {
    .name = TYPE_DESIGNWARE_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        STATE_BANK(0),
        STATE_BANK(1),
        STATE_BANK(2),
        STATE_BANK(3),
        VMSTATE_UINT32(int_en,       DESIGNWAREGPIOState),
        VMSTATE_UINT32(int_mask,     DESIGNWAREGPIOState),
        VMSTATE_UINT32(int_level,    DESIGNWAREGPIOState),
        VMSTATE_UINT32(int_polarity, DESIGNWAREGPIOState),
        VMSTATE_UINT32(int_status,   DESIGNWAREGPIOState),
        VMSTATE_UINT32(int_status_raw, DESIGNWAREGPIOState),
        VMSTATE_UINT32(porta_debounce, DESIGNWAREGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property designware_gpio_properties[] = {
    DEFINE_PROP_UINT32("ngpio", DESIGNWAREGPIOState, ngpio, DESIGNWARE_GPIO_PINS),
    DEFINE_PROP_END_OF_LIST(),
};

static void designware_gpio_realize(DeviceState *dev, Error **errp)
{
    DESIGNWAREGPIOState *s = DESIGNWARE_GPIO(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &gpio_ops, s,
            TYPE_DESIGNWARE_GPIO, DESIGNWARE_GPIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    qdev_init_gpio_in(DEVICE(s), designware_gpio_set, s->ngpio);
    qdev_init_gpio_out(DEVICE(s), s->output, s->ngpio);
}

static void designware_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, designware_gpio_properties);
    dc->vmsd = &vmstate_designware_gpio;
    dc->realize = designware_gpio_realize;
    dc->reset = designware_gpio_reset;
    dc->desc = "SiFive GPIO";
}

static const TypeInfo designware_gpio_info = {
    .name = TYPE_DESIGNWARE_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DESIGNWAREGPIOState),
    .class_init = designware_gpio_class_init
};

static void designware_gpio_register_types(void)
{
    type_register_static(&designware_gpio_info);
}

type_init(designware_gpio_register_types)
