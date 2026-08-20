/*
 * TI K3 SA2UL TRNG (EIP-76) stub (AM64x)
 *
 * Minimal EIP-76 register subset. STATUS reports RNG_READY, INTACK writes
 * are accepted, OUTPUT_0..3 return two cached 64-bit pairs, and configuration
 * registers are RAM-backed.
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/misc/ti-k3-trng.h"
#include "trace.h"

#define RNG_OUTPUT_0    0x00
#define RNG_OUTPUT_1    0x04
#define RNG_OUTPUT_2    0x08
#define RNG_OUTPUT_3    0x0C
#define RNG_STATUS      0x10
#define RNG_READY       (1u << 0)
#define RNG_CONTROL     0x14

/* Nonzero xorshift64 state reaches never zero. */
static uint64_t ti_k3_trng_next(TIK3TrngState *s)
{
    s->rng_state ^= s->rng_state << 13;
    s->rng_state ^= s->rng_state >> 7;
    s->rng_state ^= s->rng_state << 17;
    return s->rng_state;
}

static uint64_t ti_k3_trng_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3TrngState *s = TI_K3_TRNG(opaque);
    uint32_t val;

    switch (addr) {
    case RNG_OUTPUT_0:
        s->pair_a = ti_k3_trng_next(s);
        val = (uint32_t)s->pair_a;
        break;
    case RNG_OUTPUT_1:
        val = (uint32_t)(s->pair_a >> 32);
        break;
    case RNG_OUTPUT_2:
        s->pair_b = ti_k3_trng_next(s);
        val = (uint32_t)s->pair_b;
        break;
    case RNG_OUTPUT_3:
        val = (uint32_t)(s->pair_b >> 32);
        break;
    case RNG_STATUS:
        /* Always ready, never in shutdown. */
        val = RNG_READY;
        break;
    default:
        val = s->regs[addr >> 2];
        break;
    }

    trace_ti_k3_trng_read(addr, val);
    return val;
}

static void ti_k3_trng_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    TIK3TrngState *s = TI_K3_TRNG(opaque);

    trace_ti_k3_trng_write(addr, value);

    switch (addr) {
    case RNG_OUTPUT_0:
    case RNG_OUTPUT_1:
    case RNG_OUTPUT_2:
    case RNG_OUTPUT_3:
        /* Output registers are read-only; use RAZ/WI. */
        break;
    case RNG_STATUS:
        /* STATUS is synthesized on each read, so INTACK has no state. */
        break;
    default:
        s->regs[addr >> 2] = (uint32_t)value;
        break;
    }
}

static const MemoryRegionOps ti_k3_trng_ops = {
    .read = ti_k3_trng_read,
    .write = ti_k3_trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ti_k3_trng_reset(DeviceState *dev)
{
    TIK3TrngState *s = TI_K3_TRNG(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->rng_state = 0x9e3779b97f4a7c15ULL;
    s->pair_a = 0;
    s->pair_b = 0;
}

static void ti_k3_trng_init(Object *obj)
{
    TIK3TrngState *s = TI_K3_TRNG(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_trng_ops, s,
                          TYPE_TI_K3_TRNG, TI_K3_TRNG_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ti_k3_trng_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ti_k3_trng_reset);
}

static const TypeInfo ti_k3_trng_info = {
    .name = TYPE_TI_K3_TRNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3TrngState),
    .instance_init = ti_k3_trng_init,
    .class_init = ti_k3_trng_class_init,
};

static void ti_k3_trng_register_types(void)
{
    type_register_static(&ti_k3_trng_info);
}

type_init(ti_k3_trng_register_types)
