/*
 * Renesas RX65N Multi-Function Timer Pulse Unit 3 (MTU3)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Section 17
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_RENESAS_MTU3_H
#define HW_TIMER_RENESAS_MTU3_H

#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_MTU3 "renesas-mtu3"
typedef struct RXMTU3State RXMTU3State;
DECLARE_INSTANCE_CHECKER(RXMTU3State, RXMTU3, TYPE_RENESAS_MTU3)

/*
 * This device models MTU3 channels 3 and 4 of the RX65N MTU3 module,
 * which share base address 0x000C1200 with an interleaved register layout.
 * ch[0] = MTU channel 3, ch[1] = MTU channel 4.
 */
enum {
    MTU3_NR_CHAN    = 2,
    MTU3_CH_NR_IRQ = 5,   /* TGIA, TGIB, TGIC, TGID, TCIV per channel */
    MTU3_NR_IRQ    = MTU3_CH_NR_IRQ * MTU3_NR_CHAN   /* = 10 */
};

/* IRQ index within the per-channel group */
enum {
    MTU3_IRQ_TGIA = 0,
    MTU3_IRQ_TGIB = 1,
    MTU3_IRQ_TGIC = 2,
    MTU3_IRQ_TGID = 3,
    MTU3_IRQ_TCIV = 4,
};

struct RXMTU3ChanState {
    uint8_t  tcr;
    uint8_t  tmdr1;
    uint8_t  tiorh;
    uint8_t  tiorl;
    uint8_t  tier;
    uint8_t  tsr;
    uint16_t tcnt;
    uint16_t tgra;
    uint16_t tgrb;
    uint16_t tgrc;
    uint16_t tgrd;
    int64_t  tick;       /* virtual-clock time when tcnt was last latched */
    int64_t  div_round;  /* fractional cycles accumulator */
    QEMUTimer timer;
};

struct RXMTU3State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    uint64_t input_freq;

    uint8_t tstr; /* Timer Start Register A (offset 0x80) */
    struct RXMTU3ChanState ch[MTU3_NR_CHAN];
    qemu_irq irq[MTU3_NR_IRQ];
};

#endif /* HW_TIMER_RENESAS_MTU3_H */
