/*
 * Renesas RX ROM cache control registers
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RENESAS_RX_ROMCACHE_H
#define HW_MISC_RENESAS_RX_ROMCACHE_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_ROMCACHE "renesas-rx-romcache"
OBJECT_DECLARE_SIMPLE_TYPE(RenesasRxRomCacheState, RENESAS_RX_ROMCACHE)

/* Also covers the adjacent system ROMWT register at +0x01c. */
#define RX_ROMCACHE_SIZE    0x20

struct RenesasRxRomCacheState {
    SysBusDevice parent_obj;

    MemoryRegion memory;

    uint16_t romce;     /* ROM cache enable */
    uint8_t romwt;      /* ROM wait cycles */
};

#endif /* HW_MISC_RENESAS_RX_ROMCACHE_H */
