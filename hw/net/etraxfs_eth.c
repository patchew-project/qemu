/*
 * QEMU ETRAX Ethernet Controller.
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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/cris/etraxfs.h"
#include "hw/net/mdio.h"
#include "qemu/error-report.h"

#define D(x)

/* ETRAX-FS Ethernet MAC block starts here.  */

#define RW_MA0_LO      0x00
#define RW_MA0_HI      0x01
#define RW_MA1_LO      0x02
#define RW_MA1_HI      0x03
#define RW_GA_LO      0x04
#define RW_GA_HI      0x05
#define RW_GEN_CTRL      0x06
#define RW_REC_CTRL      0x07
#define RW_TR_CTRL      0x08
#define RW_CLR_ERR      0x09
#define RW_MGM_CTRL      0x0a
#define R_STAT          0x0b
#define FS_ETH_MAX_REGS      0x17

#define TYPE_ETRAX_FS_ETH "etraxfs-eth"
#define ETRAX_FS_ETH(obj) \
    OBJECT_CHECK(ETRAXFSEthState, (obj), TYPE_ETRAX_FS_ETH)

typedef struct ETRAXFSEthState
{
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;

    /* Two addrs in the filter.  */
    uint8_t macaddr[2][6];
    uint32_t regs[FS_ETH_MAX_REGS];

    union {
        void *vdma_out;
        struct etraxfs_dma_client *dma_out;
    };
    union {
        void *vdma_in;
        struct etraxfs_dma_client *dma_in;
    };

    /* MDIO bus.  */
    struct qemu_mdio mdio_bus;
    unsigned int phyaddr;
    int duplex_mismatch;

    /* PHY.     */
    struct qemu_phy phy;
} ETRAXFSEthState;

static void eth_validate_duplex(ETRAXFSEthState *eth)
{
    struct qemu_phy *phy;
    unsigned int phy_duplex;
    unsigned int mac_duplex;
    int new_mm = 0;

    phy = eth->mdio_bus.devs[eth->phyaddr];
    phy_duplex = !!(phy->read(phy, 18) & (1 << 11));
    mac_duplex = !!(eth->regs[RW_REC_CTRL] & 128);

    if (mac_duplex != phy_duplex) {
        new_mm = 1;
    }

    if (eth->regs[RW_GEN_CTRL] & 1) {
        if (new_mm != eth->duplex_mismatch) {
            if (new_mm) {
                printf("HW: WARNING ETH duplex mismatch MAC=%d PHY=%d\n",
                       mac_duplex, phy_duplex);
            } else {
                printf("HW: ETH duplex ok.\n");
            }
        }
        eth->duplex_mismatch = new_mm;
    }
}

static uint64_t
eth_read(void *opaque, hwaddr addr, unsigned int size)
{
    ETRAXFSEthState *eth = opaque;
    uint32_t r = 0;

    addr >>= 2;

    switch (addr) {
    case R_STAT:
        r = mdio_bitbang_get_data(&eth->mdio_bus);
        break;
    default:
        r = eth->regs[addr];
        D(printf("%s %x\n", __func__, addr * 4));
        break;
    }
    return r;
}

static void eth_update_ma(ETRAXFSEthState *eth, int ma)
{
    int reg;
    int i = 0;

    ma &= 1;

    reg = RW_MA0_LO;
    if (ma) {
        reg = RW_MA1_LO;
    }

    eth->macaddr[ma][i++] = eth->regs[reg];
    eth->macaddr[ma][i++] = eth->regs[reg] >> 8;
    eth->macaddr[ma][i++] = eth->regs[reg] >> 16;
    eth->macaddr[ma][i++] = eth->regs[reg] >> 24;
    eth->macaddr[ma][i++] = eth->regs[reg + 1];
    eth->macaddr[ma][i] = eth->regs[reg + 1] >> 8;

    D(printf("set mac%d=%x.%x.%x.%x.%x.%x\n", ma,
             eth->macaddr[ma][0], eth->macaddr[ma][1],
             eth->macaddr[ma][2], eth->macaddr[ma][3],
             eth->macaddr[ma][4], eth->macaddr[ma][5]));
}

static void
eth_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    ETRAXFSEthState *eth = opaque;
    uint32_t value = val64;

    addr >>= 2;
    switch (addr) {
    case RW_MA0_LO:
    case RW_MA0_HI:
        eth->regs[addr] = value;
        eth_update_ma(eth, 0);
        break;
    case RW_MA1_LO:
    case RW_MA1_HI:
        eth->regs[addr] = value;
        eth_update_ma(eth, 1);
        break;

    case RW_MGM_CTRL:
        /* Attach an MDIO/PHY abstraction.  */
        if (value & 2) {
            mdio_bitbang_set_data(&eth->mdio_bus, value & 1);
        }
        mdio_bitbang_set_clk(&eth->mdio_bus, value & 4);
        eth_validate_duplex(eth);
        eth->regs[addr] = value;
        break;

    case RW_REC_CTRL:
        eth->regs[addr] = value;
        eth_validate_duplex(eth);
        break;

    default:
        eth->regs[addr] = value;
        D(printf("%s %x %x\n", __func__, addr, value));
        break;
    }
}

/* The ETRAX FS has a groupt address table (GAT) which works like a k=1 bloom
   filter dropping group addresses we have not joined.    The filter has 64
   bits (m). The has function is a simple nible xor of the group addr.    */
static int eth_match_groupaddr(ETRAXFSEthState *eth, const unsigned char *sa)
{
    unsigned int hsh;
    int m_individual = eth->regs[RW_REC_CTRL] & 4;
    int match;

    /* First bit on the wire of a MAC address signals multicast or
       physical address.  */
    if (!m_individual && !(sa[0] & 1)) {
        return 0;
    }

    /* Calculate the hash index for the GA registers. */
    hsh = 0;
    hsh ^= (*sa) & 0x3f;
    hsh ^= ((*sa) >> 6) & 0x03;
    ++sa;
    hsh ^= ((*sa) << 2) & 0x03c;
    hsh ^= ((*sa) >> 4) & 0xf;
    ++sa;
    hsh ^= ((*sa) << 4) & 0x30;
    hsh ^= ((*sa) >> 2) & 0x3f;
    ++sa;
    hsh ^= (*sa) & 0x3f;
    hsh ^= ((*sa) >> 6) & 0x03;
    ++sa;
    hsh ^= ((*sa) << 2) & 0x03c;
    hsh ^= ((*sa) >> 4) & 0xf;
    ++sa;
    hsh ^= ((*sa) << 4) & 0x30;
    hsh ^= ((*sa) >> 2) & 0x3f;

    hsh &= 63;
    if (hsh > 31) {
        match = eth->regs[RW_GA_HI] & (1 << (hsh - 32));
    } else {
        match = eth->regs[RW_GA_LO] & (1 << hsh);
    }
    D(printf("hsh=%x ga=%x.%x mtch=%d\n", hsh,
             eth->regs[RW_GA_HI], eth->regs[RW_GA_LO], match));
    return match;
}

static ssize_t eth_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    unsigned char sa_bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    ETRAXFSEthState *eth = qemu_get_nic_opaque(nc);
    int use_ma0 = eth->regs[RW_REC_CTRL] & 1;
    int use_ma1 = eth->regs[RW_REC_CTRL] & 2;
    int r_bcast = eth->regs[RW_REC_CTRL] & 8;

    if (size < 12) {
        return -1;
    }

    D(printf("%x.%x.%x.%x.%x.%x ma=%d %d bc=%d\n",
         buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
         use_ma0, use_ma1, r_bcast));

    /* Does the frame get through the address filters?  */
    if ((!use_ma0 || memcmp(buf, eth->macaddr[0], 6))
        && (!use_ma1 || memcmp(buf, eth->macaddr[1], 6))
        && (!r_bcast || memcmp(buf, sa_bcast, 6))
        && !eth_match_groupaddr(eth, buf)) {
        return size;
    }

    /* FIXME: Find another way to pass on the fake csum.  */
    etraxfs_dmac_input(eth->dma_in, (void *)buf, size + 4, 1);

    return size;
}

static int eth_tx_push(void *opaque, unsigned char *buf, int len, bool eop)
{
    ETRAXFSEthState *eth = opaque;

    D(printf("%s buf=%p len=%d\n", __func__, buf, len));
    qemu_send_packet(qemu_get_queue(eth->nic), buf, len);
    return len;
}

static void eth_set_link(NetClientState *nc)
{
    ETRAXFSEthState *eth = qemu_get_nic_opaque(nc);
    D(printf("%s %d\n", __func__, nc->link_down));
    eth->phy.link = !nc->link_down;
}

static const MemoryRegionOps eth_ops = {
    .read = eth_read,
    .write = eth_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static NetClientInfo net_etraxfs_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = eth_receive,
    .link_status_changed = eth_set_link,
};

static int fs_eth_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    ETRAXFSEthState *s = ETRAX_FS_ETH(dev);

    if (!s->dma_out || !s->dma_in) {
        error_report("Unconnected ETRAX-FS Ethernet MAC");
        return -1;
    }

    s->dma_out->client.push = eth_tx_push;
    s->dma_out->client.opaque = s;
    s->dma_in->client.opaque = s;
    s->dma_in->client.pull = NULL;

    memory_region_init_io(&s->mmio, OBJECT(dev), &eth_ops, s,
                          "etraxfs-eth", 0x5c);
    sysbus_init_mmio(sbd, &s->mmio);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_etraxfs_info, &s->conf,
                          object_get_typename(OBJECT(s)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);


    mdio_phy_init(&s->phy, 0x0300, 0xe400);
    mdio_attach(&s->mdio_bus, &s->phy, s->phyaddr);
    return 0;
}

static Property etraxfs_eth_properties[] = {
    DEFINE_PROP_UINT32("phyaddr", ETRAXFSEthState, phyaddr, 1),
    DEFINE_PROP_PTR("dma_out", ETRAXFSEthState, vdma_out),
    DEFINE_PROP_PTR("dma_in", ETRAXFSEthState, vdma_in),
    DEFINE_NIC_PROPERTIES(ETRAXFSEthState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void etraxfs_eth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = fs_eth_init;
    dc->props = etraxfs_eth_properties;
    /* Reason: pointer properties "dma_out", "dma_in" */
    dc->user_creatable = false;
}

static const TypeInfo etraxfs_eth_info = {
    .name          = TYPE_ETRAX_FS_ETH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ETRAXFSEthState),
    .class_init    = etraxfs_eth_class_init,
};

static void etraxfs_eth_register_types(void)
{
    type_register_static(&etraxfs_eth_info);
}

type_init(etraxfs_eth_register_types)
