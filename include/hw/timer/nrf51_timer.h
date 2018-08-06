/*
* nRF51 System-on-Chip Timer peripheral
 *
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: GPIO registers
 * + sysbus irq
 *
 * Accuracy of the peripheral model:
 * + Only TIMER mode is implemented, COUNTER mode is not implemented.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef NRF51_TIMER_H
#define NRF51_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#define TYPE_NRF51_TIMER "nrf51_soc.timer"
#define NRF51_TIMER(obj) OBJECT_CHECK(Nrf51TimerState, (obj), TYPE_NRF51_TIMER)

#define NRF51_TIMER_REG_COUNT 4

typedef enum {
    NRF51_TIMER_STOPPED = 0,
    NRF51_TIMER_RUNNING
} Nrf51TimerRunstate;

typedef enum {
    NRF51_TIMER_TIMER = 0,
    NRF51_TIMER_COUNTER = 1
} Nrf51TimerMode;

typedef struct Nrf51TimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    QEMUTimer timer;

    uint8_t runstate;

    uint64_t time_offset;
    uint64_t last_visited;

    uint8_t events_compare[NRF51_TIMER_REG_COUNT];
    uint32_t cc[NRF51_TIMER_REG_COUNT];
    uint32_t cc_sorted[NRF51_TIMER_REG_COUNT];
    uint32_t shorts;
    uint32_t inten;
    uint32_t mode;
    uint32_t bitmode;
    uint32_t prescaler;
} Nrf51TimerState;


#endif
