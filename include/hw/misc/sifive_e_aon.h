/*
 * SiFive HiFive1 AON (Always On Domain) interface.
 *
 * Copyright (c) 2022 SiFive, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_AON_H
#define HW_SIFIVE_AON_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SIFIVE_E_AON "riscv.sifive.e.aon"
OBJECT_DECLARE_SIMPLE_TYPE(SiFiveEAONState, SIFIVE_E_AON)

#define SIFIVE_E_AON_WDOGKEY (0x51F15E)
#define SIFIVE_E_AON_WDOGFEED (0xD09F00D)
#define SIFIVE_E_LFCLK_DEFAULT_FREQ (32768)

enum {
    SIFIVE_E_AON_WDT_WDOGCFG        = 0,
    SIFIVE_E_AON_WDT_WDOGCOUNT      = 0x8,
    SIFIVE_E_AON_WDT_WDOGS          = 0x10,
    SIFIVE_E_AON_WDT_WDOGFEED       = 0x18,
    SIFIVE_E_AON_WDT_WDOGKEY        = 0x1c,
    SIFIVE_E_AON_WDT_WDOGCMP0       = 0x20,
    SIFIVE_E_AON_RTC_RTCCFG         = 0x40,
    SIFIVE_E_AON_LFROSC_LFROSCCFG   = 0x70,
    SIFIVE_E_AON_BACKUP_BACKUP0     = 0x80,
    SIFIVE_E_AON_PMU_PMUWAKEUP0     = 0x100,
    SIFIVE_E_AON_MAX                = 0x150
};

typedef struct wdogcfg_s wdogcfg_s;
struct wdogcfg_s {
    union {
        uint32_t value;
        struct {
            uint32_t wdogscale:4;
            uint32_t reserved:4;
            uint8_t  wdogrsten:1;
            uint8_t  wdogzerocmp:1;
            uint8_t  reserved2:2;
            uint8_t  wdogenalways:1;
            uint8_t  wdogencoreawake:1;
            uint32_t reserved3:14;
            uint8_t  wdogip0:1;
            uint8_t  reserved4:3;
        };
    };
};

struct SiFiveEAONState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /*< watchdog timer >*/
    QEMUTimer *wdog_timer;
    qemu_irq wdog_irq;
    uint64_t wdog_restart_time;
    uint64_t wdogclk_freq;

    wdogcfg_s wdogcfg;
    uint16_t wdogcmp0;
    uint32_t wdogcount;
    uint8_t wdogunlock;
};

SiFiveEAONState *sifive_e_aon_create(MemoryRegion *mr, hwaddr base,
                                     qemu_irq irq);

#endif
