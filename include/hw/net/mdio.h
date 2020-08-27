/*
 *  MDIO PHY emulation
 *
 *  Copyright 2020 Yoshinori Sato <ysato@users.sourceforge.jp>
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

#ifndef MDIO_H
#define MDIO_H

#include "hw/qdev-core.h"
#include "hw/net/mii.h"

typedef enum mdio_pin {
    mdio_z = -1,
    mdio_l = 0,
    mdio_h = 1,
} MDIOPin;

#define TYPE_ETHER_PHY "ether-phy"
#define TYPE_ETHER_PHY_CLASS(obj) \
    OBJECT_GET_CLASS(EtherPHYClass, (obj), TYPE_ETHER_PHY)
#define EtherPHYClass(klass) \
    OBJECT_CHECK_CLASS(EtherPHYClass, (klass), TYPE_ETHER_PHY)
#define EtherPHY(obj) \
    OBJECT_CHECK(PHYState, (obj), TYPE_ETHER_PHY)

#define TYPE_ETHER_MDIO_BB "ether-mdio-bb"
#define TYPE_ETHER_MDIO_BB_CLASS(obj)                           \
    OBJECT_GET_CLASS(MDIO_BBClass, (obj), TYPE_ETHER_MDIO_BB)
#define MDIO_BBClass(klass) \
    OBJECT_CHECK_CLASS(MDIO_BBClass, (klass), TYPE_ETHER_MDIO_BB)
#define MDIO_BB(obj) \
    OBJECT_CHECK(MDIOState, (obj), TYPE_ETHER_MDIO_BB)

typedef enum {
    phy_out_p = 0,    /* Link up is 'H' */
    phy_out_n = 1,    /* Link up is 'L' */
} phy_output_polarity;

typedef struct {
    DeviceState parent;

    uint16_t regs[32];
    uint32_t identifier;
    bool link_ok;
    phy_output_polarity link_out_pol;
    uint16_t bmsr;
    uint16_t anlpar;
} PHYState;

#define MDIO_ANLPAR_LINK \
    (MII_ANLPAR_TXFD | MII_ANLPAR_TX | MII_ANLPAR_10FD | MII_ANLPAR_10 | \
     MII_ANLPAR_CSMACD)

typedef enum {
    BB_PRE,
    BB_ST,
    BB_CMD,
    BB_TA_R,
    BB_TA_W,
    BB_DATA_R,
    BB_DATA_W,
    BB_INH,
} mdio_bb_state;

typedef struct {
    DeviceState parent;

    PHYState *phy;
    mdio_bb_state bb_state;
    int pclk;
    int bits;
    int cmd;
    int phyad;
    int selphy;
    int regad;
    int data;
    int mdi_pin;
    int mdo_pin;
} MDIOState;

#define mdio_get_phy(s) (s->phy)

typedef struct {
    DeviceClass parent;
} EtherPHYClass;

typedef struct {
    DeviceClass parent;
} MDIO_BBClass;

/* Generic PHY interface */
void mdio_phy_set_link(PHYState *s, bool ok);
int mdio_phy_linksta(PHYState *s);
uint16_t mdio_phy_read(PHYState *s, int addr);
void mdio_phy_write(PHYState *s, int addr, uint16_t val);

/* Bit-bang MDIO operation */
static inline MDIOPin mdio_read_mdi_pin(MDIOState *s)
{
    return s->mdi_pin;
}

static inline void mdio_set_mdo_pin(MDIOState *s, MDIOPin mdo)
{
    s->mdo_pin = mdo;
}

void mdio_set_mdc_pin(MDIOState *s, int clk);

#endif
