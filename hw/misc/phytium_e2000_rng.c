/*
 * Phytium E2000 random number generator
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/phytium_e2000_rng.h"

#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TRNG_CR             0x00
#define TRNG_CR_RNGEN       BIT(0)
#define TRNG_CR_ROSEN_MASK  MAKE_64BIT_MASK(4, 4)
#define TRNG_CR_DIEN        BIT(16)
#define TRNG_CR_ERIEN       BIT(17)
#define TRNG_CR_IRQEN       BIT(24)
#define TRNG_CR_MASK        (TRNG_CR_RNGEN | TRNG_CR_ROSEN_MASK | \
                             TRNG_CR_DIEN | TRNG_CR_ERIEN | TRNG_CR_IRQEN)

#define TRNG_MSEL           0x04
#define TRNG_MSEL_PRNG      BIT(0)

#define TRNG_SR             0x08
#define TRNG_SR_DRDY        BIT(1)

#define TRNG_DR             0x0c

#define TRNG_RESEED         0x40

struct PhytiumE2000RNGState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t control;
    uint32_t mode;
};

static bool phytium_e2000_rng_enabled(PhytiumE2000RNGState *s)
{
    return s->control & TRNG_CR_RNGEN;
}

static uint64_t phytium_e2000_rng_read(void *opaque, hwaddr offset,
                                       unsigned int size)
{
    PhytiumE2000RNGState *s = opaque;
    uint32_t value;

    switch (offset) {
    case TRNG_CR:
        return s->control;
    case TRNG_MSEL:
        return s->mode;
    case TRNG_SR:
        /*
         * Generation takes zero virtual time, so DRDY is reasserted as soon
         * as the guest acknowledges the previous batch.
         */
        return phytium_e2000_rng_enabled(s) ? TRNG_SR_DRDY : 0;
    case TRNG_DR:
        if (!phytium_e2000_rng_enabled(s)) {
            return 0;
        }
        qemu_guest_getrandom_nofail(&value, sizeof(value));
        return value;
    case TRNG_RESEED:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        return 0;
    }
}

static void phytium_e2000_rng_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned int size)
{
    PhytiumE2000RNGState *s = opaque;

    switch (offset) {
    case TRNG_CR:
        s->control = value & TRNG_CR_MASK;
        break;
    case TRNG_MSEL:
        s->mode = value & TRNG_MSEL_PRNG;
        break;
    case TRNG_SR:
        break;
    case TRNG_DR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only data register\n",
                      DEVICE(s)->canonical_path);
        break;
    case TRNG_RESEED:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    }
}

static const MemoryRegionOps phytium_e2000_rng_ops = {
    .read = phytium_e2000_rng_read,
    .write = phytium_e2000_rng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void phytium_e2000_rng_reset(Object *obj, ResetType type)
{
    PhytiumE2000RNGState *s = PHYTIUM_E2000_RNG(obj);

    s->control = 0;
    s->mode = 0;
}

static const VMStateDescription phytium_e2000_rng_vmsd = {
    .name = TYPE_PHYTIUM_E2000_RNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, PhytiumE2000RNGState),
        VMSTATE_UINT32(mode, PhytiumE2000RNGState),
        VMSTATE_END_OF_LIST()
    },
};

static void phytium_e2000_rng_init(Object *obj)
{
    PhytiumE2000RNGState *s = PHYTIUM_E2000_RNG(obj);

    memory_region_init_io(&s->iomem, obj, &phytium_e2000_rng_ops, s,
                          TYPE_PHYTIUM_E2000_RNG,
                          PHYTIUM_E2000_RNG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void phytium_e2000_rng_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Phytium E2000 random number generator";
    dc->vmsd = &phytium_e2000_rng_vmsd;
    rc->phases.enter = phytium_e2000_rng_reset;
}

static const TypeInfo phytium_e2000_rng_info = {
    .name = TYPE_PHYTIUM_E2000_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000RNGState),
    .instance_init = phytium_e2000_rng_init,
    .class_init = phytium_e2000_rng_class_init,
};

static void phytium_e2000_rng_register_types(void)
{
    type_register_static(&phytium_e2000_rng_info);
}

type_init(phytium_e2000_rng_register_types)
