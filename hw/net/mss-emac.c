/*
 * QEMU model of the Smartfusion2 Ethernet MAC.
 *
 * Copyright (c) 2018 Subbaraya Sundeep <sundeep.lkml@gmail.com>.
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
#include "qemu-common.h"
#include "qemu/log.h"
#include "hw/net/mss-emac.h"
#include "hw/net/mii.h"

#define R_CFG1                  (0x0 / 4)
#define R_CFG2                  (0x4 / 4)
#define R_IFG                   (0x8 / 4)
#define R_HALF_DUPLEX           (0xc / 4)
#define R_FRM_LEN               (0x10 / 4)
#define R_MII_CFG               (0x20 / 4)
#define R_MII_CMD               (0x24 / 4)
#define R_MII_ADDR              (0x28 / 4)
#define R_MII_CTL               (0x2c / 4)
#define R_MII_STS               (0x30 / 4)
#define R_MII_IND               (0x34 / 4)
#define R_STA1                  (0x40 / 4)
#define R_STA2                  (0x44 / 4)
#define R_FIFO_CFG0             (0x48 / 4)

#define R_DMA_TX_CTL            (0x180 / 4)
#define R_DMA_TX_DESC           (0x184 / 4)
#define R_DMA_TX_STATUS         (0x188 / 4)
#define R_DMA_RX_CTL            (0x18c / 4)
#define R_DMA_RX_DESC           (0x190 / 4)
#define R_DMA_RX_STATUS         (0x194 / 4)
#define R_DMA_IRQ_MASK          (0x198 / 4)
#define R_DMA_IRQ               (0x19c / 4)

#define R_DMA_PKTCNT_MASK       0xFF0000
#define R_DMA_PKTCNT_SHIFT      16
#define R_DMA_PKT_TXRX          (1 << 0)
#define DMA_TX_UNDERRUN         (1 << 1)
#define DMA_RX_OVERFLOW         (1 << 2)

#define EMPTY_MASK              (1 << 31)
#define PKT_SIZE                0x7FF

#define CFG1_RESET              (1 << 31)

#define FIFO_CFG0_FTFENRPLY     (1 << 20)
#define FIFO_CFG0_STFENRPLY     (1 << 19)
#define FIFO_CFG0_FRFENRPLY     (1 << 18)
#define FIFO_CFG0_SRFENRPLY     (1 << 17)
#define FIFO_CFG0_WTMENRPLY     (1 << 16)
#define FIFO_CFG0_FTFENREQ      (1 << 12)
#define FIFO_CFG0_STFENREQ      (1 << 11)
#define FIFO_CFG0_FRFENREQ      (1 << 10)
#define FIFO_CFG0_SRFENREQ      (1 << 9)
#define FIFO_CFG0_WTMENREQ      (1 << 8)

#define DMA_TX_CTL_EN           (1 << 0)
#define DMA_RX_CTL_EN           (1 << 0)

#define MII_CMD_READ            (1 << 0)

/* We emulate PHY at address 0x1 */
#define PHYADDR                 0x1
#define MII_ADDR_MASK           0x1F
#define PHY_ADDR_SHIFT          8

typedef struct {
    uint32_t PktAddr;
    uint32_t PktSize;
    uint32_t Next;
} EmacDesc;

static uint32_t emac_get_isr(MSSEmacState *s)
{
    uint32_t ier = s->regs[R_DMA_IRQ_MASK];
    uint32_t tx = s->regs[R_DMA_TX_STATUS] & 0xF;
    uint32_t rx = s->regs[R_DMA_RX_STATUS] & 0xF;
    uint32_t isr = (rx << 4) | tx;

    s->regs[R_DMA_IRQ] = ier & isr;
    return s->regs[R_DMA_IRQ];
}

static void emac_update_irq(MSSEmacState *s)
{
    bool intr = !!emac_get_isr(s);

    qemu_set_irq(s->irq, intr);
}

static void emac_load_desc(EmacDesc *d, hwaddr desc)
{
    cpu_physical_memory_read(desc, d, sizeof *d);
    /* Convert from LE into host endianness. */
    d->PktAddr = le32_to_cpu(d->PktAddr);
    d->PktSize = le32_to_cpu(d->PktSize);
    d->Next = le32_to_cpu(d->Next);
}

static void emac_store_desc(EmacDesc *d, hwaddr desc)
{
    /* Convert from host endianness into LE. */
    d->PktAddr = cpu_to_le32(d->PktAddr);
    d->PktSize = cpu_to_le32(d->PktSize);
    d->Next = cpu_to_le32(d->Next);
    cpu_physical_memory_write(desc, d, sizeof *d);
}

static void mss_dma_tx(MSSEmacState *s)
{
    EmacDesc d;
    uint8_t buf[2 * 1024];
    int size;
    uint8_t pktcnt;
    uint32_t status;

    while (1) {
        emac_load_desc(&d, s->tx_desc);
        if (d.PktSize & EMPTY_MASK) {
            break;
        }
        size = d.PktSize & PKT_SIZE;
        cpu_physical_memory_read(d.PktAddr, buf, size);
        qemu_send_packet(qemu_get_queue(s->nic), buf, size);
        d.PktSize |= EMPTY_MASK;
        emac_store_desc(&d, s->tx_desc);
        /* update sent packets count */
        status = s->regs[R_DMA_TX_STATUS];
        pktcnt = extract32(status, R_DMA_PKTCNT_SHIFT, 8);
        pktcnt++;
        s->regs[R_DMA_TX_STATUS] = ((status & ~R_DMA_PKTCNT_MASK) |
                                    (pktcnt << R_DMA_PKTCNT_SHIFT));
        s->regs[R_DMA_TX_STATUS] |= R_DMA_PKT_TXRX;
        s->tx_desc = d.Next;
    }
    s->regs[R_DMA_TX_STATUS] |= DMA_TX_UNDERRUN;
    s->regs[R_DMA_TX_CTL] &= ~DMA_TX_CTL_EN;
}

static void mss_phy_update_link(MSSEmacState *s)
{
    /* Autonegotiation status mirrors link status. */
    if (qemu_get_queue(s->nic)->link_down) {
        s->phy_regs[MII_BMSR] &= ~(MII_BMSR_AN_COMP |
                                         MII_BMSR_LINK_ST);
    } else {
        s->phy_regs[MII_BMSR] |= (MII_BMSR_AN_COMP |
                                         MII_BMSR_LINK_ST);
    }
}

static void mss_phy_reset(MSSEmacState *s)
{
    memset(&s->phy_regs[0], 0, sizeof(s->phy_regs));
    s->phy_regs[MII_BMSR] = MII_BMSR_AUTONEG | MII_BMSR_MFPS |
                            MII_BMSR_10T_HD | MII_BMSR_10T_FD |
                            MII_BMSR_100TX_HD | MII_BMSR_100TX_FD;
    /* Micrel KSZ8051 PHY */
    s->phy_regs[MII_PHYID1] = 0x0022;
    s->phy_regs[MII_PHYID2] = 0x1550;

    s->phy_regs[MII_ANAR] = MII_ANAR_CSMACD | MII_ANLPAR_10 |
                            MII_ANLPAR_10FD | MII_ANLPAR_TX | MII_ANLPAR_TXFD;
    mss_phy_update_link(s);
}

static void write_to_phy(MSSEmacState *s)
{
    uint8_t reg_addr = s->regs[R_MII_ADDR] & MII_ADDR_MASK;
    uint8_t phy_addr = (s->regs[R_MII_ADDR] >> PHY_ADDR_SHIFT) & MII_ADDR_MASK;
    uint16_t data = s->regs[R_MII_CTL] & 0xFFFF;

    if (phy_addr != PHYADDR) {
        return;
    }

    switch (reg_addr) {
    case MII_BMCR:
        if (data & MII_BMCR_RESET) {
            /* Phy reset */
            mss_phy_reset(s);
            data &= ~MII_BMCR_RESET;
        }
        if (data & MII_BMCR_AUTOEN) {
            /* Complete autonegotiation immediately */
            data &= ~MII_BMCR_AUTOEN;
            s->phy_regs[MII_BMSR] |= MII_BMSR_AN_COMP;
        }
        break;
    }

    s->phy_regs[reg_addr] = data;
}

static uint16_t read_from_phy(MSSEmacState *s)
{
    uint8_t reg_addr = s->regs[R_MII_ADDR] & MII_ADDR_MASK;
    uint8_t phy_addr = (s->regs[R_MII_ADDR] >> PHY_ADDR_SHIFT) & MII_ADDR_MASK;

    if (phy_addr == PHYADDR) {
        return s->phy_regs[reg_addr];
    } else {
        return 0xFFFF;
    }
}

static void mss_emac_do_reset(MSSEmacState *s)
{
    memset(&s->regs[0], 0, sizeof(s->regs));
    s->regs[R_CFG1] = 0x80000000;
    s->regs[R_CFG2] = 0x00007000;
    s->regs[R_IFG] = 0x40605060;
    s->regs[R_HALF_DUPLEX] = 0x00A1F037;
    s->regs[R_FRM_LEN] = 0x00000600;
    s->rx_enabled = false;

    mss_phy_reset(s);
}

static uint64_t emac_read(void *opaque, hwaddr addr, unsigned int size)
{
    MSSEmacState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;

    switch (addr) {
    case R_DMA_IRQ:
        r = emac_get_isr(s);
        break;
    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            r = s->regs[addr];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                         "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                         addr * 4);
            return r;
        }
        break;
    }
    return r;
}

static void emac_write(void *opaque, hwaddr addr, uint64_t val64,
        unsigned int size)
{
    MSSEmacState *s = opaque;
    uint32_t value = val64;
    uint8_t pktcnt;

    addr >>= 2;
    switch (addr) {
    case R_DMA_TX_CTL:
        s->regs[addr] = value;
        if (value & DMA_TX_CTL_EN) {
            mss_dma_tx(s);
        }
        break;
    case R_CFG1:
        if (value & CFG1_RESET) {
            mss_emac_do_reset(s);
        }
    case R_FIFO_CFG0:
        if (value & FIFO_CFG0_FTFENREQ) {
            value |= FIFO_CFG0_FTFENRPLY;
        }
        if (value & FIFO_CFG0_STFENREQ) {
            value |= FIFO_CFG0_STFENRPLY;
        }
        if (value & FIFO_CFG0_FRFENREQ) {
            value |= FIFO_CFG0_FRFENRPLY;
        }
        if (value & FIFO_CFG0_SRFENREQ) {
            value |= FIFO_CFG0_SRFENRPLY;
        }
        if (value & FIFO_CFG0_WTMENREQ) {
            value |= FIFO_CFG0_WTMENRPLY;
        }
        s->regs[addr] = value;
        break;
    case R_DMA_TX_DESC:
        if (value & 0x3) {
            qemu_log_mask(LOG_GUEST_ERROR, "Tx Descriptor address should be"
                        " 32 bit aligned\n");
        }
        /* Ignore [1:0] bits */
        s->regs[addr] = value & 0xFFFFFFFC;
        s->tx_desc = s->regs[addr];
        break;
    case R_DMA_RX_DESC:
        if (value & 0x3) {
            qemu_log_mask(LOG_GUEST_ERROR, "Rx Descriptor address should be"
                        " 32 bit aligned\n");
        }
        /* Ignore [1:0] bits */
        s->regs[addr] = value & 0xFFFFFFFC;
        s->rx_desc = s->regs[addr];
        break;
    case R_DMA_TX_STATUS:
        if (value & DMA_TX_UNDERRUN) {
            s->regs[addr] &= ~DMA_TX_UNDERRUN;
        }
        if (value & R_DMA_PKT_TXRX) {
            pktcnt = extract32(s->regs[addr], R_DMA_PKTCNT_SHIFT, 8);
            pktcnt--;
            s->regs[addr] = ((s->regs[addr] & ~R_DMA_PKTCNT_MASK) |
                                    (pktcnt << R_DMA_PKTCNT_SHIFT));
            if (pktcnt == 0) {
                s->regs[addr] &= ~R_DMA_PKT_TXRX;
            }
        }
        break;
    case R_DMA_RX_STATUS:
        if (value & DMA_RX_OVERFLOW) {
            s->regs[addr] &= ~DMA_RX_OVERFLOW;
        }
        if (value & R_DMA_PKT_TXRX) {
            pktcnt = extract32(s->regs[addr], R_DMA_PKTCNT_SHIFT, 8);
            pktcnt--;
            s->regs[addr] = ((s->regs[addr] & ~R_DMA_PKTCNT_MASK) |
                                    (pktcnt << R_DMA_PKTCNT_SHIFT));
            if (pktcnt == 0) {
                s->regs[addr] &= ~R_DMA_PKT_TXRX;
            }
        }
        break;
    case R_DMA_IRQ:
        break;
    case R_MII_CMD:
        if (value & MII_CMD_READ) {
            s->regs[R_MII_STS] = read_from_phy(s);
        }
        break;
    case R_MII_CTL:
        s->regs[addr] = value;
        write_to_phy(s);
        break;
    default:
        if (addr < ARRAY_SIZE(s->regs)) {
            s->regs[addr] = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                         "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__,
                         addr * 4);
            return;
        }
        break;
    }
    emac_update_irq(s);
}

static const MemoryRegionOps emac_ops = {
    .read = emac_read,
    .write = emac_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int emac_can_rx(NetClientState *nc)
{
    MSSEmacState *s = qemu_get_nic_opaque(nc);

    return !!(s->regs[R_DMA_RX_CTL] & DMA_RX_CTL_EN);
}

static bool match_addr(MSSEmacState *s, const uint8_t *buf)
{
    /*
     * R_STA1 [31:24] : octet 1 of mac address
     * R_STA1 [23:16] : octet 2 of mac address
                .
                .
     * R_STA2 [31:24] : octet 5 of mac address
     * R_STA2 [23:16] : octet 6 of mac address
     */
    uint32_t addr1 = bswap32(s->regs[R_STA1]);
    uint32_t addr2 = s->regs[R_STA2];
    uint8_t *firstword = (uint8_t *)&addr1;
    /* The broadcast MAC address: FF:FF:FF:FF:FF:FF */
    static const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF };

    if (!memcmp(buf, broadcast_addr, 6)) {
        return true;
    }
    if (memcmp(firstword, buf, 4)) {
        return false;
    }
    if (buf[4] != ((addr2 >> 24) & 0xff) ||
            buf[5] != ((addr2 >> 16) & 0xff)) {
        return false;
    }

    return true;
}

static ssize_t emac_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    MSSEmacState *s = qemu_get_nic_opaque(nc);
    EmacDesc d;
    uint8_t pktcnt;
    uint32_t status;

    if (!emac_can_rx(nc)) {
        return -1;
    }
    if (size > (s->regs[R_FRM_LEN] & 0xFFFF)) {
        return -1;
    }
    if (!match_addr(s, buf)) {
        return -1;
    }

    emac_load_desc(&d, s->rx_desc);

    if (d.PktSize & EMPTY_MASK) {
        cpu_physical_memory_write(d.PktAddr, buf, size & PKT_SIZE);
        d.PktSize = size & PKT_SIZE;
        emac_store_desc(&d, s->rx_desc);
        /* update recieved packets count */
        status = s->regs[R_DMA_RX_STATUS];
        pktcnt = extract32(status, R_DMA_PKTCNT_SHIFT, 8);
        pktcnt++;
        s->regs[R_DMA_RX_STATUS] = ((status & ~R_DMA_PKTCNT_MASK) |
                                    (pktcnt << R_DMA_PKTCNT_SHIFT));
        s->regs[R_DMA_RX_STATUS] |= R_DMA_PKT_TXRX;
        s->rx_desc = d.Next;
    } else {
        s->regs[R_DMA_RX_CTL] &= ~DMA_RX_CTL_EN;
        s->regs[R_DMA_RX_STATUS] |= DMA_RX_OVERFLOW;
    }
    emac_update_irq(s);
    return size;
}

static void mss_emac_reset(DeviceState *dev)
{
    MSSEmacState *s = MSS_EMAC(dev);

    mss_emac_do_reset(s);
}

static void emac_set_link(NetClientState *nc)
{
    MSSEmacState *s = qemu_get_nic_opaque(nc);

    mss_phy_update_link(s);
}

static NetClientInfo net_mss_emac_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = emac_can_rx,
    .receive = emac_rx,
    .link_status_changed = emac_set_link,
};

static void mss_emac_realize(DeviceState *dev, Error **errp)
{
    MSSEmacState *s = MSS_EMAC(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_mss_emac_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void mss_emac_init(Object *obj)
{
    MSSEmacState *s = MSS_EMAC(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &emac_ops, s,
                          "mss-emac", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property mss_emac_properties[] = {
    DEFINE_NIC_PROPERTIES(MSSEmacState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_mss_emac = {
    .name = TYPE_MSS_EMAC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(rx_enabled, MSSEmacState),
        VMSTATE_UINT32(rx_desc, MSSEmacState),
        VMSTATE_UINT16_ARRAY(phy_regs, MSSEmacState, 32),
        VMSTATE_UINT32_ARRAY(regs, MSSEmacState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void mss_emac_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mss_emac_realize;
    dc->reset = mss_emac_reset;
    dc->props = mss_emac_properties;
    dc->vmsd = &vmstate_mss_emac;
}

static const TypeInfo mss_emac_info = {
    .name          = TYPE_MSS_EMAC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MSSEmacState),
    .instance_init = mss_emac_init,
    .class_init    = mss_emac_class_init,
};

static void mss_emac_register_types(void)
{
    type_register_static(&mss_emac_info);
}

type_init(mss_emac_register_types)
