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
static unsigned int tdk_read(struct qemu_phy *phy, unsigned int req)
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

static void tdk_write(struct qemu_phy *phy, unsigned int req, unsigned int data)
{
    int regnum;

    regnum = req & 0x1f;
    D(printf("%s reg[%d] = %x\n", __func__, regnum, data));
    switch (regnum) {
    default:
        phy->regs[regnum] = data;
        break;
    }
}

void tdk_init(struct qemu_phy *phy)
{
    phy->regs[PHY_CTRL] = 0x3100;
    /* PHY Id. */
    phy->regs[PHY_ID1] = 0x0300;
    phy->regs[PHY_ID2] = 0xe400;
    /* Autonegotiation advertisement reg. */
    phy->regs[PHY_AUTONEG_ADV] = 0x01e1;
    phy->link = 1;

    phy->read = tdk_read;
    phy->write = tdk_write;
}

void mdio_attach(struct qemu_mdio *bus, struct qemu_phy *phy, unsigned int addr)
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

void mdio_cycle(struct qemu_mdio *bus)
{
    bus->cnt++;

    D(printf("mdc=%d mdio=%d state=%d cnt=%d drv=%d\n",
             bus->mdc, bus->mdio, bus->state, bus->cnt, bus->drive));
    switch (bus->state) {
    case PREAMBLE:
        if (bus->mdc) {
            if (bus->cnt >= (32 * 2) && !bus->mdio) {
                bus->cnt = 0;
                bus->state = SOF;
                bus->data = 0;
            }
        }
        break;
    case SOF:
        if (bus->mdc) {
            if (bus->mdio != 1) {
                printf("WARNING: no SOF\n");
            }
            if (bus->cnt == 1 * 2) {
                bus->cnt = 0;
                bus->opc = 0;
                bus->state = OPC;
            }
        }
        break;
    case OPC:
        if (bus->mdc) {
            bus->opc <<= 1;
            bus->opc |= bus->mdio & 1;
            if (bus->cnt == 2 * 2) {
                bus->cnt = 0;
                bus->addr = 0;
                bus->state = ADDR;
            }
        }
        break;
    case ADDR:
        if (bus->mdc) {
            bus->addr <<= 1;
            bus->addr |= bus->mdio & 1;

            if (bus->cnt == 5 * 2) {
                bus->cnt = 0;
                bus->req = 0;
                bus->state = REQ;
            }
        }
        break;
    case REQ:
        if (bus->mdc) {
            bus->req <<= 1;
            bus->req |= bus->mdio & 1;
            if (bus->cnt == 5 * 2) {
                bus->cnt = 0;
                bus->state = TURNAROUND;
            }
        }
        break;
    case TURNAROUND:
        if (bus->mdc && bus->cnt == 2 * 2) {
            bus->mdio = 0;
            bus->cnt = 0;

            if (bus->opc == 2) {
                bus->drive = 1;
                bus->data = mdio_read_req(bus, bus->addr, bus->req);
                bus->mdio = bus->data & 1;
            }
            bus->state = DATA;
        }
        break;
    case DATA:
        if (!bus->mdc) {
            if (bus->drive) {
                bus->mdio = !!(bus->data & (1 << 15));
                bus->data <<= 1;
            }
        } else {
            if (!bus->drive) {
                bus->data <<= 1;
                bus->data |= bus->mdio;
            }
            if (bus->cnt == 16 * 2) {
                bus->cnt = 0;
                bus->state = PREAMBLE;
                if (!bus->drive) {
                    mdio_write_req(bus, bus->addr, bus->req, bus->data);
                }
                bus->drive = 0;
            }
        }
        break;
    default:
        break;
    }
}
