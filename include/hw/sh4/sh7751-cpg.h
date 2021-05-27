/*
 * SH7751(R) Clock generator circuit
 *
 * Datasheet: SH7751 Group, SH7751R Group User's Manual: Hardware
 *            (Rev.4.01 R01UH0457EJ0401)
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

#ifndef HW_SH4_SH7751_CPG_H
#define HW_SH4_SH7751_CPG_H

#include "hw/sysbus.h"
#include "hw/qdev-clock.h"

#define TYPE_SH7751_CPG_BASE "sh7751-cpg-base"
#define SH7751CPGBase(obj) \
    OBJECT_CHECK(SH7751CPGBaseState, (obj), TYPE_SH7751_CPG_BASE)
#define TYPE_SH7751_CPG "sh7751-cpg"
#define SH7751CPG(obj) OBJECT_CHECK(SH7751CPGState, (obj), TYPE_SH7751_CPG)
#define TYPE_SH7751R_CPG "sh7751r-cpg"
#define SH7751RCPG(obj) OBJECT_CHECK(SH7751RCPGState, (obj), TYPE_SH7751R_CPG)
#define SH7751CPG_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SH7751CPGBaseClass, obj, TYPE_SH7751_CPG_BASE)
#define SH7751CPGBaseClass(klass) \
    OBJECT_CLASS_CHECK(SH7751CPGBaseClass, klass, TYPE_SH7751_CPG_BASE)

enum {
    CK_DMAC,
    CK_SCIF,
    CK_TMU_0,
    CK_RTC,
    CK_SCI,
    CK_SQ,
    CK_UBC,
    CK_PCIC,
    CK_TMU_1,
    CK_INTC,
    NUM_SUBCLOCK,
};

typedef struct SH7751CPGBaseState {
    SysBusDevice parent_obj;
    uint8_t stbcr[2];
    uint32_t clkstp00;
    uint16_t freqcr;

    uint32_t clock_mode;
    int ick;
    Clock *clk_ick;
    int bck;
    Clock *clk_bck;
    int pck;
    Clock *clk_pck;
    Clock *dev_clocks[NUM_SUBCLOCK];
    uint32_t xtal_freq_hz;
    MemoryRegion memory[3 * 2];
} SH7751CPGBaseState;

typedef struct {
    SH7751CPGBaseState parent_obj;
} SH7751CPGState;

typedef struct {
    SH7751CPGBaseState parent_obj;
} SH7751RCPGState;

typedef struct {
    SysBusDeviceClass parent;
    int (*pll1mul)(int mode, uint16_t freqcr);
    uint16_t *initfreqcr;
} SH7751CPGBaseClass;

typedef struct {
    SH7751CPGBaseClass parent;
} SH7751CPGClass;

typedef struct {
    SH7751CPGBaseClass parent;
} SH7751RCPGClass;

#endif
