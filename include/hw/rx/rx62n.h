/*
 * RX62N MCU Object
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef HW_RX_RX62N_MCU_H
#define HW_RX_RX62N_MCU_H

#include "target/rx/cpu.h"
#include "hw/intc/rx_icu.h"
#include "hw/timer/renesas_tmr8.h"
#include "hw/timer/renesas_timer.h"
#include "hw/char/renesas_sci.h"
#include "hw/rx/rx62n-cpg.h"
#include "qemu/units.h"

#define TYPE_RX62N_MCU "rx62n-mcu"
#define RX62N_MCU(obj) OBJECT_CHECK(RX62NState, (obj), TYPE_RX62N_MCU)

#define TYPE_R5F562N7_MCU "r5f562n7-mcu"
#define TYPE_R5F562N8_MCU "r5f562n8-mcu"

#define EXT_CS_BASE         0x01000000
#define VECTOR_TABLE_BASE   0xffffff80
#define RX62N_CFLASH_BASE   0xfff80000

#define RX62N_NR_TMR    2
#define RX62N_NR_CMT    2
#define RX62N_NR_SCI    6

typedef struct RX62NClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    uint64_t ram_size;
    uint64_t rom_flash_size;
    uint64_t data_flash_size;
} RX62NClass;

#define RX62N_MCU_CLASS(klass) \
    OBJECT_CLASS_CHECK(RX62NClass, (klass), TYPE_RX62N_MCU)
#define RX62N_MCU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RX62NClass, (obj), TYPE_RX62N_MCU)

typedef struct RX62NState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    RXCPU cpu;
    RXICUState icu;
    RenesasTMR8State tmr[RX62N_NR_TMR];
    RenesasCMTState cmt[RX62N_NR_CMT];
    RSCIAState sci[RX62N_NR_SCI];
    RX62NCPGState cpg;

    MemoryRegion *sysmem;

    MemoryRegion iram;
    MemoryRegion iomem1;
    MemoryRegion d_flash;
    MemoryRegion iomem2;
    MemoryRegion iomem3;
    MemoryRegion c_flash;
    qemu_irq irq[NR_IRQS];

    /* Input Clock (XTAL) frequency */
    uint32_t xtal_freq_hz;
} RX62NState;

#endif
