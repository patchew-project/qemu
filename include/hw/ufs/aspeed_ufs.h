/*
 * ASPEED AST2700 UFS Host Controller
 *
 * Copyright 2026 IBM Corp.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_UFS_H
#define ASPEED_UFS_H

#include "hw/core/sysbus.h"
#include "block/ufs.h"
#include "system/block-backend.h"

#define TYPE_ASPEED_UFS "aspeed-ufs"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedUFSState, ASPEED_UFS)

/* UFSHCI register space is 256 bytes (0x00-0xFF) */
#define ASPEED_UFS_MMIO_SIZE  0x100
#define ASPEED_UFS_NUM_REGS   (ASPEED_UFS_MMIO_SIZE / sizeof(uint32_t))

/* Number of UTP Transfer Request slots advertised */
#define ASPEED_UFS_NUTRS      32
/* Number of UTP Task Management Request slots */
#define ASPEED_UFS_NUTMRS     8

/*
 * Reset / power-on values.
 *
 * CAP: NUTRS=31 (0x1f, 5-bit), RTT=2, NUTMRS=7, 64AS=1
 * VER: UFSHCI 2.0 (matches aspeed,ufshc-m31-16nm)
 * HCS: DP|UTRLRDY|UTMRLRDY|UCRDY (bits 0-3)
 */
#define ASPEED_UFS_CAP_RESET  0x0702031f
#define ASPEED_UFS_VER_RESET  0x00000200
#define ASPEED_UFS_HCS_READY  0x0000000f

struct AspeedUFSState {
    SysBusDevice  parent_obj;

    MemoryRegion  iomem;
    qemu_irq      irq;

    BlockBackend *blk;
    uint64_t      num_sectors;

    uint32_t      regs[ASPEED_UFS_NUM_REGS];

    /*
     * HCE state machine phase:
     *   0 = idle (HCE=0)
     *   1 = cleared (firmware wrote 1, we stored 0, BH pending)
     *   2 = ready (BH fired, HCE=1, HCS ready bits set)
     */
    int           hce_phase;
    QEMUBH       *hce_bh;

    uint32_t      utrldbr;       /* pending doorbell bits */
    uint64_t      utrl_base;     /* 64-bit physical base of UTRL */
    uint64_t      utmrl_base;    /* 64-bit physical base of UTMRL */

    /* Unit Descriptor; capacity filled at realize time */
    uint8_t       unit_desc[0x23];
};

#endif /* ASPEED_UFS_H */
