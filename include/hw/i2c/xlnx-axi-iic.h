/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_I2C_XLNX_AXI_IIC_H
#define HW_I2C_XLNX_AXI_IIC_H

#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_XLNX_AXI_IIC "xlnx-axi-iic"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxAxiIicState, XLNX_AXI_IIC)

#define XLNX_AXI_IIC_REGS_SIZE      0x1000
#define XLNX_AXI_IIC_RX_FIFO_MAX    256

#define XLNX_AXI_IIC_DGIER          0x1C
#define XLNX_AXI_IIC_IISR           0x20
#define XLNX_AXI_IIC_IIER           0x28
#define XLNX_AXI_IIC_RESETR         0x40
#define XLNX_AXI_IIC_CR             0x100
#define XLNX_AXI_IIC_SR             0x104
#define XLNX_AXI_IIC_DTR            0x108
#define XLNX_AXI_IIC_DRR            0x10C
#define XLNX_AXI_IIC_TFO            0x114
#define XLNX_AXI_IIC_RFO            0x118
#define XLNX_AXI_IIC_RFD            0x120

#define XLNX_AXI_IIC_RESET_MASK             0xA

#define XLNX_AXI_IIC_SR_BUS_BUSY_MASK       0x04
#define XLNX_AXI_IIC_SR_RX_FIFO_FULL_MASK   0x20
#define XLNX_AXI_IIC_SR_RX_FIFO_EMPTY_MASK  0x40
#define XLNX_AXI_IIC_SR_TX_FIFO_EMPTY_MASK  0x80

#define XLNX_AXI_IIC_INTR_ARB_LOST_MASK     0x01
#define XLNX_AXI_IIC_INTR_TX_ERROR_MASK     0x02
#define XLNX_AXI_IIC_INTR_TX_EMPTY_MASK     0x04
#define XLNX_AXI_IIC_INTR_RX_FULL_MASK      0x08
#define XLNX_AXI_IIC_INTR_BNB_MASK          0x10

#define XLNX_AXI_IIC_GINTR_ENABLE_MASK      0x80000000UL

#define XLNX_AXI_IIC_TX_DYN_START_MASK      0x0100
#define XLNX_AXI_IIC_TX_DYN_STOP_MASK       0x0200

struct XlnxAxiIicState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    I2CBus *bus;
    char *bus_name;

    uint32_t cr;
    uint32_t isr;
    uint32_t ier;
    uint32_t dgier;
    uint32_t rfd;

    bool in_xfer;
    bool is_recv;
    bool stop_pending;

    uint8_t rx_fifo[XLNX_AXI_IIC_RX_FIFO_MAX];
    int rx_len;
    int rx_pos;
};

#endif
