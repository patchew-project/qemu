/*
 * nRF51 System-on-Chip Timer peripheral
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

#define NRF51_TIMER_SIZE         0x1000

#define NRF51_TIMER_TASK_START 0x000
#define NRF51_TIMER_TASK_STOP 0x004
#define NRF51_TIMER_TASK_COUNT 0x008
#define NRF51_TIMER_TASK_CLEAR 0x00C
#define NRF51_TIMER_TASK_SHUTDOWN 0x010
#define NRF51_TIMER_TASK_CAPTURE_0 0x040
#define NRF51_TIMER_TASK_CAPTURE_3 0x04C

#define NRF51_TIMER_EVENT_COMPARE_0 0x140
#define NRF51_TIMER_EVENT_COMPARE_3 0x14C

#define NRF51_TIMER_REG_SHORTS 0x200
#define NRF51_TIMER_REG_SHORTS_MASK 0xf0f
#define NRF51_TIMER_REG_INTENSET 0x304
#define NRF51_TIMER_REG_INTENCLR 0x308
#define NRF51_TIMER_REG_INTEN_MASK 0xf0000
#define NRF51_TIMER_REG_MODE 0x504
#define NRF51_TIMER_REG_MODE_MASK 0x01
#define NRF51_TIMER_REG_BITMODE 0x508
#define NRF51_TIMER_REG_BITMODE_MASK 0x03
#define NRF51_TIMER_REG_PRESCALER 0x510
#define NRF51_TIMER_REG_PRESCALER_MASK 0x0F
#define NRF51_TIMER_REG_CC0 0x540
#define NRF51_TIMER_REG_CC3 0x54C

typedef struct Nrf51TimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    QEMUTimer timer;

    bool running;

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
