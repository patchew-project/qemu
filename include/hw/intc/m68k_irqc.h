/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * QEMU Motorla 680x0 IRQ Controller
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#ifndef M68K_IRQC_H
#define M68K_IRQC_H

#include "hw/sysbus.h"

#define TYPE_M68K_IRQC "m68k-irq-controller"
#define M68K_IRQC(obj) OBJECT_CHECK(M68KIRQCState, (obj), \
                                    TYPE_M68K_IRQC)

typedef struct M68KIRQCState {
    SysBusDevice parent_obj;

    uint8_t ipr;

    /* statistics */
    uint64_t stats_irq_count[7];
} M68KIRQCState;

#endif
