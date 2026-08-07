/*
 * K230 DesignWare I2C controller
 *
 * Copyright (c) 2026 Wang Zhongyu <wzy15515798875@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_I2C_K230_I2C_H
#define HW_I2C_K230_I2C_H

#include <stdint.h>

#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qemu/fifo32.h"
#include "qemu/fifo8.h"

#define K230_I2C_RX_FIFO_SIZE 64
#define K230_I2C_TX_FIFO_SIZE 32

#define TYPE_K230_I2C "k230-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(K230I2CState, K230_I2C)

typedef enum K230I2CTransferState {
    K230_I2C_STATE_IDLE,
    K230_I2C_STATE_SENDING,
    K230_I2C_STATE_RECEIVING,
} K230I2CTransferState;

struct K230I2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    I2CBus *bus;
    qemu_irq irq;

    /* K230 I2C register list */
    uint32_t ic_con;
    uint32_t ic_tar;
    uint32_t ic_sar;
    uint32_t ic_hs_maddr;
    uint32_t ic_ss_scl_hcnt;
    uint32_t ic_ss_scl_lcnt;
    uint32_t ic_fs_scl_hcnt;
    uint32_t ic_fs_scl_lcnt;
    uint32_t ic_hs_scl_hcnt;
    uint32_t ic_hs_scl_lcnt;

    uint32_t ic_intr_mask;
    uint32_t ic_raw_intr_stat;

    uint32_t ic_rx_tl;
    uint32_t ic_tx_tl;

    uint32_t ic_enable;

    uint32_t ic_tx_abrt_source;
    uint32_t ic_sda_setup;
    uint32_t ic_slv_data_nack_only;
    uint32_t ic_dma_cr;
    uint32_t ic_dma_tdlr;
    uint32_t ic_dma_rdlr;
    uint32_t ic_ack_general_call;
    uint32_t ic_enable_status;

    Fifo8 rx_fifo;
    Fifo32 tx_fifo;
    uint32_t transfer_state;
};

#endif /* HW_I2C_K230_I2C_H */
