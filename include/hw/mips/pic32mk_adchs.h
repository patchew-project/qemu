/*
 * PIC32MK ADCHS (High-Speed ADC) peripheral — device interface
 * Datasheet: DS60001519E, §22
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_PIC32MK_ADCHS_H
#define HW_MIPS_PIC32MK_ADCHS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/mips/pic32mk.h"

#define TYPE_PIC32MK_ADCHS  "pic32mk-adchs"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKADCHSState, PIC32MK_ADCHS)

struct PIC32MKADCHSState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    /* Control registers */
    uint32_t adccon1;
    uint32_t adccon2;
    uint32_t adccon3;
    uint32_t adctrgmode;

    /* Input mode control */
    uint32_t adcimcon[4];

    /* Interrupt enable (result ready) */
    uint32_t adcgirqen[2];

    /* Channel scan select */
    uint32_t adccss[2];

    /* Data ready status */
    uint32_t adcdstat[2];

    /* Compare enable & values */
    uint32_t adccmpen[4];
    uint32_t adccmp[4];
    uint32_t adccmpcon[4];

    /* Digital filters */
    uint32_t adcfltr[4];

    /* Trigger configuration */
    uint32_t adctrg[7];
    uint32_t adctrgsns;

    /* Sampling time per module */
    uint32_t adctime[6];   /* ADC0TIME … ADC5TIME */

    /* Early interrupt enable / status */
    uint32_t adceien[2];
    uint32_t adceistat[2];

    /* Analog module control */
    uint32_t adcancon;

    /* Misc */
    uint32_t adcbase;

    /* Per-module configuration */
    uint32_t adccfg[8];    /* ADC0CFG … ADC7CFG */

    /* System configuration */
    uint32_t adcsyscfg[2];

    /* Conversion data registers (indexed by channel 0–53) */
    uint32_t adcdata[PIC32MK_ADC_MAX_CH];

    /*
     * Host-injectable analog input values (12-bit, 0–4095).
     * When a conversion is triggered, analog_input[ch] is copied to
     * adcdata[ch].  Set via QOM property "adc-ch<N>".
     */
    uint16_t analog_input[PIC32MK_ADC_MAX_CH];

    /* IRQ outputs to EVIC */
    qemu_irq irq_eos;     /* End-of-scan  (IRQ 101) */
    qemu_irq irq_main;    /* Main ADC     (IRQ 92)  */
};

#endif /* HW_MIPS_PIC32MK_ADCHS_H */
