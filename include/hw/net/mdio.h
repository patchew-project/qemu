#ifndef BITBANG_MDIO_H
#define BITBANG_MDIO_H

/*
 * QEMU Bitbang Ethernet MDIO bus & PHY controllers.
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
 */

/* PHY MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CTRL         0x00 /* Control Register */
#define PHY_STATUS       0x01 /* Status Regiser */
#define PHY_ID1          0x02 /* Phy Id Reg (word 1) */
#define PHY_ID2          0x03 /* Phy Id Reg (word 2) */
#define PHY_AUTONEG_ADV  0x04 /* Autoneg Advertisement */
#define PHY_LP_ABILITY   0x05 /* Link Partner Ability (Base Page) */
#define PHY_AUTONEG_EXP  0x06 /* Autoneg Expansion Reg */
#define PHY_NEXT_PAGE_TX 0x07 /* Next Page TX */
#define PHY_LP_NEXT_PAGE 0x08 /* Link Partner Next Page */
#define PHY_1000T_CTRL   0x09 /* 1000Base-T Control Reg */
#define PHY_1000T_STATUS 0x0A /* 1000Base-T Status Reg */
#define PHY_EXT_STATUS   0x0F /* Extended Status Reg */

#define NUM_PHY_REGS     0x20  /* 5 bit address bus (0-0x1F) */

#define PHY_CTRL_RST            0x8000 /* PHY reset command */
#define PHY_CTRL_ANEG_RST       0x0200 /* Autonegotiation reset command */

/* PHY Advertisement control and remote capability registers (same bitfields) */
#define PHY_ADVERTISE_10HALF    0x0020  /* Try for 10mbps half-duplex  */
#define PHY_ADVERTISE_10FULL    0x0040  /* Try for 10mbps full-duplex  */
#define PHY_ADVERTISE_100HALF   0x0080  /* Try for 100mbps half-duplex */
#define PHY_ADVERTISE_100FULL   0x0100  /* Try for 100mbps full-duplex */

struct qemu_phy {
    uint16_t regs[NUM_PHY_REGS];
    const uint16_t *regs_readonly_mask; /* 0=writable, 1=read-only */

    bool link;

    uint16_t (*read)(struct qemu_phy *phy, unsigned int req);
    void (*write)(struct qemu_phy *phy, unsigned int req, uint16_t data);
};

struct qemu_mdio {
    /* bitbanging state machine */
    bool mdc;
    bool mdio;
    enum {
        PREAMBLE,
        OPC,
        ADDR,
        REQ,
        TURNAROUND,
        DATA
    } state;

    uint16_t cnt; /* Bit count for current state */
    uint16_t addr; /* PHY Address; retrieved during ADDR state */
    uint16_t opc; /* Operation; 2:read */
    uint16_t req; /* Register address */
    uint32_t shiftreg; /* shift register; bits in to or out from PHY */

    struct qemu_phy *devs[32];
};

void mdio_phy_init(struct qemu_phy *phy, uint16_t id1, uint16_t id2);
void mdio_attach(struct qemu_mdio *bus, struct qemu_phy *phy,
                 unsigned int addr);
uint16_t mdio_read_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req);
void mdio_write_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req,
                    uint16_t data);
void mdio_bitbang_set_clk(struct qemu_mdio *bus, bool mdc);
static inline void mdio_bitbang_set_data(struct qemu_mdio *bus, bool mdio)
{
    bus->mdio = mdio;
}
static inline bool mdio_bitbang_get_data(struct qemu_mdio *bus)
{
    return bus->mdio;
}

#endif
