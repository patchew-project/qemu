/*
 * DMA control interface.
 *
 * Copyright (c) 2021 Xilinx Inc.
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_DMA_CTRL_IF_H
#define HW_DMA_CTRL_IF_H

#include "hw/hw.h"
#include "exec/memory.h"
#include "qom/object.h"

#define TYPE_DMA_CTRL_IF "dma-ctrl-if"
typedef struct DmaCtrlIfClass DmaCtrlIfClass;
DECLARE_CLASS_CHECKERS(DmaCtrlIfClass, DMA_CTRL_IF,
                       TYPE_DMA_CTRL_IF)

#define DMA_CTRL_IF(obj) \
     INTERFACE_CHECK(DmaCtrlIf, (obj), TYPE_DMA_CTRL_IF)

typedef struct DmaCtrlIf {
    Object Parent;
} DmaCtrlIf;

typedef struct DmaCtrlIfClass {
    InterfaceClass parent;

    /*
     * read: Start a read transfer on the DMA engine implementing the DMA
     * control interface
     *
     * @dma_ctrl: the DMA engine implementing this interface
     * @addr: the address to read
     * @len: the number of bytes to read at 'addr'
     *
     * @return a MemTxResult indicating whether the operation succeeded ('len'
     * bytes were read) or failed.
     */
    MemTxResult (*read)(DmaCtrlIf *dma, hwaddr addr, uint32_t len);
} DmaCtrlIfClass;

/*
 * Start a read transfer on a DMA engine implementing the DMA control
 * interface.
 *
 * @dma_ctrl: the DMA engine implementing this interface
 * @addr: the address to read
 * @len: the number of bytes to read at 'addr'
 *
 * @return a MemTxResult indicating whether the operation succeeded ('len'
 * bytes were read) or failed.
 */
MemTxResult dma_ctrl_if_read(DmaCtrlIf *dma, hwaddr addr, uint32_t len);

#endif /* HW_DMA_CTRL_IF_H */
