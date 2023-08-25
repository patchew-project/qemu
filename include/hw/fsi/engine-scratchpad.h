/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM scratchpad engne
 */
#ifndef FSI_ENGINE_SCRATCHPAD_H
#define FSI_ENGINE_SCRATCHPAD_H

#include "hw/fsi/lbus.h"
#include "hw/fsi/bits.h"

#define ENGINE_CONFIG_NEXT              BE_BIT(0)
#define ENGINE_CONFIG_VPD               BE_BIT(1)
#define ENGINE_CONFIG_SLOTS             BE_GENMASK(8, 15)
#define ENGINE_CONFIG_VERSION           BE_GENMASK(16, 19)
#define ENGINE_CONFIG_TYPE              BE_GENMASK(20, 27)
#define   ENGINE_CONFIG_TYPE_PEEK       (0x02 << 4)
#define   ENGINE_CONFIG_TYPE_FSI        (0x03 << 4)
#define   ENGINE_CONFIG_TYPE_SCRATCHPAD (0x06 << 4)
#define ENGINE_CONFIG_CRC              BE_GENMASK(28, 31)

#define TYPE_SCRATCHPAD "scratchpad"
#define SCRATCHPAD(obj) OBJECT_CHECK(ScratchPad, (obj), TYPE_SCRATCHPAD)

typedef struct ScratchPad {
        LBusDevice parent;

        uint32_t reg;
} ScratchPad;

#endif /* FSI_ENGINE_SCRATCHPAD_H */
