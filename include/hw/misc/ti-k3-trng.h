/*
 * TI K3 SA2UL TRNG (EIP-76) stub (AM64x)
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_TI_K3_TRNG_H
#define HW_MISC_TI_K3_TRNG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_K3_TRNG "ti-k3-trng"
OBJECT_DECLARE_SIMPLE_TYPE(TIK3TrngState, TI_K3_TRNG)

/* EIP-76 register window: offsets 0x00..0x7c. */
#define TI_K3_TRNG_REGS_SIZE 0x80

struct TIK3TrngState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* Nonzero xorshift64 generator state. */
    uint64_t rng_state;
    /* Cached pairs for OUTPUT_0/1 and OUTPUT_2/3. */
    uint64_t pair_a;
    uint64_t pair_b;

    /* RAM-backed registers except OUTPUT_x and STATUS. */
    uint32_t regs[TI_K3_TRNG_REGS_SIZE / 4];
};

#endif
