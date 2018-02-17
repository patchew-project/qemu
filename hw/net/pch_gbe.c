#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/net/mii.h"
#include "hw/pci/pci.h"
#include "net/checksum.h"
#include "net/eth.h"
#include "net/net.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#define TYPE_PCH_GBE    "pch_gbe"
#define PCH_GBE(obj)    OBJECT_CHECK(PCHGBEState, (obj), TYPE_PCH_GBE)

#define PCH_GBE_INTR_RX_DMA_CMPLT       BIT(0)
#define PCH_GBE_INTR_RX_VALID           BIT(1)
#define PCH_GBE_INTR_RX_FRAME_ERR       BIT(2)
#define PCH_GBE_INTR_RX_FIFO_ERR        BIT(3)
#define PCH_GBE_INTR_RX_DMA_ERR         BIT(4)
#define PCH_GBE_INTR_RX_DSC_EMP         BIT(5)
#define PCH_GBE_INTR_TX_CMPLT           BIT(8)
#define PCH_GBE_INTR_TX_DMA_CMPLT       BIT(9)
#define PCH_GBE_INTR_TX_FIFO_ERR        BIT(10)
#define PCH_GBE_INTR_TX_DMA_ERR         BIT(11)
#define PCH_GBE_INTR_PAUSE_CMPLT        BIT(12)
#define PCH_GBE_INTR_MIIM_CMPLT         BIT(16)
#define PCH_GBE_INTR_PHY_INT            BIT(20)
#define PCH_GBE_INTR_WOL_DET            BIT(24)
#define PCH_GBE_INTR_TCPIP_ERR          BIT(28)
#define PCH_GBE_INTR_ALL (              \
        PCH_GBE_INTR_RX_DMA_CMPLT |     \
        PCH_GBE_INTR_RX_VALID |         \
        PCH_GBE_INTR_RX_FRAME_ERR |     \
        PCH_GBE_INTR_RX_FIFO_ERR |      \
        PCH_GBE_INTR_RX_DMA_ERR |       \
        PCH_GBE_INTR_RX_DSC_EMP |       \
        PCH_GBE_INTR_TX_CMPLT |         \
        PCH_GBE_INTR_TX_DMA_CMPLT |     \
        PCH_GBE_INTR_TX_FIFO_ERR |      \
        PCH_GBE_INTR_TX_DMA_ERR |       \
        PCH_GBE_INTR_PAUSE_CMPLT |      \
        PCH_GBE_INTR_MIIM_CMPLT |       \
        PCH_GBE_INTR_PHY_INT |          \
        PCH_GBE_INTR_WOL_DET |          \
        PCH_GBE_INTR_TCPIP_ERR)

struct pch_gbe_tx_desc {
    uint32_t addr;

    uint32_t len;
#define PCH_GBE_TX_LENGTH               0xffff

    uint32_t control;
#define PCH_GBE_TX_CONTROL_EOB          0x3
#define PCH_GBE_TX_CONTROL_WORDS        0xfffc
#define PCH_GBE_TX_CONTROL_APAD         BIT(16)
#define PCH_GBE_TX_CONTROL_ICRC         BIT(17)
#define PCH_GBE_TX_CONTROL_ITAG         BIT(18)
#define PCH_GBE_TX_CONTROL_ACCOFF       BIT(19)

    uint32_t status;
#define PCH_GBE_TX_STATUS_TSHRT         BIT(22)
#define PCH_GBE_TX_STATUS_TLNG          BIT(23)
#define PCH_GBE_TX_STATUS_ABT           BIT(28)
#define PCH_GBE_TX_STATUS_CMPLT         BIT(29)
};

struct pch_gbe_rx_desc {
    uint32_t addr;

    uint32_t acc_status;

    uint32_t mac_status;
#define PCH_GBE_RX_MAC_STATUS_EOB       0x3
#define PCH_GBE_RX_MAC_STATUS_WORDS     0xfffc
#define PCH_GBE_RX_MAC_STATUS_LENGTH    0xffff
#define PCH_GBE_RX_MAC_STATUS_TSHRT     BIT(19)
#define PCH_GBE_RX_MAC_STATUS_TLNG      BIT(20)

    uint32_t dma_status;
};

typedef struct {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    NICState *nic;
    NICConf conf;

    bool reset;
    bool phy_reset;

    bool link;

    uint32_t intr_status;
    uint32_t intr_status_hold;
    uint32_t intr_enable;

    uint16_t addr_mask;

    bool rx_enable;
    bool rx_dma_enable;
    bool rx_acc_enable;
    bool rx_acc_csum_off;
    uint32_t rx_desc_base;
    uint32_t rx_desc_size;
    uint32_t rx_desc_hard_ptr;
    uint32_t rx_desc_hard_ptr_hold;
    uint32_t rx_desc_soft_ptr;

    bool tx_dma_enable;
    bool tx_acc_enable;
    uint32_t tx_desc_base;
    uint32_t tx_desc_size;
    uint32_t tx_desc_hard_ptr;
    uint32_t tx_desc_hard_ptr_hold;
    uint32_t tx_desc_soft_ptr;

    uint8_t miim_phy_addr;
    uint8_t miim_reg_addr;
    uint16_t miim_data;

    MemoryRegion bar_mem;
    MemoryRegion bar_io;
    uint16_t io_index;

    uint8_t *pkt_buf;
} PCHGBEState;

static void pch_gbe_update_irq(PCHGBEState *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    pci_set_irq(d, !!(s->intr_status & s->intr_enable));
}

static void pch_gbe_set_intr(PCHGBEState *s, uint32_t intr)
{
    s->intr_status |= intr;
    pch_gbe_update_irq(s);
}

static void pch_gbe_tx(PCHGBEState *s)
{
    struct pch_gbe_tx_desc desc;
    dma_addr_t addr, len, pad;
    uint32_t ctl, sts;

    if (!s->tx_dma_enable) {
        return;
    }

    while (s->tx_desc_hard_ptr != s->tx_desc_soft_ptr) {
        if ((s->tx_desc_hard_ptr & 0xf) ||
            (s->tx_desc_hard_ptr < s->tx_desc_base) ||
            (s->tx_desc_hard_ptr >= (s->tx_desc_base + s->tx_desc_size))) {
            pch_gbe_set_intr(s, PCH_GBE_INTR_TX_DMA_ERR);
            break;
        }

        pci_dma_read(PCI_DEVICE(s), s->tx_desc_hard_ptr, &desc, sizeof(desc));

        ctl = le32_to_cpu(desc.control);
        addr = le32_to_cpu(desc.addr);
        len = le32_to_cpu(desc.len) & PCH_GBE_TX_LENGTH;
        pad = s->tx_acc_enable ? 2 : 0;

        pci_dma_read(PCI_DEVICE(s), addr, s->pkt_buf, len + pad);

        if (pad && (len >= 14)) {
            memcpy(s->pkt_buf + 14, s->pkt_buf + 16, len - 14);
        }

        if ((ctl & PCH_GBE_TX_CONTROL_APAD) && (len < 64)) {
            memset(s->pkt_buf + len, 0, 64 - len);
            len = 64;
        }

        if (s->tx_acc_enable &&
            !(ctl & (PCH_GBE_TX_CONTROL_ICRC | PCH_GBE_TX_CONTROL_ACCOFF))) {
                net_checksum_calculate(s->pkt_buf, len);
        }

        qemu_send_packet(qemu_get_queue(s->nic), s->pkt_buf, len);
        pch_gbe_set_intr(s, PCH_GBE_INTR_TX_DMA_CMPLT);

        sts = PCH_GBE_TX_STATUS_CMPLT;
        desc.status = cpu_to_le32(sts);
        pci_dma_write(PCI_DEVICE(s), s->tx_desc_hard_ptr, &desc, sizeof(desc));
        pch_gbe_set_intr(s, PCH_GBE_INTR_TX_CMPLT);

        s->tx_desc_hard_ptr += sizeof(desc);
        if (s->tx_desc_hard_ptr >= (s->tx_desc_base + s->tx_desc_size)) {
            s->tx_desc_hard_ptr = s->tx_desc_base;
        }
    }
}

static ssize_t pch_gbe_receive(NetClientState *nc,
                               const uint8_t *buf, size_t len)
{
    PCHGBEState *s = qemu_get_nic_opaque(nc);
    struct pch_gbe_rx_desc desc;
    uint32_t mac_status;
    dma_addr_t addr;

    if (s->reset || !s->link || !s->rx_enable || !s->rx_dma_enable) {
        return -1;
    }

    if (s->rx_desc_hard_ptr == s->rx_desc_soft_ptr) {
        pch_gbe_set_intr(s, PCH_GBE_INTR_RX_DSC_EMP);
        return -1;
    }

    pci_dma_read(PCI_DEVICE(s), s->rx_desc_hard_ptr, &desc, sizeof(desc));
    addr = le32_to_cpu(desc.addr);

    if (len < 1519) {
        memcpy(s->pkt_buf, buf, len);

        /* Add an empty FCS */
        memset(&s->pkt_buf[len], 0, 4);
        len += 4;

        pci_dma_write(PCI_DEVICE(s), addr, s->pkt_buf, len);

        mac_status = (len + 3) & PCH_GBE_RX_MAC_STATUS_EOB;
        mac_status |= (len + 3) & PCH_GBE_RX_MAC_STATUS_WORDS;

        /*
         * Unsure why this is required, but the Linux driver subtracts 4 from
         * the length if bit 1 of rx_eob is set. We add 4 here to compensate.
         */
        if (mac_status & BIT(1)) {
            mac_status = (mac_status + 4) & PCH_GBE_RX_MAC_STATUS_LENGTH;
        }

        pch_gbe_set_intr(s, PCH_GBE_INTR_RX_DMA_CMPLT);
        pch_gbe_set_intr(s, PCH_GBE_INTR_RX_VALID);
    } else {
        mac_status = PCH_GBE_RX_MAC_STATUS_TLNG;
        pch_gbe_set_intr(s, PCH_GBE_INTR_RX_FRAME_ERR);
    }

    desc.acc_status = 0;
    desc.mac_status = cpu_to_le32(mac_status);
    desc.dma_status = 0;
    pci_dma_write(PCI_DEVICE(s), s->rx_desc_hard_ptr, &desc, sizeof(desc));

    s->rx_desc_hard_ptr += sizeof(desc);
    if (s->rx_desc_hard_ptr >= (s->rx_desc_base + s->rx_desc_size)) {
        s->rx_desc_hard_ptr = s->rx_desc_base;
    }

    return len;
}

static int pch_gbe_can_receive(NetClientState *nc)
{
    PCHGBEState *s = qemu_get_nic_opaque(nc);

    return s->rx_desc_hard_ptr != s->rx_desc_soft_ptr;
}

static void pch_gbe_set_link_status(NetClientState *nc)
{
    PCHGBEState *s = qemu_get_nic_opaque(nc);

    s->link = !nc->link_down;
}

static NetClientInfo pch_gbe_net_client_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = pch_gbe_can_receive,
    .receive = pch_gbe_receive,
    .link_status_changed = pch_gbe_set_link_status,
};

static void pch_gbe_reset(DeviceState *d)
{
    PCHGBEState *s = PCH_GBE(d);

    s->io_index = 0;

    s->intr_status = 0;
    s->intr_status_hold = 0;
    s->intr_enable = 0;
    pch_gbe_update_irq(s);

    pch_gbe_set_link_status(qemu_get_queue(s->nic));
}

/*
 * PHY registers
 */

static void pch_gbe_phy_write(PCHGBEState *s, uint8_t addr, uint16_t val)
{
    switch (addr) {
    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PHY write 0x%x = 0x%x\n",
                      addr, val);
    }
}

static uint16_t pch_gbe_phy_read(PCHGBEState *s, uint8_t addr)
{
    switch (addr) {
    case MII_BMCR:
        return MII_BMCR_SPEED1000 | MII_BMCR_FD;

    case MII_BMSR:
        return MII_BMSR_100TX_FD | MII_BMSR_AN_COMP |
               (s->link ? MII_BMSR_LINK_ST : 0);

    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PHY read 0x%x\n", addr);
    }
    return 0;
}

/*
 * PCI Memory Mapped I/O Space
 */

enum pch_gbe_mem_regs {
    PCH_GBE_MEM_INTR                    = 0x000,
    PCH_GBE_MEM_INTR_EN                 = 0x004,
    PCH_GBE_MEM_INTR_HOLD               = 0x018,

    PCH_GBE_MEM_RESET                   = 0x00c,
#define PCH_GBE_MEM_RESET_ALL           BIT(31)
#define PCH_GBE_MEM_RESET_TX            BIT(15)
#define PCH_GBE_MEM_RESET_RX            BIT(14)

    PCH_GBE_MEM_TCPIPACC                = 0x010,
#define PCH_GBE_MEM_TCPIPACC_RXEN       BIT(0)
#define PCH_GBE_MEM_TCPIPACC_TXEN       BIT(1)
#define PCH_GBE_MEM_TCPIPACC_RXSUMOFF   BIT(2)

    PCH_GBE_MEM_MAX_RXEN                = 0x020,
#define PCH_GBE_MEM_MAX_RXEN_EN         BIT(0)

    PCH_GBE_MEM_MAC_ADDR_1A             = 0x060,
    PCH_GBE_MEM_MAC_ADDR_1B             = 0x064,

    PCH_GBE_MEM_ADDR_MASK               = 0x0e0,
#define PCH_GBE_MEM_ADDR_MASK_MAC0      BIT(0)
#define PCH_GBE_MEM_ADDR_MASK_BUSY      BIT(31)

    PCH_GBE_MEM_MIIM                    = 0x0e4,
#define PCH_GBE_MEM_MIIM_READY          BIT(26)
#define PCH_GBE_MEM_MIIM_WRITE          BIT(26)
#define PCH_GBE_MEM_MIIM_PHY_ADDR_SHF   21
#define PCH_GBE_MEM_MIIM_PHY_ADDR_MSK   (0x1f << 21)
#define PCH_GBE_MEM_MIIM_REG_ADDR_SHF   16
#define PCH_GBE_MEM_MIIM_REG_ADDR_MSK   (0x1f << 16)
#define PCH_GBE_MEM_MIIM_DATA           0xffff

    PCH_GBE_MEM_RGMII_STATUS            = 0x0ec,
#define PCH_GBE_MEM_RGMII_STATUS_FDPLX  BIT(0)
#define PCH_GBE_MEM_RGMII_STATUS_UP     BIT(3)

    PCH_GBE_MEM_DMA_CONTROL             = 0x100,
#define PCH_GBE_MEM_DMA_CONTROL_TX_EN   BIT(0)
#define PCH_GBE_MEM_DMA_CONTROL_RX_EN   BIT(1)

    PCH_GBE_MEM_RX_DESC_BASE            = 0x110,

    PCH_GBE_MEM_RX_DESC_SIZE            = 0x114,
#define PCH_GBE_MEM_RX_DESC_SIZE_SIZE   0xfff0

    PCH_GBE_MEM_RX_DESC_HARD_PTR        = 0x118,
    PCH_GBE_MEM_RX_DESC_HARD_PTR_HOLD   = 0x11c,
    PCH_GBE_MEM_RX_DESC_SOFT_PTR        = 0x120,

    PCH_GBE_MEM_TX_DESC_BASE            = 0x130,

    PCH_GBE_MEM_TX_DESC_SIZE            = 0x134,
#define PCH_GBE_MEM_TX_DESC_SIZE_SIZE   0xfff0

    PCH_GBE_MEM_TX_DESC_HARD_PTR        = 0x138,
    PCH_GBE_MEM_TX_DESC_HARD_PTR_HOLD   = 0x13c,
    PCH_GBE_MEM_TX_DESC_SOFT_PTR        = 0x140,

    PCH_GBE_MEM_SRST                    = 0x1fc,
#define PCH_GBE_MEM_SRST_SRST           BIT(0)
};

static void pch_gbe_mem_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PCHGBEState *s = PCH_GBE(opaque);

    switch (addr) {
    case PCH_GBE_MEM_INTR:
    case PCH_GBE_MEM_INTR_HOLD:
    case PCH_GBE_MEM_RX_DESC_HARD_PTR_HOLD:
    case PCH_GBE_MEM_TX_DESC_HARD_PTR_HOLD:
        /* read-only */
        break;

    case PCH_GBE_MEM_INTR_EN:
        s->intr_enable = val & PCH_GBE_INTR_ALL;
        pch_gbe_update_irq(s);
        break;

    case PCH_GBE_MEM_RESET:
        s->reset = !!(val & PCH_GBE_MEM_RESET_ALL);
        if (s->reset) {
            pch_gbe_reset(DEVICE(s));
            s->reset = false;
            break;
        }
        if (val & PCH_GBE_MEM_RESET_TX) {
            qemu_log_mask(LOG_UNIMP,
                          "pch_gbe: Partial (TX) reset unimplemented\n");
        }
        if (val & PCH_GBE_MEM_RESET_RX) {
            qemu_log_mask(LOG_UNIMP,
                          "pch_gbe: Partial (RX) reset unimplemented\n");
        }
        break;

    case PCH_GBE_MEM_TCPIPACC:
        s->rx_acc_enable = !!(val & PCH_GBE_MEM_TCPIPACC_RXEN);
        s->tx_acc_enable = !!(val & PCH_GBE_MEM_TCPIPACC_TXEN);
        s->rx_acc_csum_off = !!(val & PCH_GBE_MEM_TCPIPACC_RXSUMOFF);
        if (s->rx_acc_enable) {
            qemu_log_mask(LOG_UNIMP,
                          "pch_gbe: RX acceleration unimplemented\n");
        }
        break;

    case PCH_GBE_MEM_MAX_RXEN:
        s->rx_enable = !!(val & PCH_GBE_MEM_MAX_RXEN_EN);
        break;

    case PCH_GBE_MEM_MAC_ADDR_1A:
        s->conf.macaddr.a[0] = (val >> 0);
        s->conf.macaddr.a[1] = (val >> 8);
        s->conf.macaddr.a[2] = (val >> 16);
        s->conf.macaddr.a[3] = (val >> 24);
        break;

    case PCH_GBE_MEM_MAC_ADDR_1B:
        s->conf.macaddr.a[4] = (val >> 0);
        s->conf.macaddr.a[5] = (val >> 8);
        break;

    case PCH_GBE_MEM_ADDR_MASK:
        s->addr_mask = val & PCH_GBE_MEM_ADDR_MASK_MAC0;
        break;

    case PCH_GBE_MEM_MIIM:
        s->miim_phy_addr = (val & PCH_GBE_MEM_MIIM_PHY_ADDR_MSK)
                         >> PCH_GBE_MEM_MIIM_PHY_ADDR_SHF;
        s->miim_reg_addr = (val & PCH_GBE_MEM_MIIM_REG_ADDR_MSK)
                         >> PCH_GBE_MEM_MIIM_REG_ADDR_SHF;
        s->miim_data = val & PCH_GBE_MEM_MIIM_DATA;
        if (s->miim_phy_addr == 1) {
            if (val & PCH_GBE_MEM_MIIM_WRITE) {
                pch_gbe_phy_write(s, s->miim_reg_addr, s->miim_data);
            } else {
                s->miim_data = pch_gbe_phy_read(s, s->miim_reg_addr);
            }
        } else if (!(val & PCH_GBE_MEM_MIIM_WRITE)) {
            s->miim_data = PCH_GBE_MEM_MIIM_DATA;
        }
        pch_gbe_set_intr(s, PCH_GBE_INTR_MIIM_CMPLT);
        break;

    case PCH_GBE_MEM_DMA_CONTROL:
        s->rx_dma_enable = !!(val & PCH_GBE_MEM_DMA_CONTROL_RX_EN);
        s->tx_dma_enable = !!(val & PCH_GBE_MEM_DMA_CONTROL_TX_EN);
        break;

    case PCH_GBE_MEM_RX_DESC_BASE:
        s->rx_desc_base = val;
        s->rx_desc_hard_ptr = s->rx_desc_base;
        break;

    case PCH_GBE_MEM_RX_DESC_SIZE:
        s->rx_desc_size = (val & PCH_GBE_MEM_RX_DESC_SIZE_SIZE) + 0x10;
        break;

    case PCH_GBE_MEM_RX_DESC_HARD_PTR:
        s->rx_desc_hard_ptr = val;
        break;

    case PCH_GBE_MEM_RX_DESC_SOFT_PTR:
        s->rx_desc_soft_ptr = val;
        break;

    case PCH_GBE_MEM_TX_DESC_BASE:
        s->tx_desc_base = val;
        s->tx_desc_hard_ptr = s->tx_desc_base;
        pch_gbe_tx(s);
        break;

    case PCH_GBE_MEM_TX_DESC_SIZE:
        s->tx_desc_size = (val & PCH_GBE_MEM_TX_DESC_SIZE_SIZE) + 0x10;
        pch_gbe_tx(s);
        break;

    case PCH_GBE_MEM_TX_DESC_HARD_PTR:
        s->tx_desc_hard_ptr = val;
        pch_gbe_tx(s);
        break;

    case PCH_GBE_MEM_TX_DESC_SOFT_PTR:
        s->tx_desc_soft_ptr = val;
        pch_gbe_tx(s);
        break;

    case PCH_GBE_MEM_SRST:
        s->reset = val & PCH_GBE_MEM_SRST_SRST;
        if (s->reset) {
            pch_gbe_reset(DEVICE(s));
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PCI mem write 0x%"
                      HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
    }
}

static uint64_t pch_gbe_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    PCHGBEState *s = PCH_GBE(opaque);

    switch (addr) {
    case PCH_GBE_MEM_INTR:
        s->rx_desc_hard_ptr_hold = s->rx_desc_hard_ptr;
        s->tx_desc_hard_ptr_hold = s->tx_desc_hard_ptr;
        s->intr_status_hold = s->intr_status;
        s->intr_status = 0;
        pch_gbe_update_irq(s);
    case PCH_GBE_MEM_INTR_HOLD:
        return s->intr_status_hold;

    case PCH_GBE_MEM_INTR_EN:
        return s->intr_enable;

    case PCH_GBE_MEM_RESET:
        return 0;

    case PCH_GBE_MEM_TCPIPACC:
        return (s->rx_acc_enable ? PCH_GBE_MEM_TCPIPACC_RXEN : 0) |
               (s->tx_acc_enable ? PCH_GBE_MEM_TCPIPACC_TXEN : 0) |
               (s->rx_acc_csum_off ? PCH_GBE_MEM_TCPIPACC_RXSUMOFF : 0);

    case PCH_GBE_MEM_MAX_RXEN:
        return s->rx_enable ? PCH_GBE_MEM_MAX_RXEN_EN : 0;

    case PCH_GBE_MEM_MAC_ADDR_1A:
        return s->conf.macaddr.a[0] << 0 |
               s->conf.macaddr.a[1] << 8 |
               s->conf.macaddr.a[2] << 16 |
               s->conf.macaddr.a[3] << 24;

    case PCH_GBE_MEM_MAC_ADDR_1B:
        return s->conf.macaddr.a[4] << 0 |
               s->conf.macaddr.a[5] << 8;

    case PCH_GBE_MEM_ADDR_MASK:
        return s->addr_mask;

    case PCH_GBE_MEM_MIIM:
        return PCH_GBE_MEM_MIIM_READY |
            (s->miim_phy_addr << PCH_GBE_MEM_MIIM_PHY_ADDR_SHF) |
            (s->miim_reg_addr << PCH_GBE_MEM_MIIM_REG_ADDR_SHF) |
            s->miim_data;

    case PCH_GBE_MEM_SRST:
        return s->reset ? PCH_GBE_MEM_SRST_SRST : 0;

    case PCH_GBE_MEM_RGMII_STATUS:
        return (s->link ? PCH_GBE_MEM_RGMII_STATUS_UP : 0) |
               PCH_GBE_MEM_RGMII_STATUS_FDPLX;

    case PCH_GBE_MEM_DMA_CONTROL:
        return (s->rx_dma_enable ? PCH_GBE_MEM_DMA_CONTROL_RX_EN : 0) |
               (s->tx_dma_enable ? PCH_GBE_MEM_DMA_CONTROL_TX_EN : 0);

    case PCH_GBE_MEM_RX_DESC_BASE:
        return s->rx_desc_base;

    case PCH_GBE_MEM_RX_DESC_SIZE:
        return (s->rx_desc_size - 0x10) & PCH_GBE_MEM_RX_DESC_SIZE_SIZE;

    case PCH_GBE_MEM_RX_DESC_HARD_PTR:
        return s->rx_desc_hard_ptr;

    case PCH_GBE_MEM_RX_DESC_HARD_PTR_HOLD:
        return s->rx_desc_hard_ptr_hold;

    case PCH_GBE_MEM_RX_DESC_SOFT_PTR:
        return s->rx_desc_soft_ptr;

    case PCH_GBE_MEM_TX_DESC_BASE:
        return s->tx_desc_base;

    case PCH_GBE_MEM_TX_DESC_SIZE:
        return (s->tx_desc_size - 0x10) & PCH_GBE_MEM_TX_DESC_SIZE_SIZE;

    case PCH_GBE_MEM_TX_DESC_HARD_PTR:
        return s->tx_desc_hard_ptr;

    case PCH_GBE_MEM_TX_DESC_HARD_PTR_HOLD:
        return s->tx_desc_hard_ptr_hold;

    case PCH_GBE_MEM_TX_DESC_SOFT_PTR:
        return s->tx_desc_soft_ptr;

    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PCI mem read 0x%"
                      HWADDR_PRIx "\n", addr);
        return -1;
    }
}

static const MemoryRegionOps pch_gbe_mem_ops = {
    .read = pch_gbe_mem_read,
    .write = pch_gbe_mem_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * PCI I/O Space
 */

enum pch_gbe_io_regs {
    PCH_GBE_IO_INDEX                    = 0x0,
#define PCH_GBE_IO_INDEX_INDEX          0x1ff

    PCH_GBE_IO_DATA                     = 0x4,
};

static void pch_gbe_io_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    PCHGBEState *s = PCH_GBE(opaque);

    switch (addr) {
    case PCH_GBE_IO_INDEX:
        s->io_index = val & PCH_GBE_IO_INDEX_INDEX;
        break;

    case PCH_GBE_IO_DATA:
        pch_gbe_mem_write(opaque, s->io_index, val, size);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PCI I/O write 0x%"
                      HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
    }
}

static uint64_t pch_gbe_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCHGBEState *s = PCH_GBE(opaque);

    switch (addr) {
    case PCH_GBE_IO_INDEX:
        return s->io_index;

    case PCH_GBE_IO_DATA:
        return pch_gbe_mem_read(opaque, s->io_index, size);

    default:
        qemu_log_mask(LOG_UNIMP, "pch_gbe: Unhandled PCI I/O read 0x%"
                      HWADDR_PRIx "\n", addr);
        return -1;
    }
}

static const MemoryRegionOps pch_gbe_io_ops = {
    .read = pch_gbe_io_read,
    .write = pch_gbe_io_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pch_gbe_realize(PCIDevice *dev, Error **errp)
{
    PCHGBEState *s = PCH_GBE(dev);

    pci_config_set_interrupt_pin(dev->config, 1);

    memory_region_init_io(&s->bar_io, OBJECT(s), &pch_gbe_io_ops, s,
                          "pch_gbe-io", 0x20);
    memory_region_init_io(&s->bar_mem, OBJECT(s), &pch_gbe_mem_ops, s,
                          "pch_gbe-mem", 0x200);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_io);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mem);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->pkt_buf = g_malloc(64 * 1024);

    s->nic = qemu_new_nic(&pch_gbe_net_client_info, &s->conf,
                          object_get_typename(OBJECT(dev)), DEVICE(dev)->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void pch_gbe_uninit(PCIDevice *dev)
{
    PCHGBEState *s = PCH_GBE(dev);

    g_free(s->pkt_buf);
}

static void pch_gbe_instance_init(Object *obj)
{
}

static Property pch_gbe_properties[] = {
    DEFINE_NIC_PROPERTIES(PCHGBEState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void pch_gbe_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pch_gbe_realize;
    k->exit = pch_gbe_uninit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = 0x8802;
    k->revision = 0x2;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = pch_gbe_reset;
    dc->props = pch_gbe_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pch_gbe_info = {
    .name          = TYPE_PCH_GBE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCHGBEState),
    .class_init    = pch_gbe_class_init,
    .instance_init = pch_gbe_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void pch_gbe_register_types(void)
{
    type_register_static(&pch_gbe_info);
}
type_init(pch_gbe_register_types)
