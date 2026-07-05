/*
 * RP2040 DMA emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_RP2040_DMA_H
#define HW_DMA_RP2040_DMA_H

#include "hw/core/sysbus.h"
#include "system/memory.h"
#include "qom/object.h"

#define TYPE_RP2040_DMA "rp2040-dma"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040DmaState, RP2040_DMA)

#define RP2040_DMA_BASE 0x50000000
#define RP2040_DMA_SIZE 0x4000
#define RP2040_DMA_NUM_CHANNELS 12
#define RP2040_DMA_NUM_IRQS 2
#define RP2040_DMA_NUM_DREQS 64
#define RP2040_DMA_NUM_TIMERS 4

#define RP2040_DREQ_UART0_TX 20
#define RP2040_DREQ_UART0_RX 21
#define RP2040_DREQ_UART1_TX 22
#define RP2040_DREQ_UART1_RX 23
#define RP2040_DREQ_XIP_STREAM 37
#define RP2040_DREQ_XIP_SSITX 38
#define RP2040_DREQ_XIP_SSIRX 39
#define RP2040_DREQ_DMA_TIMER0 59
#define RP2040_DREQ_DMA_TIMER1 60
#define RP2040_DREQ_DMA_TIMER2 61
#define RP2040_DREQ_DMA_TIMER3 62
#define RP2040_DREQ_FORCE 63

typedef struct RP2040DmaChannel {
    uint32_t read_addr;
    uint32_t write_addr;
    uint32_t trans_count;
    uint32_t reload_count;
    uint32_t ctrl;
    bool paced_nyi_logged;
} RP2040DmaChannel;

typedef struct RP2040DmaPacingTimer {
    void *dma;
    QEMUTimer *timer;
    uint64_t period_ns;
    unsigned index;
} RP2040DmaPacingTimer;

struct RP2040DmaState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq[RP2040_DMA_NUM_IRQS];

    RP2040DmaChannel chan[RP2040_DMA_NUM_CHANNELS];
    uint32_t intr;
    uint32_t inte[RP2040_DMA_NUM_IRQS];
    uint32_t intf[RP2040_DMA_NUM_IRQS];
    uint32_t timer[RP2040_DMA_NUM_TIMERS];
    RP2040DmaPacingTimer pacing_timer[RP2040_DMA_NUM_TIMERS];
    uint32_t sniff_ctrl;
    uint32_t sniff_data;
    QEMUBH *dreq_bh;
    bool dreq_servicing;
    bool engine_active;
    uint32_t pending_start;
    bool dreq_level[RP2040_DMA_NUM_DREQS];
    uint32_t pending_dreq[RP2040_DMA_NUM_DREQS];
};

#endif
