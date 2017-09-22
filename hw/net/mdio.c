/*
 * QEMU Ethernet MDIO bus & PHY models
 *
 * Copyright (c) 2008 Edgar E. Iglesias, Axis Communications AB.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This is a generic MDIO implementation.
 *
 * TODO:
 * - Split PHYs out as separate device models so they can be defined and
 *   instantiated separately from the MDIO bus.
 * - Split out bitbang state machine into a separate model. Mostly this consists
 *   of the mdio_cycle() routine and the bitbang state data in struct qemu_mdio
 * - Use the GPIO interface for driving bitbang
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "hw/net/mdio.h"

#define D(x)

/*
 * The MDIO extensions in the TDK PHY model were reversed engineered from the
 * linux driver (PHYID and Diagnostics reg).
 * TODO: Add friendly names for the register nums.
 */
static uint16_t mdio_phy_read(struct qemu_phy *phy, unsigned int req)
{
    int regnum;
    unsigned r = 0;

    regnum = req & 0x1f;

    switch (regnum) {
    case 1:
        if (!phy->link) {
            break;
        }
        /* MR1.     */
        /* Speeds and modes.  */
        r |= (1 << 13) | (1 << 14);
        r |= (1 << 11) | (1 << 12);
        r |= (1 << 5); /* Autoneg complete.  */
        r |= (1 << 3); /* Autoneg able.     */
        r |= (1 << 2); /* link.     */
        r |= (1 << 1); /* link.     */
        break;
    case 5:
        /* Link partner ability.
           We are kind; always agree with whatever best mode
           the guest advertises.  */
        r = 1 << 14; /* Success.  */
        /* Copy advertised modes.  */
        r |= phy->regs[4] & (15 << 5);
        /* Autoneg support.  */
        r |= 1;
        break;
    case 17:
        /* Marvel PHY on many xilinx boards. */
        r = 0x8000; /* 1000Mb */
        break;
    case 18:
    {
        /* Diagnostics reg.  */
        int duplex = 0;
        int speed_100 = 0;

        if (!phy->link) {
            break;
        }

        /* Are we advertising 100 half or 100 duplex ? */
        speed_100 = !!(phy->regs[4] & PHY_ADVERTISE_100HALF);
        speed_100 |= !!(phy->regs[4] & PHY_ADVERTISE_100FULL);

        /* Are we advertising 10 duplex or 100 duplex ? */
        duplex = !!(phy->regs[4] & PHY_ADVERTISE_100FULL);
        duplex |= !!(phy->regs[4] & PHY_ADVERTISE_10FULL);
        r = (speed_100 << 10) | (duplex << 11);
    }
    break;

    default:
        r = phy->regs[regnum];
        break;
    }
    D(printf("\n%s %x = reg[%d]\n", __func__, r, regnum));
    return r;
}

static void mdio_phy_write(struct qemu_phy *phy, unsigned int req,
                           uint16_t data)
{
    int regnum = req & 0x1f;
    uint16_t mask = phy->regs_readonly_mask[regnum];

    D(printf("%s reg[%d] = %x; mask=%x\n", __func__, regnum, data, mask));
    switch (regnum) {
    default:
        phy->regs[regnum] = (phy->regs[regnum] & mask) | (data & ~mask);
        break;
    }
}

static const uint16_t default_readonly_mask[32] = {
    [PHY_CTRL] = PHY_CTRL_RST | PHY_CTRL_ANEG_RST,
    [PHY_ID1] = 0xffff,
    [PHY_ID2] = 0xffff,
    [PHY_LP_ABILITY] = 0xffff,
};

void mdio_phy_init(struct qemu_phy *phy, uint16_t id1, uint16_t id2)
{
    phy->regs[PHY_CTRL] = 0x3100;
    /* PHY Id. */
    phy->regs[PHY_ID1] = id1;
    phy->regs[PHY_ID2] = id2;
    /* Autonegotiation advertisement reg. */
    phy->regs[PHY_AUTONEG_ADV] = 0x01e1;
    phy->regs_readonly_mask = default_readonly_mask;
    phy->link = true;

    phy->read = mdio_phy_read;
    phy->write = mdio_phy_write;
}

void mdio_attach(struct qemu_mdio *bus, struct qemu_phy *phy,
                 unsigned int addr)
{
    bus->devs[addr & 0x1f] = phy;
}

uint16_t mdio_read_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req)
{
    struct qemu_phy *phy;

    phy = bus->devs[bus->addr];
    if (phy && phy->read) {
        return phy->read(phy, req);
    }
    return 0xffff;
}

void mdio_write_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req,
                    uint16_t data)
{
    struct qemu_phy *phy;

    phy = bus->devs[bus->addr];
    if (phy && phy->write) {
        phy->write(phy, req, data);
    }
}

/**
 * mdio_bitbang_update() - internal function to check how many clocks have
 * passed and move to the next state if necessary. Returns TRUE on state change.
 */
static bool mdio_bitbang_update(struct qemu_mdio *bus, int num_bits, int next,
                                uint16_t *reg)
{
    if (bus->cnt < num_bits) {
        return false;
    }
    if (reg) {
        *reg = bus->shiftreg;
    }
    bus->state = next;
    bus->cnt = 0;
    bus->shiftreg = 0;
    return true;
}

/**
 * mdio_bitbang_set_clk() - set value of mdc signal and update state
 */
void mdio_bitbang_set_clk(struct qemu_mdio *bus, bool mdc)
{
    uint16_t tmp;

    if (mdc == bus->mdc) {
        return; /* Clock state hasn't changed; do nothing */
    }

    bus->mdc = mdc;
    if (bus->mdc) {
        /* Falling (inactive) clock edge */
        if ((bus->state == DATA) && (bus->opc == 2)) {
            bus->mdio = !!(bus->shiftreg & 0x8000);
        }
        return;
    }

    /* Rising clock Edge */
    bus->shiftreg = (bus->shiftreg << 1) | bus->mdio;
    bus->cnt++;
    D(printf("mdc=%d mdio=%d state=%d cnt=%d drv=%d\n",
             bus->mdc, bus->mdio, bus->state, bus->cnt));
    switch (bus->state) {
    case PREAMBLE:
        /* MDIO must be 30 clocks high, 1 low, and 1 high to get out of
           preamble */
        if (bus->shiftreg == 0xfffffffd) {
            mdio_bitbang_update(bus, 0, OPC, NULL);
        }
        break;
    case OPC:
        mdio_bitbang_update(bus, 2, ADDR, &bus->opc);
        break;
    case ADDR:
        mdio_bitbang_update(bus, 5, REQ, &bus->addr);
        break;
    case REQ:
        mdio_bitbang_update(bus, 5, TURNAROUND, &bus->req);
        break;
    case TURNAROUND:
        /* If beginning of DATA READ cycle, then read PHY into shift register */
        if (mdio_bitbang_update(bus, 2, DATA, NULL) && (bus->opc == 2)) {
            bus->shiftreg = mdio_read_req(bus, bus->addr, bus->req);
        }
        break;
    case DATA:
        /* If end of DATA WRITE cycle, then write shift register to PHY */
        if (mdio_bitbang_update(bus, 16, PREAMBLE, &tmp) && (bus->opc == 1)) {
            mdio_write_req(bus, bus->addr, bus->req, tmp);
        }
        break;
    default:
        break;
    }
}

const VMStateDescription vmstate_mdio = {
    .name = "mdio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(mdc, struct qemu_mdio),
        VMSTATE_BOOL(mdio, struct qemu_mdio),
        VMSTATE_UINT32(state, struct qemu_mdio),
        VMSTATE_UINT16(cnt, struct qemu_mdio),
        VMSTATE_UINT16(addr, struct qemu_mdio),
        VMSTATE_UINT16(opc, struct qemu_mdio),
        VMSTATE_UINT16(req, struct qemu_mdio),
        VMSTATE_UINT32(shiftreg, struct qemu_mdio),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_mdio_phy = {
    .name = "mdio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_ARRAY(regs, struct qemu_phy, 32),
        VMSTATE_BOOL(link, struct qemu_phy),
        VMSTATE_END_OF_LIST()
    }
};
