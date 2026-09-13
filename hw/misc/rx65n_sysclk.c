/*
 * RX65N Clock Generation Circuit (SYSTEM)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 9
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * The RX65N clock generation circuit configures the oscillators, PLL and
 * clock dividers. QEMU does not model real clock timing, so most registers
 * behave as plain read/write storage. The crucial exception is the
 * Oscillation Stabilization Flag Register (OSCOVFSR): startup code busy-waits
 * on it until every selected clock source reports "stable", so it is forced
 * to report all sources stabilized.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/rx65n_sysclk.h"
#include "migration/vmstate.h"

/* Register offsets from the SYSTEM base (0x00080000). */
#define R_PLLCR2    0x2A    /* PLL control register 2                (8-bit)  */
#define R_MOSCCR    0x32    /* Main clock oscillator control reg     (8-bit)  */
#define R_SOSCCR    0x33    /* Sub-clock oscillator control reg      (8-bit)  */
#define R_HOCOCR    0x36    /* High-speed on-chip osc control reg    (8-bit)  */
#define R_OSCOVFSR  0x3C    /* Oscillation stabilization flag reg    (8-bit)  */

/* OSCOVFSR flag bits. */
#define OSCOVFSR_MOOVF  (1 << 0)    /* Main clock oscillator           */
#define OSCOVFSR_SOOVF  (1 << 1)    /* Sub-clock oscillator            */
#define OSCOVFSR_PLOVF  (1 << 2)    /* PLL                             */
#define OSCOVFSR_HCOVF  (1 << 3)    /* High-speed on-chip oscillator   */
#define OSCOVFSR_ILCOVF (1 << 4)    /* IWDT low-speed on-chip osc      */
#define OSCOVFSR_PPLOVF (1 << 5)    /* PLL2 (RX72M, high-speed periph) */

/*
 * Compute the Oscillation Stabilization Flag Register from the oscillator
 * control registers. Each flag reads 1 once its oscillator is enabled (its
 * stop bit is clear) and 0 while it is stopped. QEMU has no real oscillator
 * timing, so stabilization is reported as immediate. PLLCR2.PLLEN uses the
 * same "0 = operating" polarity as the stop bits.
 */
static uint8_t oscovfsr_value(RX65NSysClkState *s)
{
    uint8_t v = 0;

    if (!(s->regs[R_MOSCCR] & 1)) {
        v |= OSCOVFSR_MOOVF;
    }
    if (!(s->regs[R_SOSCCR] & 1)) {
        v |= OSCOVFSR_SOOVF;
    }
    if (!(s->regs[R_PLLCR2] & 1)) {
        v |= OSCOVFSR_PLOVF;
    }
    if (!(s->regs[R_HOCOCR] & 1)) {
        v |= OSCOVFSR_HCOVF;
    }
    /*
     * The IWDT low-speed on-chip oscillator and (on the RX72M) PLL2 have no
     * stop bit modelled here; report them stabilized so firmware that selects
     * them does not busy-wait forever. QEMU has no real oscillator timing.
     */
    v |= OSCOVFSR_ILCOVF | OSCOVFSR_PPLOVF;
    return v;
}

static uint64_t rx65n_sysclk_read(void *opaque, hwaddr offset, unsigned size)
{
    RX65NSysClkState *s = opaque;
    uint64_t val = 0;
    unsigned i;

    if (offset + size > RX65N_SYSCLK_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rx65n_sysclk: read out of range 0x%" HWADDR_PRIX "\n",
                      offset);
        return 0;
    }

    for (i = 0; i < size; i++) {
        val |= (uint64_t)s->regs[offset + i] << (8 * i);
    }

    /* OSCOVFSR is read-only and derived from the oscillator controls. */
    if (offset <= R_OSCOVFSR && offset + size > R_OSCOVFSR) {
        unsigned b = R_OSCOVFSR - offset;
        val &= ~((uint64_t)0xff << (8 * b));
        val |= (uint64_t)oscovfsr_value(s) << (8 * b);
    }

    return val;
}

static void rx65n_sysclk_write(void *opaque, hwaddr offset, uint64_t val,
                               unsigned size)
{
    RX65NSysClkState *s = opaque;
    unsigned i;

    if (offset + size > RX65N_SYSCLK_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rx65n_sysclk: write out of range 0x%" HWADDR_PRIX "\n",
                      offset);
        return;
    }

    for (i = 0; i < size; i++) {
        s->regs[offset + i] = (val >> (8 * i)) & 0xff;
    }
}

static const MemoryRegionOps rx65n_sysclk_ops = {
    .read = rx65n_sysclk_read,
    .write = rx65n_sysclk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void rx65n_sysclk_reset(DeviceState *dev)
{
    RX65NSysClkState *s = RX65N_SYSCLK(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * Reset values for the oscillator controls (HW manual section 9). The
     * main and sub-clock oscillators and the PLL are stopped out of reset;
     * the high-speed on-chip oscillator (HOCO) is left running so its
     * stabilization flag reads ready for firmware that selects it directly.
     */
    s->regs[R_MOSCCR] = 0x01;   /* MOSTP = 1: main oscillator stopped */
    s->regs[R_SOSCCR] = 0x01;   /* SOSTP = 1: sub-clock stopped       */
    s->regs[R_PLLCR2] = 0x01;   /* PLLEN = 1: PLL stopped             */
    s->regs[R_HOCOCR] = 0x00;   /* HCSTP = 0: HOCO running            */
}

static void rx65n_sysclk_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RX65NSysClkState *s = RX65N_SYSCLK(obj);

    memory_region_init_io(&s->memory, obj, &rx65n_sysclk_ops, s,
                          "rx65n-sysclk", RX65N_SYSCLK_SIZE);
    sysbus_init_mmio(d, &s->memory);
}

static const VMStateDescription vmstate_rx65n_sysclk = {
    .name = "rx65n-sysclk",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, RX65NSysClkState, RX65N_SYSCLK_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void rx65n_sysclk_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rx65n_sysclk;
    device_class_set_legacy_reset(dc, rx65n_sysclk_reset);
}

static const TypeInfo rx65n_sysclk_info = {
    .name = TYPE_RX65N_SYSCLK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RX65NSysClkState),
    .instance_init = rx65n_sysclk_init,
    .class_init = rx65n_sysclk_class_init,
};

static void rx65n_sysclk_register_types(void)
{
    type_register_static(&rx65n_sysclk_info);
}

type_init(rx65n_sysclk_register_types)
