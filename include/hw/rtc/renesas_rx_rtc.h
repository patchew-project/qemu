/*
 * Renesas RX Realtime Clock (RTC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 26 (RTC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_RTC_RENESAS_RX_RTC_H
#define HW_RTC_RENESAS_RX_RTC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_RX_RTC "renesas-rx-rtc"
typedef struct RenesasRxRtcState RenesasRxRtcState;
DECLARE_INSTANCE_CHECKER(RenesasRxRtcState, RENESAS_RX_RTC, TYPE_RENESAS_RX_RTC)

#define RX_RTC_REGS_SIZE    0x40

struct RenesasRxRtcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mr;
    QEMUTimer *timer;

    /* Current time held as binary fields (converted to/from BCD on access). */
    int sec;
    int min;
    int hour;
    int wday;       /* 0 = Sunday */
    int mday;       /* 1..31 */
    int mon;        /* 1..12 */
    int year;       /* full year, e.g. 2024 */

    /* Alarm match registers (raw BCD as written). */
    uint8_t secar, minar, hrar, wkar, dayar, monar;
    uint16_t yrar;

    uint8_t rcr1;
    uint8_t rcr2;
    uint8_t rcr3;
    uint8_t rcr4;
};

#endif /* HW_RTC_RENESAS_RX_RTC_H */
