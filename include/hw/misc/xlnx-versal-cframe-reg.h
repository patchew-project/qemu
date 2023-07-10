/*
 * QEMU model of the Configuration Frame Control module
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Written by Francisco Iglesias <francisco.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * References:
 * [1] Versal ACAP Technical Reference Manual,
 *     https://www.xilinx.com/support/documentation/architecture-manuals/am011-versal-acap-trm.pdf
 *
 * [2] Versal ACAP Register Reference,
 *     https://www.xilinx.com/htmldocs/registers/am012/am012-versal-register-reference.html
 */
#ifndef HW_MISC_XLNX_VERSAL_CFRAME_REG_H
#define HW_MISC_XLNX_VERSAL_CFRAME_REG_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/misc/xlnx-cfi-if.h"

#define TYPE_XLNX_VERSAL_CFRAME_REG "xlnx,cframe-reg"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalCFrameReg, XLNX_VERSAL_CFRAME_REG)

/*
 * The registers in this module are 128 bits wide but it is ok to write
 * and read them through 4 sequential 32 bit accesses (address[3:2] = 0,
 * 1, 2, 3).
 */
REG32(CRC0, 0x0)
    FIELD(CRC, CRC, 0, 32)
REG32(CRC1, 0x4)
REG32(CRC2, 0x8)
REG32(CRC3, 0xc)
REG32(FAR0, 0x10)
    FIELD(FAR0, SEGMENT, 23, 2)
    FIELD(FAR0, BLOCKTYPE, 20, 3)
    FIELD(FAR0, FRAME_ADDR, 0, 20)
REG32(FAR1, 0x14)
REG32(FAR2, 0x18)
REG32(FAR3, 0x1c)
REG32(FAR_SFR0, 0x20)
    FIELD(FAR_SFR0, BLOCKTYPE, 20, 3)
    FIELD(FAR_SFR0, FRAME_ADDR, 0, 20)
REG32(FAR_SFR1, 0x24)
REG32(FAR_SFR2, 0x28)
REG32(FAR_SFR3, 0x2c)
REG32(FDRI0, 0x40)
REG32(FDRI1, 0x44)
REG32(FDRI2, 0x48)
REG32(FDRI3, 0x4c)
REG32(FRCNT0, 0x50)
    FIELD(FRCNT0, FRCNT, 0, 32)
REG32(FRCNT1, 0x54)
REG32(FRCNT2, 0x58)
REG32(FRCNT3, 0x5c)
REG32(CMD0, 0x60)
    FIELD(CMD0, CMD, 0, 5)
REG32(CMD1, 0x64)
REG32(CMD2, 0x68)
REG32(CMD3, 0x6c)
REG32(CR_MASK0, 0x70)
REG32(CR_MASK1, 0x74)
REG32(CR_MASK2, 0x78)
REG32(CR_MASK3, 0x7c)
REG32(CTL0, 0x80)
    FIELD(CTL, PER_FRAME_CRC, 0, 1)
REG32(CTL1, 0x84)
REG32(CTL2, 0x88)
REG32(CTL3, 0x8c)
REG32(CFRM_ISR0, 0x150)
    FIELD(CFRM_ISR0, READ_BROADCAST_ERROR, 21, 1)
    FIELD(CFRM_ISR0, CMD_MISSING_ERROR, 20, 1)
    FIELD(CFRM_ISR0, RW_ROWOFF_ERROR, 19, 1)
    FIELD(CFRM_ISR0, READ_REG_ADDR_ERROR, 18, 1)
    FIELD(CFRM_ISR0, READ_BLK_TYPE_ERROR, 17, 1)
    FIELD(CFRM_ISR0, READ_FRAME_ADDR_ERROR, 16, 1)
    FIELD(CFRM_ISR0, WRITE_REG_ADDR_ERROR, 15, 1)
    FIELD(CFRM_ISR0, WRITE_BLK_TYPE_ERROR, 13, 1)
    FIELD(CFRM_ISR0, WRITE_FRAME_ADDR_ERROR, 12, 1)
    FIELD(CFRM_ISR0, MFW_OVERRUN_ERROR, 11, 1)
    FIELD(CFRM_ISR0, FAR_FIFO_UNDERFLOW, 10, 1)
    FIELD(CFRM_ISR0, FAR_FIFO_OVERFLOW, 9, 1)
    FIELD(CFRM_ISR0, PER_FRAME_SEQ_ERROR, 8, 1)
    FIELD(CFRM_ISR0, CRC_ERROR, 7, 1)
    FIELD(CFRM_ISR0, WRITE_OVERRUN_ERROR, 6, 1)
    FIELD(CFRM_ISR0, READ_OVERRUN_ERROR, 5, 1)
    FIELD(CFRM_ISR0, CMD_INTERRUPT_ERROR, 4, 1)
    FIELD(CFRM_ISR0, WRITE_INTERRUPT_ERROR, 3, 1)
    FIELD(CFRM_ISR0, READ_INTERRUPT_ERROR, 2, 1)
    FIELD(CFRM_ISR0, SEU_CRC_ERROR, 1, 1)
    FIELD(CFRM_ISR0, SEU_ECC_ERROR, 0, 1)
REG32(CFRM_ISR1, 0x154)
REG32(CFRM_ISR2, 0x158)
REG32(CFRM_ISR3, 0x15c)
REG32(CFRM_IMR0, 0x160)
    FIELD(CFRM_IMR0, READ_BROADCAST_ERROR, 21, 1)
    FIELD(CFRM_IMR0, CMD_MISSING_ERROR, 20, 1)
    FIELD(CFRM_IMR0, RW_ROWOFF_ERROR, 19, 1)
    FIELD(CFRM_IMR0, READ_REG_ADDR_ERROR, 18, 1)
    FIELD(CFRM_IMR0, READ_BLK_TYPE_ERROR, 17, 1)
    FIELD(CFRM_IMR0, READ_FRAME_ADDR_ERROR, 16, 1)
    FIELD(CFRM_IMR0, WRITE_REG_ADDR_ERROR, 15, 1)
    FIELD(CFRM_IMR0, WRITE_BLK_TYPE_ERROR, 13, 1)
    FIELD(CFRM_IMR0, WRITE_FRAME_ADDR_ERROR, 12, 1)
    FIELD(CFRM_IMR0, MFW_OVERRUN_ERROR, 11, 1)
    FIELD(CFRM_IMR0, FAR_FIFO_UNDERFLOW, 10, 1)
    FIELD(CFRM_IMR0, FAR_FIFO_OVERFLOW, 9, 1)
    FIELD(CFRM_IMR0, PER_FRAME_SEQ_ERROR, 8, 1)
    FIELD(CFRM_IMR0, CRC_ERROR, 7, 1)
    FIELD(CFRM_IMR0, WRITE_OVERRUN_ERROR, 6, 1)
    FIELD(CFRM_IMR0, READ_OVERRUN_ERROR, 5, 1)
    FIELD(CFRM_IMR0, CMD_INTERRUPT_ERROR, 4, 1)
    FIELD(CFRM_IMR0, WRITE_INTERRUPT_ERROR, 3, 1)
    FIELD(CFRM_IMR0, READ_INTERRUPT_ERROR, 2, 1)
    FIELD(CFRM_IMR0, SEU_CRC_ERROR, 1, 1)
    FIELD(CFRM_IMR0, SEU_ECC_ERROR, 0, 1)
REG32(CFRM_IMR1, 0x164)
REG32(CFRM_IMR2, 0x168)
REG32(CFRM_IMR3, 0x16c)
REG32(CFRM_IER0, 0x170)
    FIELD(CFRM_IER0, READ_BROADCAST_ERROR, 21, 1)
    FIELD(CFRM_IER0, CMD_MISSING_ERROR, 20, 1)
    FIELD(CFRM_IER0, RW_ROWOFF_ERROR, 19, 1)
    FIELD(CFRM_IER0, READ_REG_ADDR_ERROR, 18, 1)
    FIELD(CFRM_IER0, READ_BLK_TYPE_ERROR, 17, 1)
    FIELD(CFRM_IER0, READ_FRAME_ADDR_ERROR, 16, 1)
    FIELD(CFRM_IER0, WRITE_REG_ADDR_ERROR, 15, 1)
    FIELD(CFRM_IER0, WRITE_BLK_TYPE_ERROR, 13, 1)
    FIELD(CFRM_IER0, WRITE_FRAME_ADDR_ERROR, 12, 1)
    FIELD(CFRM_IER0, MFW_OVERRUN_ERROR, 11, 1)
    FIELD(CFRM_IER0, FAR_FIFO_UNDERFLOW, 10, 1)
    FIELD(CFRM_IER0, FAR_FIFO_OVERFLOW, 9, 1)
    FIELD(CFRM_IER0, PER_FRAME_SEQ_ERROR, 8, 1)
    FIELD(CFRM_IER0, CRC_ERROR, 7, 1)
    FIELD(CFRM_IER0, WRITE_OVERRUN_ERROR, 6, 1)
    FIELD(CFRM_IER0, READ_OVERRUN_ERROR, 5, 1)
    FIELD(CFRM_IER0, CMD_INTERRUPT_ERROR, 4, 1)
    FIELD(CFRM_IER0, WRITE_INTERRUPT_ERROR, 3, 1)
    FIELD(CFRM_IER0, READ_INTERRUPT_ERROR, 2, 1)
    FIELD(CFRM_IER0, SEU_CRC_ERROR, 1, 1)
    FIELD(CFRM_IER0, SEU_ECC_ERROR, 0, 1)
REG32(CFRM_IER1, 0x174)
REG32(CFRM_IER2, 0x178)
REG32(CFRM_IER3, 0x17c)
REG32(CFRM_IDR0, 0x180)
    FIELD(CFRM_IDR0, READ_BROADCAST_ERROR, 21, 1)
    FIELD(CFRM_IDR0, CMD_MISSING_ERROR, 20, 1)
    FIELD(CFRM_IDR0, RW_ROWOFF_ERROR, 19, 1)
    FIELD(CFRM_IDR0, READ_REG_ADDR_ERROR, 18, 1)
    FIELD(CFRM_IDR0, READ_BLK_TYPE_ERROR, 17, 1)
    FIELD(CFRM_IDR0, READ_FRAME_ADDR_ERROR, 16, 1)
    FIELD(CFRM_IDR0, WRITE_REG_ADDR_ERROR, 15, 1)
    FIELD(CFRM_IDR0, WRITE_BLK_TYPE_ERROR, 13, 1)
    FIELD(CFRM_IDR0, WRITE_FRAME_ADDR_ERROR, 12, 1)
    FIELD(CFRM_IDR0, MFW_OVERRUN_ERROR, 11, 1)
    FIELD(CFRM_IDR0, FAR_FIFO_UNDERFLOW, 10, 1)
    FIELD(CFRM_IDR0, FAR_FIFO_OVERFLOW, 9, 1)
    FIELD(CFRM_IDR0, PER_FRAME_SEQ_ERROR, 8, 1)
    FIELD(CFRM_IDR0, CRC_ERROR, 7, 1)
    FIELD(CFRM_IDR0, WRITE_OVERRUN_ERROR, 6, 1)
    FIELD(CFRM_IDR0, READ_OVERRUN_ERROR, 5, 1)
    FIELD(CFRM_IDR0, CMD_INTERRUPT_ERROR, 4, 1)
    FIELD(CFRM_IDR0, WRITE_INTERRUPT_ERROR, 3, 1)
    FIELD(CFRM_IDR0, READ_INTERRUPT_ERROR, 2, 1)
    FIELD(CFRM_IDR0, SEU_CRC_ERROR, 1, 1)
    FIELD(CFRM_IDR0, SEU_ECC_ERROR, 0, 1)
REG32(CFRM_IDR1, 0x184)
REG32(CFRM_IDR2, 0x188)
REG32(CFRM_IDR3, 0x18c)
REG32(CFRM_ITR0, 0x190)
    FIELD(CFRM_ITR0, READ_BROADCAST_ERROR, 21, 1)
    FIELD(CFRM_ITR0, CMD_MISSING_ERROR, 20, 1)
    FIELD(CFRM_ITR0, RW_ROWOFF_ERROR, 19, 1)
    FIELD(CFRM_ITR0, READ_REG_ADDR_ERROR, 18, 1)
    FIELD(CFRM_ITR0, READ_BLK_TYPE_ERROR, 17, 1)
    FIELD(CFRM_ITR0, READ_FRAME_ADDR_ERROR, 16, 1)
    FIELD(CFRM_ITR0, WRITE_REG_ADDR_ERROR, 15, 1)
    FIELD(CFRM_ITR0, WRITE_BLK_TYPE_ERROR, 13, 1)
    FIELD(CFRM_ITR0, WRITE_FRAME_ADDR_ERROR, 12, 1)
    FIELD(CFRM_ITR0, MFW_OVERRUN_ERROR, 11, 1)
    FIELD(CFRM_ITR0, FAR_FIFO_UNDERFLOW, 10, 1)
    FIELD(CFRM_ITR0, FAR_FIFO_OVERFLOW, 9, 1)
    FIELD(CFRM_ITR0, PER_FRAME_SEQ_ERROR, 8, 1)
    FIELD(CFRM_ITR0, CRC_ERROR, 7, 1)
    FIELD(CFRM_ITR0, WRITE_OVERRUN_ERROR, 6, 1)
    FIELD(CFRM_ITR0, READ_OVERRUN_ERROR, 5, 1)
    FIELD(CFRM_ITR0, CMD_INTERRUPT_ERROR, 4, 1)
    FIELD(CFRM_ITR0, WRITE_INTERRUPT_ERROR, 3, 1)
    FIELD(CFRM_ITR0, READ_INTERRUPT_ERROR, 2, 1)
    FIELD(CFRM_ITR0, SEU_CRC_ERROR, 1, 1)
    FIELD(CFRM_ITR0, SEU_ECC_ERROR, 0, 1)
REG32(CFRM_ITR1, 0x194)
REG32(CFRM_ITR2, 0x198)
REG32(CFRM_ITR3, 0x19c)
REG32(SEU_SYNDRM00, 0x1a0)
REG32(SEU_SYNDRM01, 0x1a4)
REG32(SEU_SYNDRM02, 0x1a8)
REG32(SEU_SYNDRM03, 0x1ac)
REG32(SEU_SYNDRM10, 0x1b0)
REG32(SEU_SYNDRM11, 0x1b4)
REG32(SEU_SYNDRM12, 0x1b8)
REG32(SEU_SYNDRM13, 0x1bc)
REG32(SEU_SYNDRM20, 0x1c0)
REG32(SEU_SYNDRM21, 0x1c4)
REG32(SEU_SYNDRM22, 0x1c8)
REG32(SEU_SYNDRM23, 0x1cc)
REG32(SEU_SYNDRM30, 0x1d0)
REG32(SEU_SYNDRM31, 0x1d4)
REG32(SEU_SYNDRM32, 0x1d8)
REG32(SEU_SYNDRM33, 0x1dc)
REG32(SEU_VIRTUAL_SYNDRM0, 0x1e0)
REG32(SEU_VIRTUAL_SYNDRM1, 0x1e4)
REG32(SEU_VIRTUAL_SYNDRM2, 0x1e8)
REG32(SEU_VIRTUAL_SYNDRM3, 0x1ec)
REG32(SEU_CRC0, 0x1f0)
REG32(SEU_CRC1, 0x1f4)
REG32(SEU_CRC2, 0x1f8)
REG32(SEU_CRC3, 0x1fc)
REG32(CFRAME_FAR_BOT0, 0x200)
REG32(CFRAME_FAR_BOT1, 0x204)
REG32(CFRAME_FAR_BOT2, 0x208)
REG32(CFRAME_FAR_BOT3, 0x20c)
REG32(CFRAME_FAR_TOP0, 0x210)
REG32(CFRAME_FAR_TOP1, 0x214)
REG32(CFRAME_FAR_TOP2, 0x218)
REG32(CFRAME_FAR_TOP3, 0x21c)
REG32(LAST_FRAME_BOT0, 0x220)
    FIELD(LAST_FRAME_BOT0, BLOCKTYPE1_LAST_FRAME_LSB, 20, 12)
    FIELD(LAST_FRAME_BOT0, BLOCKTYPE0_LAST_FRAME, 0, 20)
REG32(LAST_FRAME_BOT1, 0x224)
    FIELD(LAST_FRAME_BOT1, BLOCKTYPE3_LAST_FRAME_LSB, 28, 4)
    FIELD(LAST_FRAME_BOT1, BLOCKTYPE2_LAST_FRAME, 8, 20)
    FIELD(LAST_FRAME_BOT1, BLOCKTYPE1_LAST_FRAME_MSB, 0, 8)
REG32(LAST_FRAME_BOT2, 0x228)
    FIELD(LAST_FRAME_BOT2, BLOCKTYPE3_LAST_FRAME_MSB, 0, 16)
REG32(LAST_FRAME_BOT3, 0x22c)
REG32(LAST_FRAME_TOP0, 0x230)
    FIELD(LAST_FRAME_TOP0, BLOCKTYPE5_LAST_FRAME_LSB, 20, 12)
    FIELD(LAST_FRAME_TOP0, BLOCKTYPE4_LAST_FRAME, 0, 20)
REG32(LAST_FRAME_TOP1, 0x234)
    FIELD(LAST_FRAME_TOP1, BLOCKTYPE6_LAST_FRAME, 8, 20)
    FIELD(LAST_FRAME_TOP1, BLOCKTYPE5_LAST_FRAME_MSB, 0, 8)
REG32(LAST_FRAME_TOP2, 0x238)
REG32(LAST_FRAME_TOP3, 0x23c)

#define CFRAME_REG_R_MAX (R_LAST_FRAME_TOP3 + 1)

#define FRAME_NUM_QWORDS 25
#define FRAME_NUM_WORDS (FRAME_NUM_QWORDS * 4) /* 25 * 128 bits */

typedef struct XlnxCFrame {
    uint32_t addr;
    uint32_t idx;
    uint32_t data[FRAME_NUM_WORDS];
} XlnxCFrame;

struct XlnxVersalCFrameReg {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion iomem_fdri;
    qemu_irq irq_cfrm_imr;

    /* 128-bit wfifo.  */
    uint32_t wfifo[4];

    uint32_t regs[CFRAME_REG_R_MAX];
    RegisterInfo regs_info[CFRAME_REG_R_MAX];

    bool rowon;
    bool wcfg;
    bool rcfg;

    GArray *cframes;
    XlnxCFrame new_f;
    uint32_t *cf_data;
    uint32_t cf_dlen;

    struct {
        XlnxCfiIf *cfu_fdro;
        uint32_t blktype_num_frames[7];
    } cfg;
    bool row_configured;
};

#endif
