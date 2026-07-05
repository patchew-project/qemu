/*
 * RP2040 power-on state machine emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_PSM_H
#define HW_MISC_RP2040_PSM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_PSM "rp2040-psm"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040PsmState, RP2040_PSM)

#define RP2040_PSM_BASE       0x40010000
#define RP2040_PSM_SIZE       0x4000
#define RP2040_PSM_VALID_MASK 0x0001ffff
#define RP2040_PSM_PROC1      (1u << 16)

typedef void (*RP2040PsmUpdateFn)(void *opaque);

struct RP2040PsmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t frce_on;
    uint32_t frce_off;
    uint32_t wdsel;

    RP2040PsmUpdateFn update;
    void *update_opaque;
};

void rp2040_psm_set_update_callback(RP2040PsmState *s,
                                    RP2040PsmUpdateFn update,
                                    void *opaque);
uint32_t rp2040_psm_get_frce_off(RP2040PsmState *s);

#endif
