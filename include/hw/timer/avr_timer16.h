/*
 * AVR 16 bit timer
 *
 * Copyright (c) 2018 University of Kent
 * Author: Ed Robbins
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Driver for 16 bit timers on 8 bit AVR devices.
 * Note:
 * On ATmega640/V-1280/V-1281/V-2560/V-2561/V timers 1, 3, 4 and 5 are 16 bit
 */

#ifndef AVR_TIMER16_H
#define AVR_TIMER16_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/hw.h"

enum NextInterrupt {
    OVERFLOW,
    COMPA,
    COMPB,
    COMPC,
    CAPT
};

#define TYPE_AVR_TIMER16 "avr-timer16"
#define AVR_TIMER16(obj) \
    OBJECT_CHECK(AVRTimer16State, (obj), TYPE_AVR_TIMER16)

typedef struct AVRTimer16State {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    MemoryRegion imsk_iomem;
    MemoryRegion ifr_iomem;
    QEMUTimer *timer;
    qemu_irq capt_irq;
    qemu_irq compa_irq;
    qemu_irq compb_irq;
    qemu_irq compc_irq;
    qemu_irq ovf_irq;

    bool enabled;

    /* registers */
    uint8_t cra;
    uint8_t crb;
    uint8_t crc;
    uint8_t cntl;
    uint8_t cnth;
    uint8_t icrl;
    uint8_t icrh;
    uint8_t ocral;
    uint8_t ocrah;
    uint8_t ocrbl;
    uint8_t ocrbh;
    uint8_t ocrcl;
    uint8_t ocrch;
    /*
     * Reads and writes to CNT and ICR utilise a bizarre temporary
     * register, which we emulate
     */
    uint8_t rtmp;
    uint8_t imsk;
    uint8_t ifr;

    uint64_t cpu_freq_hz;
    uint64_t freq_hz;
    uint64_t period_ns;
    uint64_t reset_time_ns;
    enum NextInterrupt next_interrupt;
} AVRTimer16State;

#endif /* AVR_TIMER16_H */
