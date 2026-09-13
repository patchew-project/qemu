/*
 * Renesas RX ROM cache control registers
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.2.40 R01UH0590EJ0240)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The ROM cache speeds up code flash reads and is architecturally invisible
 * apart from these two registers. QEMU has no such cache, so there is nothing
 * to model beyond them -- but they do have to exist, because start-up code
 * routinely enables the cache and then spins until the enable reads back:
 *
 *     FLASH.ROMCE.WORD = 0x0001;
 *     while (FLASH.ROMCE.WORD != 0x0001) {
 *     }
 *
 * With no device here that loop never terminates, and the machine hangs
 * before anything else in the firmware runs.
 *
 * ROMCE holds the enable bit and reads back what was written. ROMCIV starts
 * an invalidation, which completes instantly here, so the bit reads back as
 * zero the way it does on hardware once invalidation has finished.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/renesas_rx_romcache.h"
#include "migration/vmstate.h"

#define R_ROMCE     0x00    /* 16-bit ROM cache enable     */
#define R_ROMCIV    0x04    /* 16-bit ROM cache invalidate */
#define R_ROMWT     0x1c    /* 8-bit ROM wait-cycle setting */

#define ROMCE_ROMCEN    0x0001

static uint64_t romcache_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxRomCacheState *s = opaque;

    switch (offset) {
    case R_ROMCE:
        return s->romce;
    case R_ROMCIV:
        /* Invalidation is instantaneous, so it always reads as complete. */
        return 0;
    case R_ROMWT:
        return s->romwt;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas-rx-romcache: read from unmapped offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void romcache_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    RenesasRxRomCacheState *s = opaque;

    switch (offset) {
    case R_ROMCE:
        s->romce = value & ROMCE_ROMCEN;
        break;
    case R_ROMCIV:
        /* Nothing to invalidate; the write is accepted and completes. */
        break;
    case R_ROMWT:
        s->romwt = value & 0x3;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas-rx-romcache: write to unmapped offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps romcache_ops = {
    .read = romcache_read,
    .write = romcache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void romcache_reset(DeviceState *dev)
{
    RenesasRxRomCacheState *s = RENESAS_RX_ROMCACHE(dev);

    s->romce = 0;
    s->romwt = 0;
}

static void romcache_init(Object *obj)
{
    RenesasRxRomCacheState *s = RENESAS_RX_ROMCACHE(obj);

    memory_region_init_io(&s->memory, obj, &romcache_ops, s,
                          "renesas-rx-romcache", RX_ROMCACHE_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->memory);
}

static const VMStateDescription vmstate_romcache = {
    .name = "renesas-rx-romcache",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(romce, RenesasRxRomCacheState),
        VMSTATE_UINT8(romwt, RenesasRxRomCacheState),
        VMSTATE_END_OF_LIST()
    }
};

static void romcache_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_romcache;
    device_class_set_legacy_reset(dc, romcache_reset);
}

static const TypeInfo romcache_info = {
    .name          = TYPE_RENESAS_RX_ROMCACHE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxRomCacheState),
    .instance_init = romcache_init,
    .class_init    = romcache_class_init,
};

static void romcache_register_types(void)
{
    type_register_static(&romcache_info);
}

type_init(romcache_register_types)
