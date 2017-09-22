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

/* PHY Advertisement control register */
#define PHY_ADVERTISE_10HALF    0x0020  /* Try for 10mbps half-duplex  */
#define PHY_ADVERTISE_10FULL    0x0040  /* Try for 10mbps full-duplex  */
#define PHY_ADVERTISE_100HALF   0x0080  /* Try for 100mbps half-duplex */
#define PHY_ADVERTISE_100FULL   0x0100  /* Try for 100mbps full-duplex */

struct qemu_phy {
    uint32_t regs[32];

    int link;

    unsigned int (*read)(struct qemu_phy *phy, unsigned int req);
    void (*write)(struct qemu_phy *phy, unsigned int req, unsigned int data);
};

struct qemu_mdio {
    /* bus. */
    int mdc;
    int mdio;

    /* decoder.  */
    enum {
        PREAMBLE,
        SOF,
        OPC,
        ADDR,
        REQ,
        TURNAROUND,
        DATA
    } state;
    unsigned int drive;

    unsigned int cnt;
    unsigned int addr;
    unsigned int opc;
    unsigned int req;
    unsigned int data;

    struct qemu_phy *devs[32];
};

void tdk_init(struct qemu_phy *phy);
void mdio_attach(struct qemu_mdio *bus, struct qemu_phy *phy,
                 unsigned int addr);
uint16_t mdio_read_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req);
void mdio_write_req(struct qemu_mdio *bus, uint8_t addr, uint8_t req, uint16_t data);
void mdio_cycle(struct qemu_mdio *bus);

#endif
