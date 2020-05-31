/*
 * RX62N MCU Object
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
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

#ifndef HW_RX_RX62N_H
#define HW_RX_RX62N_H

#include "hw/sysbus.h"
#include "hw/intc/rx_icu.h"
#include "hw/timer/renesas_8timer.h"
#include "hw/timer/renesas_timer.h"
#include "hw/char/renesas_sci.h"
#include "target/rx/cpu.h"
#include "qemu/units.h"

#define TYPE_RX62N "rx62n"
#define RX62N(obj) OBJECT_CHECK(RX62NState, (obj), TYPE_RX62N)

#define RX62N_NR_TMR    2
#define RX62N_NR_CMT    2
#define RX62N_NR_SCI    6

typedef struct RX62NState {
    SysBusDevice parent_obj;

    RXCPU cpu;
    RXICUState icu;
    RTMRState tmr[RX62N_NR_TMR];
    RTIMERState cmt[RX62N_NR_CMT];
    RSCIState sci[RX62N_NR_SCI];

    MemoryRegion *sysmem;
    bool kernel;

    MemoryRegion iram;
    MemoryRegion iomem1;
    MemoryRegion d_flash;
    MemoryRegion iomem2;
    MemoryRegion iomem3;
    MemoryRegion c_flash;
    qemu_irq irq[NR_IRQS];
} RX62NState;

/*
 * RX62N Peripheral Address
 * See users manual section 5
 */
#define RX62N_ICUBASE 0x00087000
#define RX62N_TMRBASE 0x00088200
#define RX62N_CMTBASE 0x00088000
#define RX62N_SCIBASE 0x00088240

/*
 * RX62N Peripheral IRQ
 * See users manual section 11
 */
#define RX62N_TMR_IRQBASE 174
#define RX62N_CMT_IRQBASE 28
#define RX62N_SCI_IRQBASE 214

/*
 * RX62N Internal Memory
 * It is the value of R5F562N8.
 * Please change the size for R5F562N7.
 */
#define RX62N_IRAM_BASE 0x00000000
#define RX62N_IRAM_SIZE (96 * KiB)
#define RX62N_DFLASH_BASE 0x00100000
#define RX62N_DFLASH_SIZE (32 * KiB)
#define RX62N_CFLASH_BASE 0xfff80000
#define RX62N_CFLASH_SIZE (512 * KiB)

#define RX62N_PCLK (48 * 1000 * 1000)
#endif
