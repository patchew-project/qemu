/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_SBC_H
#define ASPEED_SBC_H

#include "hw/sysbus.h"
#include "hw/misc/aspeed_otpmem.h"

#define TYPE_ASPEED_SBC "aspeed.sbc"
#define TYPE_ASPEED_AST2600_SBC TYPE_ASPEED_SBC "-ast2600"
OBJECT_DECLARE_TYPE(AspeedSBCState, AspeedSBCClass, ASPEED_SBC)

#define ASPEED_SBC_NR_REGS (0x93c >> 2)

#define QSR_AES                     BIT(27)
#define QSR_RSA1024                 (0x0 << 12)
#define QSR_RSA2048                 (0x1 << 12)
#define QSR_RSA3072                 (0x2 << 12)
#define QSR_RSA4096                 (0x3 << 12)
#define QSR_SHA224                  (0x0 << 10)
#define QSR_SHA256                  (0x1 << 10)
#define QSR_SHA384                  (0x2 << 10)
#define QSR_SHA512                  (0x3 << 10)

#define READ_CMD                    (0x23b1e361)
#define WRITE_CMD                   (0x23b1e362)
#define PROG_CMD                    (0x23b1e364)

#define OTP_DATA_DWORD_COUNT        (0x800)
#define OTP_TOTAL_DWORD_COUNT       (0x1000)
#define OTP_FILE_SIZE               (OTP_TOTAL_DWORD_COUNT * sizeof(uint32_t))

#define MODE_REGISTER               (0x1000)
#define MODE_REGISTER_A             (0x3000)
#define MODE_REGISTER_B             (0x5000)

struct AspeedSBCState {
    SysBusDevice parent;

    bool emmc_abr;
    uint32_t signing_settings;

    MemoryRegion iomem;

    uint32_t regs[ASPEED_SBC_NR_REGS];

    AspeedOTPMemState *otpmem;
};

struct AspeedSBCClass {
    SysBusDeviceClass parent_class;
};

#endif /* ASPEED_SBC_H */
