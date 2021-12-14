/*
 * DMA control interface.
 *
 * Copyright (c) 2021 Xilinx Inc.
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/dma/dma-ctrl-if.h"

void dma_ctrl_if_read(DmaCtrlIf *dma, hwaddr addr, uint32_t len)
{
    DmaCtrlIfClass *dcc =  DMA_CTRL_IF_GET_CLASS(dma);
    dcc->read(dma, addr, len);
}

static const TypeInfo dma_ctrl_if_info = {
    .name          = TYPE_DMA_CTRL_IF,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(DmaCtrlIfClass),
};

static void dma_ctrl_if_register_types(void)
{
    type_register_static(&dma_ctrl_if_info);
}

type_init(dma_ctrl_if_register_types)
