/*
 *  Bit-bang MII emulation
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

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/net/mdio.h"

void mdio_phy_set_link(PHYState *s, bool ok)
{
    if (ok) {
        s->regs[MII_BMSR] |= MII_BMSR_LINK_ST;
        s->regs[MII_ANLPAR] |= MDIO_ANLPAR_LINK;
    } else {
        s->regs[MII_BMSR] &= ~(MII_BMSR_LINK_ST | MII_BMSR_AUTONEG);
        s->regs[MII_ANLPAR] &= MDIO_ANLPAR_LINK;
    }
    s->link_ok = ok;
}

static void mdio_phy_reset(PHYState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[MII_BMSR] = s->bmsr;
    s->regs[MII_ANLPAR] = s->anlpar;
    s->regs[MII_PHYID1] = extract32(s->identifier, 16, 16);
    s->regs[MII_PHYID2] = extract32(s->identifier, 0, 16);
    mdio_phy_set_link(s, s->link_ok);
}

uint16_t mdio_phy_read(PHYState *s, int addr)
{
    if (addr >= 0 && addr < 32) {
        return s->regs[addr];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mdio: Register %04x invalid address.\n", addr);
        return 0;
    }
}

int mdio_phy_linksta(PHYState *s)
{
    return s->link_ok ^ s->link_out_pol;
}

void mdio_phy_write(PHYState *s, int addr, uint16_t val)
{
    switch (addr) {
    case MII_BMCR:
        s->regs[MII_BMCR] = val & 0xfd80;
        if (val & MII_BMCR_RESET) {
            mdio_phy_reset(s);
        }
        break;
    case MII_BMSR:
    case MII_ANLPAR:
        /* Read only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mdio: Register %04x is read only register.\n", addr);
        break;
    case MII_PHYID1:
    case MII_PHYID2:
        s->regs[addr] = val;
        break;
    case MII_ANAR:
        s->regs[addr] = val & 0x2dff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "mdio: Register %04x not implemented\n", addr);
        break;
    }
}

static Property phy_properties[] = {
    DEFINE_PROP_UINT32("phy-id", PHYState, identifier, 0),
    DEFINE_PROP_UINT32("link-out-pol", PHYState, link_out_pol, 0),
    DEFINE_PROP_UINT16("bmsr", PHYState, bmsr, 0),
    DEFINE_PROP_UINT16("anlpar", PHYState, anlpar, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void phy_realize(DeviceState *dev, Error **errp)
{
    PHYState *s = EtherPHY(dev);
    mdio_phy_reset(s);
}

static void phy_class_init(ObjectClass *klass, void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, phy_properties);
    dc->realize = phy_realize;
}

/* shift in MDO */
static void read_mdo(MDIOState *s)
{
    int op;
    s->bits++;
    switch (s->bb_state) {
    case BB_PRE: /* preamble */
        if (s->mdo_pin == 0) {
            /* ST 1st bit found */
            s->bb_state = BB_ST;
        }
        break;
    case BB_ST: /* ST 2nd bit */
        if (s->mdo_pin == 0) {
            s->bb_state = BB_CMD;
            s->cmd = 0;
            s->bits = 2;
            s->selphy = -1;
            s->regad = -1;
        } else {
            s->bb_state = BB_PRE;
        }
        break;
    case BB_CMD:
        s->cmd <<= 1;
        s->cmd |= (s->mdo_pin & 1);
        if (s->bits == 14) {
            op = extract32(s->cmd, 10, 2);
            s->selphy = extract32(s->cmd, 5, 5);
            s->regad = extract32(s->cmd, 0, 5);
            switch (op) {
            case 0x02: /* READ */
                s->bb_state = BB_TA_R;
                break;
            case 0x01: /* WRITE */
                s->bb_state = BB_TA_W;
                break;
            default:
                s->bb_state = BB_INH;
                break;
            }
        }
        break;
    case BB_TA_R:
        s->mdi_pin = 0;
        if (s->bits == 16) {
            if (s->phyad == s->selphy) {
                s->data = mdio_phy_read(s->phy, s->regad);
                s->bb_state = BB_DATA_R;
            } else {
                s->bb_state = BB_INH;
            }
        }
        break;
    case BB_TA_W:
        if (s->bits == 16) {
            s->bb_state = BB_DATA_W;
        }
        break;
    case BB_DATA_W:
        s->data <<= 1;
        s->data |= (s->mdo_pin & 1);
        if (s->bits == 32) {
            if (s->phyad == s->selphy) {
                mdio_phy_write(s->phy, s->regad, s->data);
            }
            s->bb_state = BB_PRE;
        }
        break;
    case BB_INH:
    case BB_DATA_R:
        if (s->bits == 32) {
            s->bb_state = BB_PRE;
        }
        break;
    }
}

/* shift out MDI */
static void write_mdi(MDIOState *s)
{
    switch (s->bb_state) {
    case BB_DATA_R:
        s->mdi_pin = (s->data >> 15) & 1;
        s->data <<= 1;
        break;
    case BB_TA_R:
        s->mdi_pin = 0;
        break;
    default:
        s->mdi_pin = mdio_z;
        break;
    }
}

/* MDIO pin operation */
void mdio_set_mdc_pin(MDIOState *s, int clk)
{
    if (s->pclk ^ (clk & 1)) {
        s->pclk = (clk & 1);
        if (s->pclk == 1) {
            /* rising edge */
            read_mdo(s);
        } else {
            /* faling edge */
            write_mdi(s);
        }
    }
}

static Property bb_properties[] = {
    DEFINE_PROP_INT32("address", MDIOState, phyad, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void bb_init(Object *obj)
{
    MDIOState *s = MDIO_BB(obj);

    object_property_add_link(obj, "phy",
                             TYPE_ETHER_PHY,
                             (Object **)&s->phy,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void bb_class_init(ObjectClass *klass, void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, bb_properties);
}

static const TypeInfo phy_types_info[] = {
    {
        .name = TYPE_ETHER_PHY,
        .parent = TYPE_DEVICE,
        .class_init = phy_class_init,
        .instance_size = sizeof(PHYState),
    },
    {
        .name = TYPE_ETHER_MDIO_BB,
        .parent = TYPE_DEVICE,
        .class_init = bb_class_init,
        .instance_size = sizeof(MDIOState),
        .instance_init = bb_init,
    },
};

DEFINE_TYPES(phy_types_info);
