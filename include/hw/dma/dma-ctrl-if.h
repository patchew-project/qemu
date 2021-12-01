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
#include "qom/object.h"

#define TYPE_DMA_CTRL_IF "dma-ctrl-if"
typedef struct DmaCtrlIfClass DmaCtrlIfClass;
DECLARE_CLASS_CHECKERS(DmaCtrlIfClass, DMA_CTRL_IF,
                       TYPE_DMA_CTRL_IF)

#define DMA_CTRL_IF(obj) \
     INTERFACE_CHECK(DmaCtrlIf, (obj), TYPE_DMA_CTRL_IF)

typedef void (*dmactrlif_notify_fn)(void *opaque);

typedef struct DmaCtrlIfNotify {
    void *opaque;
    dmactrlif_notify_fn cb;
} DmaCtrlIfNotify;

typedef struct DmaCtrlIf {
    Object Parent;
} DmaCtrlIf;

typedef struct DmaCtrlIfClass {
    InterfaceClass parent;

    /*
     * read: Start a read transfer on the DMA implementing the DMA control
     * interface
     *
     * @dma_ctrl: the DMA implementing this interface
     * @addr: the address to read
     * @len: the amount of bytes to read at 'addr'
     * @notify: the structure containg a callback to call and opaque pointer
     * to pass the callback when the transfer has been completed
     * @start_dma: true for starting the DMA transfer and false for just
     * refilling and proceding an already started transfer
     */
    void (*read)(DmaCtrlIf *dma, hwaddr addr, uint32_t len,
                 DmaCtrlIfNotify *notify, bool start_dma);
} DmaCtrlIfClass;

/*
 * Start a read transfer on a DMA implementing the DMA control interface.
 * The DMA will notify the caller that 'len' bytes have been read at 'addr'
 * through the callback in the DmaCtrlIfNotify structure. For allowing refilling
 * an already started transfer the DMA notifies the caller before considering
 * the transfer done (e.g. before setting done flags, generating IRQs and
 * modifying other relevant internal device state).
 *
 * @dma_ctrl: the DMA implementing this interface
 * @addr: the address to read
 * @len: the amount of bytes to read at 'addr'
 * @notify: the structure containing a callback to call and opaque pointer
 * to pass the callback when the transfer has been completed
 * @start_dma: true for starting the DMA transfer and false for just
 * refilling and proceding an already started transfer
 */
void dma_ctrl_if_read_with_notify(DmaCtrlIf *dma, hwaddr addr, uint32_t len,
                                  DmaCtrlIfNotify *notify, bool start_dma);

#endif /* HW_DMA_CTRL_IF_H */
