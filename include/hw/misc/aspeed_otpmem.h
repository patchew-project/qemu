/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_OTPMMEM_H
#define ASPEED_OTPMMEM_H

#include "hw/sysbus.h"
#include "qapi/error.h"

#define TYPE_ASPEED_OTPMEM "aspeed.otpmem"
#define ASPEED_OTPMEM_DRIVE "otpmem"

#define ASPEED_OTPMEM(obj) OBJECT_CHECK(AspeedOTPMemState, (obj), \
                                        TYPE_ASPEED_OTPMEM)

typedef struct AspeedOTPMemOps {
    void (*read)(void *s, uint32_t addr, uint32_t *out, Error **errp);
    void (*prog)(void *s, uint32_t addr, uint32_t data, Error **errp);
    void (*set_default_value)(void *s, uint32_t otp_offset,
                              uint32_t data, Error **errp);
} AspeedOTPMemOps;

typedef struct AspeedOTPMemState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    BlockBackend *blk;
    int64_t max_size;

    AspeedOTPMemOps *ops;
} AspeedOTPMemState;

#endif /* ASPEED_OTPMMEM_H */

