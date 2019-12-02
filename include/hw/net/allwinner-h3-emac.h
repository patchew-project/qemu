/*
 * Allwinner H3 EMAC emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALLWINNER_H3_EMAC_H
#define ALLWINNER_H3_EMAC_H

#include "qemu/units.h"
#include "net/net.h"
#include "qemu/fifo8.h"
#include "hw/net/mii.h"
#include "hw/sysbus.h"

#define AW_H3_EMAC_REGS_MEM_SIZE  (1024)

#define TYPE_AW_H3_EMAC "allwinner-h3-emac"
#define AW_H3_EMAC(obj) OBJECT_CHECK(AwH3EmacState, (obj), TYPE_AW_H3_EMAC)

typedef struct AwH3EmacState {
    /*< private >*/
    SysBusDevice  parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq     irq;
    NICState     *nic;
    NICConf      conf;

    uint8_t      mii_phy_addr;
    uint32_t     mii_cmd;
    uint32_t     mii_data;
    uint32_t     mii_cr;
    uint32_t     mii_st;

    uint32_t     basic_ctl0;
    uint32_t     basic_ctl1;
    uint32_t     int_en;
    uint32_t     int_sta;
    uint32_t     frm_flt;

    uint32_t     rx_ctl0;
    uint32_t     rx_ctl1;
    uint32_t     rx_desc_head;
    uint32_t     rx_desc_curr;

    uint32_t     tx_ctl0;
    uint32_t     tx_ctl1;
    uint32_t     tx_desc_head;
    uint32_t     tx_desc_curr;
    uint32_t     tx_flowctl;

} AwH3EmacState;

#endif
