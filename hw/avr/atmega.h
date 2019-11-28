/*
 * QEMU ATmega MCU
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AVR_ATMEGA_H
#define HW_AVR_ATMEGA_H

#include "hw/char/avr_usart.h"
#include "hw/char/avr_usart.h"
#include "hw/timer/avr_timer16.h"
#include "hw/misc/avr_mask.h"
#include "target/avr/cpu.h"

#define TYPE_ATMEGA     "ATmega"
#define TYPE_ATMEGA168  "ATmega168"
#define TYPE_ATMEGA328  "ATmega328"
#define TYPE_ATMEGA1280 "ATmega1280"
#define TYPE_ATMEGA2560 "ATmega2560"
#define ATMEGA(obj)     OBJECT_CHECK(AtmegaState, (obj), TYPE_ATMEGA)

#define USART_MAX 4
#define TIMER_MAX 6

typedef struct AtmegaState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    AVRCPU cpu;
    MemoryRegion flash;
    MemoryRegion eeprom;
    MemoryRegion sram;
    DeviceState *io;
    AVRMaskState pwr[2];
    AVRUsartState usart[USART_MAX];
    AVRTimer16State timer[TIMER_MAX];
    uint64_t xtal_freq_hz;
} AtmegaState;

typedef struct AtmegaInfo AtmegaInfo;

typedef struct AtmegaClass {
    SysBusDeviceClass parent_class;
    const AtmegaInfo *info;
} AtmegaClass;

#define ATMEGA_CLASS(klass) \
    OBJECT_CLASS_CHECK(AtmegaClass, (klass), TYPE_ATMEGA)
#define ATMEGA_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AtmegaClass, (obj), TYPE_ATMEGA)

#endif /* HW_AVR_ATMEGA_H */
