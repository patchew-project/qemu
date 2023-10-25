/*
 * Andes ATCDMAC300 (Andes Technology DMA Controller)
 *
 * Copyright (c) 2022 Andes Tech. Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef ATCDMAC300_H
#define ATCDMAC300_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/coroutine.h"
#include "block/aio.h"
#include "sysemu/iothread.h"
#include "sysemu/dma.h"

#define TYPE_ATCDMAC300 "atcdmac300"
OBJECT_DECLARE_SIMPLE_TYPE(ATCDMAC300State, ATCDMAC300)

#define ATCDMAC300_IOPMP_SID            0

#define ATCDMAC300_PRODUCT_ID           0x010230
#define ATCDMAC300_REV_MAJOR            0x0
#define ATCDMAC300_REV_MINOR            0x1

/* DMAC Configuration Register (Offset 0x10)  */
#define ATCDMAC300_DMA_CFG              0x10
#define DMA_CFG_CHAIN_XFR               31
#define DMA_CFG_REQ_SYNC                30
#define DMA_CFG_DATA_WITDTH             24
#define DMA_CFG_ADDR_WIDTH              17
#define DMA_CFG_CORE_NUM                16
#define DMA_CFG_BUS_NUM                 15
#define DMA_CFG_REQ_NUM                 10
#define DMA_CFG_FIFO_DEPTH              4
#define DMA_CFG_CHAN_NUM                0

/* Interrupt Status Register (Offset 0x20) */
#define ATCDMAC300_DMAC_CTRL            0x20

/* Channel Abort Register (Offset 0x24) */
#define ATCDMAC300_CHN_ABT              0x24

/* Interrupt Status Register (Offset 0x30) */
#define ATCDMAC300_INT_STATUS           0x30
#define INT_STATUS_TC                   16
#define INT_STATUS_ABT                  8
#define INT_STATUS_ERR                  0

/* Interrupt Status Register (Offset 0x34) */
#define ATCDMAC300_CHAN_ENABLE          0x34

/* Channel n Control Register (Offset 0x40 + n*0x20) */
#define CHAN_CTL_SRC_BUS_IDX            31
#define CHAN_CTL_DST_BUS_IDX            30
#define CHAN_CTL_PRIORITY               29
#define CHAN_CTL_SRC_BURST_SZ           24
#define CHAN_CTL_SRC_WIDTH              21
#define CHAN_CTL_DST_WIDTH              18
#define CHAN_CTL_SRC_MODE               17
#define CHAN_CTL_DST_MODE               16
#define CHAN_CTL_SRC_ADDR_CTL           14
#define CHAN_CTL_DST_ADDR_CTL           12
#define CHAN_CTL_SRC_REQ_SEL            8
#define CHAN_CTL_DST_REQ_SEL            4
#define CHAN_CTL_INT_ABT_MASK_POS       3
#define CHAN_CTL_INT_ERR_MASK_POS       2
#define CHAN_CTL_INT_TC_MASK_POS        1
#define CHAN_CTL_ENABLE                 0

#define CHAN_CTL_SRC_WIDTH_MASK         0x7
#define CHAN_CTL_DST_WIDTH_MASK         0x7
#define CHAN_CTL_SRC_BURST_SZ_MASK      0xf
#define CHAN_CTL_SRC_ADDR_CTL_MASK      0x3
#define CHAN_CTL_DST_ADDR_CTL_MASK      0x3

#define ATCDMAC300_CHAN_CTL             0x40
#define ATCDMAC300_CHAN_TRAN_SZ         0x44
#define ATCDMAC300_CHAN_SRC_ADDR        0x48
#define ATCDMAC300_CHAN_SRC_ADDR_H      0x4C
#define ATCDMAC300_CHAN_DST_ADDR        0x50
#define ATCDMAC300_CHAN_DST_ADDR_H      0x54
#define ATCDMAC300_CHAN_LL_POINTER      0x58
#define ATCDMAC300_CHAN_LL_POINTER_H    0x5C

#define ATCDMAC300_IRQ_START            0x40
#define ATCDMAC300_IRQ_END              (ATCDMAC300_IRQ_START + \
                                         ATCDMAC300_MAX_CHAN)

#define ATCDMAC300_MAX_BURST_SIZE       1024
#define ATCDMAC300_MAX_CHAN             0x8

#define PER_CHAN_OFFSET                 0x20
#define ATCDMAC300_FIRST_CHAN_BASE      ATCDMAC300_CHAN_CTL
#define ATCDMAC300_GET_CHAN(reg)        (((reg - ATCDMAC300_FIRST_CHAN_BASE) / \
                                            PER_CHAN_OFFSET))
#define ATCDMAC300_GET_OFF(reg, ch)     (reg - (ch * PER_CHAN_OFFSET))

#define DMA_ABT_RESULT (1 << 3)

typedef struct {
    qemu_irq irq;

    /* Channel control registers (n=0~7) */
    uint32_t ChnCtrl;
    uint32_t ChnTranSize;
    uint32_t ChnSrcAddr;
    uint64_t ChnSrcAddrH;
    uint32_t ChnDstAddr;
    uint64_t ChnDstAddrH;
    uint32_t ChnLLPointer;
    uint32_t ChnLLPointerH;
} ATCDMAC300Chan;

struct ATCDMAC300State {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    qemu_irq irq;
    MemoryRegion mmio;
    uint32_t mmio_size;

    /* ID and revision register */
    uint32_t IdRev;

    /* Configuration register */
    uint32_t DMACfg;

    /* Global control registers */
    uint32_t DMACtrl;
    uint32_t ChAbort;

    /* Channel status registers */
    uint32_t IntStatus;
    uint32_t ChEN;

    ATCDMAC300Chan chan[ATCDMAC300_MAX_CHAN];

    /* To support iopmp */
    AddressSpace *iopmp_as;
    uint32_t sid;

    Coroutine *co;
    QEMUBH *bh;
    bool running;
    bool dma_bh_scheduled;
    AioContext *ctx;
    IOThread *iothread;
};

DeviceState *atcdmac300_create(const char *name, hwaddr addr, hwaddr mmio_size,
                               qemu_irq irq);

void atcdmac300_connect_iopmp_as(DeviceState *dev, AddressSpace *iopmp_as,
                                 uint32_t sid);

#endif /* ATCDMAC300_H */
