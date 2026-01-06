/*
 *  DesignWare I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2026 Alano Song <AlanoSong@163.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef DW_I2C_H
#define DW_I2C_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/fifo8.h"

#define TYPE_DW_I2C "dw.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(DWI2CState, DW_I2C)

#define DW_I2C_TX_FIFO_DEPTH        16
#define DW_I2C_RX_FIFO_DEPTH        16

#define DW_IC_CON                   0x00
#define DW_IC_TAR                   0x04
#define DW_IC_SAR                   0x08
#define DW_IC_DATA_CMD              0x10
#define DW_IC_SS_SCL_HCNT           0x14
#define DW_IC_SS_SCL_LCNT           0x18
#define DW_IC_FS_SCL_HCNT           0x1c
#define DW_IC_FS_SCL_LCNT           0x20
#define DW_IC_INTR_STAT             0x2c
#define DW_IC_INTR_MASK             0x30
#define DW_IC_RAW_INTR_STAT         0x34
#define DW_IC_RX_TL                 0x38
#define DW_IC_TX_TL                 0x3c
#define DW_IC_CLR_INTR              0x40
#define DW_IC_CLR_RX_UNDER          0x44
#define DW_IC_CLR_RX_OVER           0x48
#define DW_IC_CLR_TX_OVER           0x4c
#define DW_IC_CLR_RD_REQ            0x50
#define DW_IC_CLR_TX_ABRT           0x54
#define DW_IC_CLR_RX_DONE           0x58
#define DW_IC_CLR_ACTIVITY          0x5c
#define DW_IC_CLR_STOP_DET          0x60
#define DW_IC_CLR_START_DET         0x64
#define DW_IC_CLR_GEN_CALL          0x68
#define DW_IC_ENABLE                0x6c
#define DW_IC_STATUS                0x70
#define DW_IC_TXFLR                 0x74
#define DW_IC_RXFLR                 0x78
#define DW_IC_SDA_HOLD              0x7c
#define DW_IC_TX_ABRT_SOURCE        0x80
#define DW_IC_ENABLE_STATUS         0x9c
#define DW_IC_COMP_PARAM_1          0xf4
#define DW_IC_COMP_VERSION          0xf8
#define DW_IC_COMP_TYPE             0xfc

#define DW_IC_CON_MASTER            BIT(0)
#define DW_IC_CON_SPEED_STANDARD    (0x1 << 1)
#define DW_IC_CON_SPEED_FAST        (0x2 << 1)
#define DW_IC_CON_SPEED_HIGH        (0x3 << 1)
#define DW_IC_CON_RESTART_EN        BIT(5)

#define DW_IC_TAR_10BITADDR_MASTER  BIT(12)

#define DW_IC_DATA_CMD_DAT_MASK     0xff
#define DW_IC_DATA_CMD_READ         BIT(8)
#define DW_IC_DATA_CMD_STOP         BIT(9)
#define DW_IC_DATA_CMD_RESTART      BIT(10)

#define DW_IC_INTR_RX_UNDER         BIT(0)
#define DW_IC_INTR_RX_OVER          BIT(1)
#define DW_IC_INTR_RX_FULL          BIT(2)
#define DW_IC_INTR_TX_OVER          BIT(3)
#define DW_IC_INTR_TX_EMPTY         BIT(4)
#define DW_IC_INTR_RD_REQ           BIT(5)
#define DW_IC_INTR_TX_ABRT          BIT(6)
#define DW_IC_INTR_RX_DONE          BIT(7)
#define DW_IC_INTR_ACTIVITY         BIT(8)
#define DW_IC_INTR_STOP_DET         BIT(9)
#define DW_IC_INTR_START_DET        BIT(10)

#define DW_IC_ENABLE_ENABLE         BIT(0)
#define DW_IC_ENABLE_ABORT          BIT(1)

#define DW_IC_STATUS_ACTIVITY           BIT(0)
#define DW_IC_STATUS_TFNF               BIT(1)
#define DW_IC_STATUS_TFE                BIT(2)
#define DW_IC_STATUS_RFNE               BIT(3)
#define DW_IC_STATUS_RFF                BIT(4)
#define DW_IC_STATUS_MASTER_ACTIVITY    BIT(5)

#define DW_IC_TX_ABRT_7B_ADDR_NOACK     BIT(0)

#define DW_IC_ENABLE_STATUS_EN          BIT(0)

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
