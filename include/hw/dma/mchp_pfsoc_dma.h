/*
 * Microchip PolarFire SoC DMA emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MCHP_PFSOC_DMA_H
#define MCHP_PFSOC_DMA_H

struct mchp_pfsoc_dma_chan {
    uint32_t control;
    uint32_t next_config;
    uint64_t next_bytes;
    uint64_t next_dst;
    uint64_t next_src;
    uint32_t exec_config;
    uint64_t exec_bytes;
    uint64_t exec_dst;
    uint64_t exec_src;
    int state;
};

#define MCHP_PFSOC_DMA_CHANS        4
#define MCHP_PFSOC_DMA_REG_SIZE     0x100000
#define MCHP_PFSOC_DMA_CHAN_NO(reg) \
        ((reg & (MCHP_PFSOC_DMA_REG_SIZE - 1)) >> 12)

typedef struct MchpPfSoCDMAState {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irq;

    struct mchp_pfsoc_dma_chan chan[MCHP_PFSOC_DMA_CHANS];
} MchpPfSoCDMAState;

#define TYPE_MCHP_PFSOC_DMA "mchp.pfsoc.dma"

#define MCHP_PFSOC_DMA(obj) \
    OBJECT_CHECK(MchpPfSoCDMAState, (obj), TYPE_MCHP_PFSOC_DMA)

#endif /* MCHP_PFSOC_DMA_H */
