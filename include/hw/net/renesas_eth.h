/*
 *  Renesas ETHERC / EDMAC
 *
 *  Copyright 2019 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/net/mdio.h"
#include "hw/register.h"
#include "hw/clock.h"

#define TYPE_RENESAS_ETH "renesas_eth"
#define RenesasEth(obj) OBJECT_CHECK(RenesasEthState, (obj), TYPE_RENESAS_ETH)

#define RENESAS_ETHERC_R_MAX (0x100 / 4)
#define RENESAS_EDMAC_R_MAX  (0x100 / 4)

typedef struct RenesasEthState {
    SysBusDevice parent_obj;

    NICState *nic;
    NICConf conf;
    MemoryRegion etherc_mem;
    MemoryRegion edmac_mem;
    qemu_irq irq;

    /* ETHERC registers */
    RegisterInfo etherc_regs_info[RENESAS_ETHERC_R_MAX];
    uint32_t etherc_regs[RENESAS_ETHERC_R_MAX];

    /* EDMAC register */
    RegisterInfo edmac_regs_info[RENESAS_EDMAC_R_MAX];
    uint32_t edmac_regs[RENESAS_EDMAC_R_MAX];

    int descsize;
    int rcv_bcast;
    uint8_t macadr[6];
    int link_sta;
    MDIOState *mdiodev;
    Clock *ick;
} RenesasEthState;
