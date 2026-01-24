/*
 * DesignWare I2C Bus Controller
 *
 * Copyright (C) 2026, Alano Song <AlanoSong@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DW_I2C_H
#define DW_I2C_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/fifo8.h"
#include "hw/core/registerfields.h"

#define TYPE_DW_I2C "dw.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(DWI2CState, DW_I2C)

#define DW_I2C_TX_FIFO_DEPTH        16
#define DW_I2C_RX_FIFO_DEPTH        16

REG32(DW_IC_CON, 0x00)
    FIELD(DW_IC_CON, MASTER, 0, 1)
    FIELD(DW_IC_CON, SPEED, 1, 2)
    FIELD(DW_IC_CON, RESTART_EN, 5, 1)
REG32(DW_IC_TAR, 0x04)
    FIELD(DW_IC_TAR, ADDRESS, 0, 10)
    FIELD(DW_IC_TAR, 10BITADDR_MASTER, 12, 1)
REG32(DW_IC_SAR, 0x08)
REG32(DW_IC_DATA_CMD, 0x10)
    FIELD(DW_IC_DATA_CMD, DAT, 0, 8)
    FIELD(DW_IC_DATA_CMD, READ, 8, 1)
    FIELD(DW_IC_DATA_CMD, STOP, 9, 1)
    FIELD(DW_IC_DATA_CMD, RESTART, 10, 1)
REG32(DW_IC_SS_SCL_HCNT, 0x14)
REG32(DW_IC_SS_SCL_LCNT, 0x18)
REG32(DW_IC_FS_SCL_HCNT, 0x1c)
REG32(DW_IC_FS_SCL_LCNT, 0x20)
REG32(DW_IC_INTR_STAT, 0x2c)
REG32(DW_IC_INTR_MASK, 0x30)
REG32(DW_IC_RAW_INTR_STAT, 0x34)
    FIELD(DW_IC_RAW_INTR_STAT, RX_UNDER, 0, 1)
    FIELD(DW_IC_RAW_INTR_STAT, RX_OVER, 1, 1)
    FIELD(DW_IC_RAW_INTR_STAT, RX_FULL, 2, 1)
    FIELD(DW_IC_RAW_INTR_STAT, TX_OVER, 3, 1)
    FIELD(DW_IC_RAW_INTR_STAT, TX_EMPTY, 4, 1)
    FIELD(DW_IC_RAW_INTR_STAT, RD_REQ, 5, 1)
    FIELD(DW_IC_RAW_INTR_STAT, TX_ABRT, 6, 1)
    FIELD(DW_IC_RAW_INTR_STAT, RX_DONE, 7, 1)
    FIELD(DW_IC_RAW_INTR_STAT, ACTIVITY, 8, 1)
    FIELD(DW_IC_RAW_INTR_STAT, STOP_DET, 9, 1)
    FIELD(DW_IC_RAW_INTR_STAT, START_DET, 10, 1)
REG32(DW_IC_RX_TL, 0x38)
REG32(DW_IC_TX_TL, 0x3c)
REG32(DW_IC_CLR_INTR, 0x40)
REG32(DW_IC_CLR_RX_UNDER, 0x44)
REG32(DW_IC_CLR_RX_OVER, 0x48)
REG32(DW_IC_CLR_TX_OVER, 0x4c)
REG32(DW_IC_CLR_RD_REQ, 0x50)
REG32(DW_IC_CLR_TX_ABRT, 0x54)
REG32(DW_IC_CLR_RX_DONE, 0x58)
REG32(DW_IC_CLR_ACTIVITY, 0x5c)
REG32(DW_IC_CLR_STOP_DET, 0x60)
REG32(DW_IC_CLR_START_DET, 0x64)
REG32(DW_IC_CLR_GEN_CALL, 0x68)
REG32(DW_IC_ENABLE, 0x6c)
    FIELD(DW_IC_ENABLE, ENABLE, 0, 1)
    FIELD(DW_IC_ENABLE, ABORT, 1, 1)
REG32(DW_IC_STATUS, 0x70)
    FIELD(DW_IC_STATUS, ACTIVITY, 0, 1)
    FIELD(DW_IC_STATUS, TFNF, 1, 1)
    FIELD(DW_IC_STATUS, TFE, 2, 1)
    FIELD(DW_IC_STATUS, RFNE, 3, 1)
    FIELD(DW_IC_STATUS, RFF, 4, 1)
    FIELD(DW_IC_STATUS, MASTER_ACTIVITY, 5, 1)
REG32(DW_IC_TXFLR, 0x74)
REG32(DW_IC_RXFLR, 0x78)
REG32(DW_IC_SDA_HOLD, 0x7c)
REG32(DW_IC_TX_ABRT_SOURCE, 0x80)
    FIELD(DW_IC_TX_ABRT_SOURCE, 7B_ADDR_NOACK, 0, 1)
REG32(DW_IC_ENABLE_STATUS, 0x9c)
    FIELD(DW_IC_ENABLE_STATUS, EN, 0, 1)
REG32(DW_IC_COMP_PARAM_1, 0xf4)
REG32(DW_IC_COMP_VERSION, 0xf8)
REG32(DW_IC_COMP_TYPE, 0xfc)

#define DW_IC_COMP_PARAM_1_SPEED_MODE_FAST (0x2 << 2)
#define DW_IC_COMP_PARAM_1_VALUE (((DW_I2C_TX_FIFO_DEPTH  - 1) & 0xff) << 16 | \
                                  ((DW_I2C_RX_FIFO_DEPTH - 1) & 0xff) << 8 | \
                                  DW_IC_COMP_PARAM_1_SPEED_MODE_FAST)
#define DW_IC_SDA_HOLD_MIN_VERS 0x3131312a /* "111*" == v1.11* */
#define DW_IC_COMP_TYPE_VALUE   0x44570140 /* "DW" + 0x0140 */

typedef struct DWI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
    I2CBus *bus;

    bool bus_active;
    Fifo8 rx_fifo;
    uint32_t addr_mask;

    uint32_t reg_con;
    uint32_t reg_tar;
    uint32_t reg_ss_scl_hcnt;
    uint32_t reg_ss_scl_lcnt;
    uint32_t reg_fs_scl_hcnt;
    uint32_t reg_fs_scl_lcnt;
    uint32_t reg_intr_stat;
    uint32_t reg_intr_mask;
    uint32_t reg_raw_intr_stat;
    uint32_t reg_rx_tl;
    uint32_t reg_tx_tl;
    uint32_t reg_sda_hold;
    uint32_t reg_enable;
    uint32_t reg_status;
    uint32_t reg_txflr;
    uint32_t reg_rxflr;
    uint32_t reg_tx_abrt_source;
    uint32_t reg_enable_status;
    uint32_t reg_comp_param_1;
    uint32_t reg_comp_param_ver;
    uint32_t reg_comp_type_num;
} DWI2CState;

#endif /* DW_I2C_H */
