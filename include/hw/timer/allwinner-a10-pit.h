#ifndef ALLWINNER_A10_PIT_H
#define ALLWINNER_A10_PIT_H

#include "hw/ptimer.h"
#include "hw/sysbus.h"

#define TYPE_AW_A10_PIT "allwinner-A10-timer"

#define AW_PIT_TIMER_MAX        6

typedef struct AwA10PITState AwA10PITState;

typedef struct AwA10TimerContext {
    AwA10PITState *container;
    int index;
} AwA10TimerContext;

struct AwA10PITState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    qemu_irq irq[AW_PIT_TIMER_MAX];
    ptimer_state * ptimer[AW_PIT_TIMER_MAX];
    AwA10TimerContext timer_context[AW_PIT_TIMER_MAX];
    MemoryRegion iomem;
    uint32_t clk_freq[4];

    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t control[AW_PIT_TIMER_MAX];
    uint32_t interval[AW_PIT_TIMER_MAX];
    uint32_t count[AW_PIT_TIMER_MAX];
    uint32_t watch_dog_mode;
    uint32_t watch_dog_control;
    uint32_t count_lo;
    uint32_t count_hi;
    uint32_t count_ctl;
};

#endif
