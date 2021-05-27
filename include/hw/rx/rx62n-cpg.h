/*
 * RX62N Clock generator circuit
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2020 Yoshinori Sato
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

#ifndef HW_RX_RX62N_CPG_H
#define HW_RX_RX62N_CPG_H

#include "hw/sysbus.h"
#include "hw/qdev-clock.h"

#define TYPE_RX62N_CPG "rx62n-cpg"
#define RX62NCPG(obj) OBJECT_CHECK(RX62NCPGState, (obj), TYPE_RX62N_CPG)

enum {
    CK_TMR8_1,
    CK_TMR8_0,
    CK_MTU_1,
    CK_MTU_0,
    CK_CMT_1,
    CK_CMT_0,
    CK_EDMAC,
    CK_SCI6,
    CK_SCI5,
    CK_SCI3,
    CK_SCI2,
    CK_SCI1,
    CK_SCI0,
    NUM_SUBCLOCK,
};

typedef struct RX62NCPGState {
    SysBusDevice parent_obj;
    uint32_t mstpcr[3];
    uint32_t sckcr;
    uint8_t  bckcr;
    uint8_t  ostdcr;

    int ick;
    Clock *clk_ick;
    int bck;
    Clock *clk_bck;
    int pck;
    Clock *clk_pck;
    Clock *dev_clocks[NUM_SUBCLOCK];
    uint32_t xtal_freq_hz;
    MemoryRegion memory;
} RX62NCPGState;

typedef struct RX62NCPGClass {
    SysBusDeviceClass parent;
} RX62NCPGClass;

#define OSTDCR_KEY 0xac

#endif
