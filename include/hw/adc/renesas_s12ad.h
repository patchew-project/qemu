/*
 * Renesas RX65N 12-bit Successive Approximation A/D Converter (S12AD)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Section 40
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ADC_RENESAS_S12AD_H
#define HW_ADC_RENESAS_S12AD_H

#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_S12AD "renesas-s12ad"
typedef struct RX65NS12ADState RX65NS12ADState;
DECLARE_INSTANCE_CHECKER(RX65NS12ADState, RENESAS_S12AD, TYPE_RENESAS_S12AD)

#define S12AD_NR_CHANNELS   16  /* AN000–AN015 */

enum {
    S12AD_IRQ_S12ADI = 0,  /* end of scan (group A) */
    S12AD_IRQ_GBADI  = 1,  /* end of scan (group B) */
    S12AD_NR_IRQ     = 2,
};

struct RX65NS12ADState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    uint64_t input_freq;

    /* Registers */
    uint16_t adcsr;     /* A/D Control/Status */
    uint16_t adansa0;   /* Channel select 0 (AN000–AN015) */
    uint16_t adansa1;   /* Channel select 1 (AN016–AN020) */
    uint16_t adads0;    /* Average enable 0 */
    uint16_t adads1;    /* Average enable 1 */
    uint8_t  adadc;     /* Average count */
    uint16_t adcer;     /* Control extended */
    uint16_t adstrgr;   /* Start trigger select */
    uint16_t adexicr;   /* Extended input */
    uint16_t adansb0;   /* Group B channel select 0 */
    uint16_t adansb1;   /* Group B channel select 1 */
    uint16_t addr[S12AD_NR_CHANNELS]; /* Result registers AN000–AN015 */
    uint16_t adrd;      /* Temperature sensor / internal ref result */
    uint16_t adsam;     /* Conversion time setting */
    uint8_t adsampr;    /* ADSAM write protection */
    uint8_t ext_regs[0x80]; /* Extended configuration, offsets 0x70-0xef */

    qemu_irq irq[S12AD_NR_IRQ];
    QEMUTimer conv_timer; /* models conversion delay */
};

#endif /* HW_ADC_RENESAS_S12AD_H */
