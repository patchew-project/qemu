/*
 * Data Scratch Pad RAM
 *
 * Copyright (c) 2017 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIPS_DSPRAM_H
#define MIPS_DSPRAM_H

#include "hw/sysbus.h"

#define TYPE_MIPS_DSPRAM "mips-dspram"
#define MIPS_DSPRAM(obj) OBJECT_CHECK(MIPSDSPRAMState, (obj), TYPE_MIPS_DSPRAM)

typedef struct MIPSDSPRAMState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /* 2 ^ SIZE */
    uint64_t size;

    MemoryRegion mr;

    /* SAAR */
    bool saar_present;
    void *saar;

    /* ramblock */
    uint8_t *ramblock;
} MIPSDSPRAMState;

#endif /* MIPS_DSPRAM_H */
