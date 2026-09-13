/*
 * Renesas RX I/O Ports (GPIO) and Multi-Function Pin Controller (MPC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), sections 20 (I/O Ports) and 22 (MPC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Each 8-bit I/O port exposes a direction register (PDR), an output data
 * register (PODR) and an input data register (PIDR). Output pins drive the
 * corresponding "gpio-out" line (used by board LEDs); input pins read back the
 * level driven on the "gpio-in" line (used by board switches). The MPC PFS
 * registers select alternate pin functions and behave as plain storage.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/gpio/renesas_rx_gpio.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

/* Per-port register offsets within the I/O port block (index = port number). */
#define R_PDR   0x000   /* 1 byte per port */
#define R_PODR  0x020
#define R_PIDR  0x040
#define R_PMR   0x060
#define R_ODR   0x080   /* 2 bytes per port */
#define R_PCR   0x0C0
#define R_DSCR  0x0E0
#define R_END   0x100

/* MPC write-protect register, relative to the MPC block base (0x0008C100). */
#define R_PWPR  0x01F   /* 0x0008C11F */

/* Compute the input-data value seen for a port. */
static uint8_t port_pidr(RenesasRxGpioState *s, unsigned p)
{
    uint8_t value;

    /* Output pins read back their output latch; input pins read the pin. */
    value = (s->podr[p] & s->pdr[p]) | (s->input[p] & ~s->pdr[p]);
    return value;
}

/* Drive the per-pin output lines for a port from PDR/PODR. */
static void port_update_out(RenesasRxGpioState *s, unsigned p)
{
    for (unsigned b = 0; b < 8; b++) {
        int level = 0;
        if (s->pdr[p] & (1 << b)) {                 /* output pin */
            level = (s->podr[p] >> b) & 1;
        }
        qemu_set_irq(s->out[p * 8 + b], level);
    }
}

static uint64_t gpio_port_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxGpioState *s = opaque;
    unsigned p = offset & 0x1f;

    switch (offset & ~0x1f) {
    case R_PDR:
        return s->pdr[p];
    case R_PODR:
        return s->podr[p];
    case R_PIDR:
        return port_pidr(s, p);
    case R_PMR:
        return s->pmr[p];
    case R_PCR:
        return s->pcr[p];
    case R_DSCR:
        return s->dscr[p];
    default:
        if (offset >= R_ODR && offset < R_PCR) {
            return s->odr[offset - R_ODR];
        }
        qemu_log_mask(LOG_UNIMP, "renesas-rx-gpio: read unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void gpio_port_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    RenesasRxGpioState *s = opaque;
    unsigned p = offset & 0x1f;

    switch (offset & ~0x1f) {
    case R_PDR:
        s->pdr[p] = value;
        port_update_out(s, p);
        break;
    case R_PODR:
        s->podr[p] = value;
        port_update_out(s, p);
        break;
    case R_PMR:
        s->pmr[p] = value;
        break;
    case R_PCR:
        s->pcr[p] = value;
        break;
    case R_DSCR:
        s->dscr[p] = value;
        break;
    case R_PIDR:
        /* Input data register is read-only. */
        break;
    default:
        if (offset >= R_ODR && offset < R_PCR) {
            s->odr[offset - R_ODR] = value;
        } else {
            qemu_log_mask(LOG_UNIMP, "renesas-rx-gpio: write unimplemented 0x%"
                          HWADDR_PRIx "\n", offset);
        }
        break;
    }
}

static const MemoryRegionOps gpio_port_ops = {
    .read = gpio_port_read,
    .write = gpio_port_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
    .valid = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t gpio_mpc_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxGpioState *s = opaque;
    uint64_t val = 0;

    for (unsigned i = 0; i < size && offset + i < RX_GPIO_MPC_SIZE; i++) {
        val |= (uint64_t)s->mpc[offset + i] << (8 * i);
    }
    return val;
}

static void gpio_mpc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    RenesasRxGpioState *s = opaque;

    /*
     * PFS registers are locked unless PWPR.PFSWE is set (and PWPR.B0WI clear).
     * Storing the PFS selection is otherwise sufficient: QEMU peripherals are
     * wired unconditionally, so pin muxing has no functional effect here.
     */
    for (unsigned i = 0; i < size && offset + i < RX_GPIO_MPC_SIZE; i++) {
        s->mpc[offset + i] = (value >> (8 * i)) & 0xff;
    }
}

static const MemoryRegionOps gpio_mpc_ops = {
    .read = gpio_mpc_read,
    .write = gpio_mpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 1 },
    .valid = { .min_access_size = 1, .max_access_size = 1 },
};

/* External input line handler: pin = port * 8 + bit. */
static void gpio_set_input(void *opaque, int pin, int level)
{
    RenesasRxGpioState *s = opaque;
    unsigned p = pin / 8, b = pin % 8;

    if (level) {
        s->input[p] |= (1 << b);
    } else {
        s->input[p] &= ~(1 << b);
    }
}

static void rx_gpio_reset(DeviceState *dev)
{
    RenesasRxGpioState *s = RENESAS_RX_GPIO(dev);

    memset(s->pdr, 0, sizeof(s->pdr));
    memset(s->podr, 0, sizeof(s->podr));
    memset(s->pmr, 0, sizeof(s->pmr));
    memset(s->odr, 0, sizeof(s->odr));
    memset(s->pcr, 0, sizeof(s->pcr));
    memset(s->dscr, 0, sizeof(s->dscr));
    memset(s->mpc, 0, sizeof(s->mpc));
    /* PWPR resets with B0WI set (PFS write disabled). */
    s->mpc[R_PWPR] = 0x80;

    for (unsigned p = 0; p < RX_GPIO_NR_PORTS; p++) {
        port_update_out(s, p);
    }
}

static void rx_gpio_init(Object *obj)
{
    RenesasRxGpioState *s = RENESAS_RX_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->port_mr, obj, &gpio_port_ops, s,
                          "renesas-rx-gpio.port", RX_GPIO_PORT_SIZE);
    memory_region_init_io(&s->mpc_mr, obj, &gpio_mpc_ops, s,
                          "renesas-rx-gpio.mpc", RX_GPIO_MPC_SIZE);
    sysbus_init_mmio(sbd, &s->port_mr);
    sysbus_init_mmio(sbd, &s->mpc_mr);

    qdev_init_gpio_out_named(DEVICE(obj), s->out, "gpio-out", RX_GPIO_NR_PINS);
    qdev_init_gpio_in_named(DEVICE(obj), gpio_set_input, "gpio-in",
                            RX_GPIO_NR_PINS);
}

static const VMStateDescription vmstate_rx_gpio = {
    .name = "renesas-rx-gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(pdr, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(podr, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(pmr, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(odr, RenesasRxGpioState, RX_GPIO_NR_PORTS * 2),
        VMSTATE_UINT8_ARRAY(pcr, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(dscr, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(input, RenesasRxGpioState, RX_GPIO_NR_PORTS),
        VMSTATE_UINT8_ARRAY(mpc, RenesasRxGpioState, RX_GPIO_MPC_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rx_gpio;
    device_class_set_legacy_reset(dc, rx_gpio_reset);
}

static const TypeInfo rx_gpio_info = {
    .name = TYPE_RENESAS_RX_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxGpioState),
    .instance_init = rx_gpio_init,
    .class_init = rx_gpio_class_init,
};

static void rx_gpio_register_types(void)
{
    type_register_static(&rx_gpio_info);
}

type_init(rx_gpio_register_types)
