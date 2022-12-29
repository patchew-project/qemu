/*
 * QEMU INTEL 82576EB GbE NIC emulation
 *
 * Software developer's manuals:
 * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82576eg-gbe-datasheet.pdf
 *
 * Authors:
 * Sriram Yagnaraman <sriram.yagnaraman@est.tech>
 *
 * Based on work done by:
 * Knut Omang.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NET_IGB_CORE_H
#define HW_NET_IGB_CORE_H

#define IGB_PHY_PAGE_SIZE    (0x20)
#define IGB_PHY_PAGES        (0x07)
#define IGB_MAC_SIZE         (0x8000)
#define IGB_EEPROM_SIZE      (80)
#define IGB_MSIX_VEC_NUM     (25)
#define IGB_NUM_QUEUES       (16)
#define IGB_VF_NUM_QUEUES    (2)
#define IGB_NUM_TX_CTX       (8)
#define IGB_RAR_ENTRIES      (24)

#define IGB_MSIX_BAR 3
#define IGB_MSIX_VECTORS_PF 10
#define IGB_MSIX_VECTORS_VF 3
#define IGB_TOTAL_VFS 8
#define IGB_VF_OFFSET 0x80
#define IGB_VF_STRIDE 2

typedef struct IgbCore IGBCore;

enum { PHY_R = BIT(0),
       PHY_W = BIT(1),
       PHY_RW = PHY_R | PHY_W,
       PHY_ANYPAGE = BIT(2) };

typedef struct IgbIntrDelayTimer_st {
    QEMUTimer *timer;
    bool running;
    uint32_t delay_reg;
    uint32_t delay_resolution_us;
    IGBCore *core;
} IgbIntrDelayTimer;

typedef struct IGBRingInfo_st {
    int dbah;
    int dbal;
    int dlen;
    int dh;
    int dt;
    int idx;
} IGBRingInfo;

typedef struct IGBTxSwitchInfo_st {
    bool external;
    bool local;
    uint8_t mrqc_type;
    struct {
        bool enabled;
        bool replicate;
        uint8_t num_pools;
        uint8_t spool_select;
        uint8_t dpool_select;
    } vmdq;
} IGBTxSwitchInfo;

typedef struct IGBRxQueueInfo_st {
    uint8_t mrqc_type;
    uint8_t queue;
    struct {
        bool enabled;
        bool replicate;
        uint8_t num_pools;
        uint8_t pool_select;
    } vmdq;
    struct {
        bool enabled;
        uint32_t hash;
        uint32_t type;
    } rss;
} IGBRxQueueInfo;

struct IgbCore {
    uint32_t mac[IGB_MAC_SIZE];
    uint16_t phy[IGB_PHY_PAGES][IGB_PHY_PAGE_SIZE];
    uint16_t eeprom[IGB_EEPROM_SIZE];

    uint32_t rx_desc_buf_size[IGB_NUM_QUEUES];
    uint8_t rx_desc_len;

    QEMUTimer *autoneg_timer;

    IGBRingInfo tx_ring_info[IGB_NUM_QUEUES];
    IGBRingInfo rx_ring_info[IGB_NUM_QUEUES];

    struct igb_tx_ctx {
        uint16_t ip_len;
        uint8_t mac_len;
        uint16_t vlan;
        uint16_t tucmd;
        uint8_t l4_len;
        uint16_t mss;
        uint8_t idx;
        bool valid;
    } tx_ctx[IGB_NUM_TX_CTX];

    struct igb_tx {
        IGBTxSwitchInfo info;
        bool is_first;
        bool skip_current_pkt;
        bool vlan_needed;
        bool cptse;
        uint8_t sum_needed;
        uint8_t ctx_id;
        struct NetTxPkt *tx_pkt;
    } tx[IGB_NUM_QUEUES];

    struct igb_rx {
        IGBRxQueueInfo info;
        bool mac_pif;
        bool mac_match;
        bool vlan_match;
        eth_pkt_types_e eth_pkt_type;
        struct NetRxPkt *rx_pkt;
    } rx;

    bool has_vnet;
    int max_queue_num;

    /* Interrupt moderation management */
    uint32_t delayed_causes;

    IgbIntrDelayTimer eitr[IGB_MSIX_VEC_NUM];
    bool eitr_intr_pending[IGB_MSIX_VEC_NUM];
    uint32_t eitr_guest_value[IGB_MSIX_VEC_NUM];

    VMChangeStateEntry *vmstate;

    uint16_t vet;

    uint8_t permanent_mac[ETH_ALEN];

    NICState *owner_nic;
    PCIDevice *owner;

    IGBCore *pf;
    IGBCore *vfs[IGB_TOTAL_VFS];
    uint32_t msi_causes_pending;
};

void
igb_core_write(IGBCore *core, hwaddr addr, uint64_t val, unsigned size);

uint64_t
igb_core_read(IGBCore *core, hwaddr addr, unsigned size);

void
igb_core_pci_realize(IGBCore      *regs,
                       const uint16_t *eeprom_templ,
                       uint32_t        eeprom_size,
                       const uint8_t  *macaddr);

void
igb_core_reset(IGBCore *core);

void
igb_core_pre_save(IGBCore *core);

int
igb_core_post_load(IGBCore *core);

void
igb_core_set_link_status(IGBCore *core);

void
igb_core_pci_uninit(IGBCore *core);

bool
igb_can_receive(IGBCore *core);

ssize_t
igb_receive(IGBCore *core, const uint8_t *buf, size_t size);

ssize_t
igb_receive_iov(IGBCore *core, const struct iovec *iov, int iovcnt);

void
igb_start_recv(IGBCore *core);

#endif
