/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_OTPMMEM_H
#define ASPEED_OTPMMEM_H

#include "system/memory.h"
#include "hw/block/block.h"
#include "system/memory.h"
#include "system/address-spaces.h"

#define OTPMEM_SIZE 0x4000
#define TYPE_ASPEED_OTPMEM "aspeed.otpmem"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedOTPMemState, ASPEED_OTPMEM)

typedef struct AspeedOTPMemState {
    DeviceState parent_obj;

    BlockBackend *blk;

    uint64_t size;

    AddressSpace as;

    MemoryRegion mmio;

    uint8_t *storage;
} AspeedOTPMemState;

#endif /* ASPEED_OTPMMEM_H */
