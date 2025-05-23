// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU STM32 Direct memory access controller (DMA).
 *
 * This includes STM32F1xxxx, STM32F2xxxx and STM32F30x
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 */
#ifndef HW_STM32_DMA_H
#define HW_STM32_DMA_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define STM32_DMA_CHAN_NUMBER    7

#define TYPE_STM32_DMA "stm32-dma"
OBJECT_DECLARE_SIMPLE_TYPE(STM32DmaState, STM32_DMA)

typedef struct STM32DmaChannel {
    bool enabled;

    uint32_t chctl;
    uint32_t chcnt;
    uint32_t chpaddr;
    uint32_t chmaddr;

    uint32_t chcnt_shadow;
} STM32DmaChannel;

typedef struct STM32DmaState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint8_t nr_chans;

    uint32_t intf;

    STM32DmaChannel chan[STM32_DMA_CHAN_NUMBER];

    qemu_irq output[STM32_DMA_CHAN_NUMBER];
} STM32DmaState;

#endif /* HW_STM32_DMA_H */
