/*
 * Nuvoton NPCM7xx Timer Controller
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef NPCM7XX_TIMER_H
#define NPCM7XX_TIMER_H

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

/* Each Timer Module (TIM) instance holds five 25 MHz timers. */
#define NPCM7XX_TIMERS_PER_CTRL (5)

/**
 * enum NPCM7xxTimerRegisters - 32-bit register indices.
 */
enum NPCM7xxTimerRegisters {
    NPCM7XX_TIMER_TCSR0,
    NPCM7XX_TIMER_TCSR1,
    NPCM7XX_TIMER_TICR0,
    NPCM7XX_TIMER_TICR1,
    NPCM7XX_TIMER_TDR0,
    NPCM7XX_TIMER_TDR1,
    NPCM7XX_TIMER_TISR,
    NPCM7XX_TIMER_WTCR,
    NPCM7XX_TIMER_TCSR2,
    NPCM7XX_TIMER_TCSR3,
    NPCM7XX_TIMER_TICR2,
    NPCM7XX_TIMER_TICR3,
    NPCM7XX_TIMER_TDR2,
    NPCM7XX_TIMER_TDR3,
    NPCM7XX_TIMER_TCSR4         = 0x0040 / sizeof(uint32_t),
    NPCM7XX_TIMER_TICR4         = 0x0048 / sizeof(uint32_t),
    NPCM7XX_TIMER_TDR4          = 0x0050 / sizeof(uint32_t),
    NPCM7XX_TIMER_NR_REGS,
};

typedef struct NPCM7xxTimerCtrlState NPCM7xxTimerCtrlState;

/**
 * struct NPCM7xxTimer - Individual timer state.
 * @irq: GIC interrupt line to fire on expiration (if enabled).
 * @qtimer: QEMU timer that notifies us on expiration.
 * @expires_ns: Absolute virtual expiration time.
 * @remaining_ns: Remaining time until expiration if timer is paused.
 * @tcsr: The Timer Control and Status Register.
 * @ticr: The Timer Initial Count Register.
 */
typedef struct NPCM7xxTimer {
    NPCM7xxTimerCtrlState *ctrl;

    qemu_irq    irq;
    QEMUTimer   qtimer;
    int64_t     expires_ns;
    int64_t     remaining_ns;

    uint32_t    tcsr;
    uint32_t    ticr;
} NPCM7xxTimer;

/**
 * struct NPCM7xxTimerCtrlState - Timer Module device state.
 * @parent: System bus device.
 * @iomem: Memory region through which registers are accessed.
 * @tisr: The Timer Interrupt Status Register.
 * @wtcr: The Watchdog Timer Control Register.
 * @timer: The five individual timers managed by this module.
 */
struct NPCM7xxTimerCtrlState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t    tisr;
    uint32_t    wtcr;

    NPCM7xxTimer timer[NPCM7XX_TIMERS_PER_CTRL];
};

#define TYPE_NPCM7XX_TIMER "npcm7xx-timer"
#define NPCM7XX_TIMER(obj)                                              \
    OBJECT_CHECK(NPCM7xxTimerCtrlState, (obj), TYPE_NPCM7XX_TIMER)

#endif /* NPCM7XX_TIMER_H */
