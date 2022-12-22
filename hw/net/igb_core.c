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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "net/net.h"
#include "net/tap.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "sysemu/runstate.h"

#include "net_tx_pkt.h"
#include "net_rx_pkt.h"

#include "e1000x_common.h"
#include "igb_core.h"

#include "trace.h"

#define IGB_START_EITR   (648) /* ~6000 ints/sec */
#define IGB_MAX_TX_FRAGS (64)

#define IGB_IVAR_ENTRY_MASK   0xff
#define IGB_IVAR_ENTRY_VALID(x) ((x) & 0x80)
#define IGB_IVAR_ENTRY_VEC(pci_dev, x) ((x) & (pci_is_vf(pci_dev) ? 0x03 : 0x1f))

#define IGB_NUM_VECTORS(pci_dev) \
    (pci_is_vf(pci_dev) ? IGB_MSIX_VECTORS_VF : IGB_MSIX_VECTORS_PF)

#define IGB_LOW_BITS_READ_FUNC(num)                   \
    static uint32_t                                   \
    igb_mac_low##num##_read(IGBCore *core, int index) \
    {                                                 \
        return core->mac[index] & (BIT(num) - 1);     \
    }                                                 \

#define IGB_LOW_BITS_READ(num)                        \
    igb_mac_low##num##_read

IGB_LOW_BITS_READ_FUNC(4);
IGB_LOW_BITS_READ_FUNC(6);
IGB_LOW_BITS_READ_FUNC(11);
IGB_LOW_BITS_READ_FUNC(13);
IGB_LOW_BITS_READ_FUNC(16);

static inline void
igb_set_interrupt_cause(IGBCore *core, uint32_t val);

static void
igb_update_interrupt_state(IGBCore *core);

static bool
igb_rx_uc_mac_filter(IGBCore *core, const uint8_t *buf, uint8_t *pool_select);

static bool
igb_rx_uta_filter(IGBCore *core, uint8_t *buf);

static bool
igb_rx_mta_filter(IGBCore *core, uint8_t *buf);

static inline void
igb_process_ts_option(IGBCore *core, union e1000_adv_tx_desc *dp)
{
    if (le32_to_cpu(dp->read.cmd_type_len) & E1000_ADVTXD_MAC_TSTAMP) {
        trace_igb_wrn_no_ts_support();
    }
}

static inline void
igb_raise_legacy_irq(IGBCore *core)
{
    trace_igb_irq_legacy_notify(true);
    e1000x_inc_reg_if_not_full(core->mac, IAC);
    pci_set_irq(core->owner, 1);
}

static inline void
igb_lower_legacy_irq(IGBCore *core)
{
    trace_igb_irq_legacy_notify(false);
    pci_set_irq(core->owner, 0);
}

static inline void
igb_intrmgr_rearm_timer(IgbIntrDelayTimer *timer)
{
    int64_t delay_us = (int64_t) timer->core->mac[timer->delay_reg] *
                                 timer->delay_resolution_us;

    trace_igb_irq_rearm_timer(timer->delay_reg << 2, delay_us);

    timer_mod(timer->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + delay_us);

    timer->running = true;
}

static void
igb_intmgr_timer_resume(IgbIntrDelayTimer *timer)
{
    if (timer->running) {
        igb_intrmgr_rearm_timer(timer);
    }
}

static void
igb_intmgr_timer_pause(IgbIntrDelayTimer *timer)
{
    if (timer->running) {
        timer_del(timer->timer);
    }
}

static inline void
igb_intrmgr_stop_timer(IgbIntrDelayTimer *timer)
{
    if (timer->running) {
        timer_del(timer->timer);
        timer->running = false;
    }
}

static void
igb_intrmgr_on_msix_throttling_timer(void *opaque)
{
    IgbIntrDelayTimer *timer = opaque;
    int idx = timer - &timer->core->eitr[0];

    assert(msix_enabled(timer->core->owner));

    timer->running = false;

    if (!timer->core->eitr_intr_pending[idx]) {
        trace_igb_irq_throttling_no_pending_vec(idx);
        return;
    }

    trace_igb_irq_msix_notify_postponed_vec(idx);
    msix_notify(timer->core->owner, idx);
}

static void
igb_intrmgr_initialize_all_timers(IGBCore *core, bool create)
{
    int i;
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (i = 0; i < num_vectors; i++) {
        core->eitr[i].core = core;
        core->eitr[i].delay_reg = EITR_IGB + i;
        core->eitr[i].delay_resolution_us = E1000_INTR_THROTTLING_US_RES;
    }

    if (!create) {
        return;
    }

    for (i = 0; i < num_vectors; i++) {
        core->eitr[i].timer =
            timer_new_ns(QEMU_CLOCK_VIRTUAL,
                         igb_intrmgr_on_msix_throttling_timer,
                         &core->eitr[i]);
    }
}

static void
igb_intrmgr_resume(IGBCore *core)
{
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (int i = 0; i < num_vectors; i++) {
        igb_intmgr_timer_resume(&core->eitr[i]);
    }
}

static void
igb_intrmgr_pause(IGBCore *core)
{
    int i;
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (i = 0; i < num_vectors; i++) {
        igb_intmgr_timer_pause(&core->eitr[i]);
    }
}

static void
igb_intrmgr_reset(IGBCore *core)
{
    int i;
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    core->delayed_causes = 0;

    for (i = 0; i < num_vectors; i++) {
        igb_intrmgr_stop_timer(&core->eitr[i]);
    }
}

static void
igb_intrmgr_pci_unint(IGBCore *core)
{
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (int i = 0; i < num_vectors; i++) {
        timer_free(core->eitr[i].timer);
    }
}

static void
igb_intrmgr_pci_realize(IGBCore *core)
{
    igb_intrmgr_initialize_all_timers(core, true);
}

static inline bool
igb_rx_csum_enabled(IGBCore *core)
{
    return (core->mac[RXCSUM] & E1000_RXCSUM_PCSD) ? false : true;
}

static uint32_t
igb_rss_get_hash_type(IGBCore *core, struct igb_rx *rx)
{
    bool isip4, isip6, isudp, istcp;
    net_rx_pkt_get_protocols(rx->rx_pkt, &isip4, &isip6, &isudp, &istcp);

    if (isip4) {
        bool fragment = net_rx_pkt_get_ip4_info(rx->rx_pkt)->fragment;

        trace_igb_rx_rss_ip4(fragment, istcp, core->mac[MRQC],
                                E1000_MRQC_EN_TCPIPV4(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV4(core->mac[MRQC]));

        if (!fragment) {
            if (istcp && E1000_MRQC_EN_TCPIPV4(core->mac[MRQC])) {
                return E1000_MRQ_RSS_TYPE_IPV4TCP;
            }
            if (isudp && E1000_MRQC_EN_UDPIPV4(core->mac[MRQC])) {
                return E1000_MRQ_RSS_TYPE_IPV4UDP;
            }
        }
        if (E1000_MRQC_EN_IPV4(core->mac[MRQC])) {
            return E1000_MRQ_RSS_TYPE_IPV4;
        }
    } else if (isip6) {
        eth_ip6_hdr_info *ip6info = net_rx_pkt_get_ip6_info(rx->rx_pkt);
        bool ex_dis = core->mac[RFCTL] & E1000_RFCTL_IPV6_EX_DIS;

        /*
         * Following two traces must not be combined because resulting
         * event will have 11 arguments totally and some trace backends
         * (at least "ust") have limitation of maximum 10 arguments per
         * event. Events with more arguments fail to compile for
         * backends like these.
         */
        trace_igb_rx_rss_ip6_rfctl(core->mac[RFCTL]);
        trace_igb_rx_rss_ip6(ex_dis, istcp,
                                ip6info->has_ext_hdrs,
                                ip6info->rss_ex_dst_valid,
                                ip6info->rss_ex_src_valid,
                                core->mac[MRQC],
                                E1000_MRQC_EN_TCPIPV6(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV6EX(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV6(core->mac[MRQC]));

        if (!ex_dis || !ip6info->has_ext_hdrs) {
            if (!ip6info->fragment) {
                if (istcp && E1000_MRQC_EN_TCPIPV6_IGB(core->mac[MRQC])) {
                    return E1000_MRQ_RSS_TYPE_IPV6TCP;
                }
                if (istcp && E1000_MRQC_EN_TCPIPV6EX_IGB(core->mac[MRQC])) {
                    return E1000_MRQ_RSS_TYPE_IPV6TCPEX;
                }
                if (isudp && E1000_MRQC_EN_UDPIPV6(core->mac[MRQC])) {
                    return E1000_MRQ_RSS_TYPE_IPV6UDP;
                }
                if (istcp && E1000_MRQC_EN_UDPIPV6EX(core->mac[MRQC])) {
                    return E1000_MRQ_RSS_TYPE_IPV6UDPEX;
                }
            }
            if (E1000_MRQC_EN_IPV6EX(core->mac[MRQC])) {
                return E1000_MRQ_RSS_TYPE_IPV6EX;
            }
        }

        if (E1000_MRQC_EN_IPV6(core->mac[MRQC])) {
            return E1000_MRQ_RSS_TYPE_IPV6;
        }
    }
    return E1000_MRQ_RSS_TYPE_NONE;
}

static uint32_t
igb_rss_calc_hash(IGBCore *core, struct igb_rx *rx)
{
    NetRxPktRssType type;

    assert(rx->info.rss.enabled);
    switch (rx->info.rss.type) {
    case E1000_MRQ_RSS_TYPE_IPV4:
        type = NetPktRssIpV4;
        break;
    case E1000_MRQ_RSS_TYPE_IPV4TCP:
        type = NetPktRssIpV4Tcp;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6TCP:
        type = NetPktRssIpV6Tcp;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6:
        type = NetPktRssIpV6;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6EX:
        type = NetPktRssIpV6Ex;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6TCPEX:
        type = NetPktRssIpV6TcpEx;
        break;
    case E1000_MRQ_RSS_TYPE_IPV4UDP:
        type = NetPktRssIpV4Udp;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6UDP:
        type = NetPktRssIpV6Udp;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6UDPEX:
        type = NetPktRssIpV6UdpEx;
        break;
    default:
        assert(false);
        return 0;
    }

    return net_rx_pkt_calc_rss_hash(rx->rx_pkt, type, (uint8_t *) &core->mac[RSSRK]);
}

static void
igb_rss_parse_packet(IGBCore *core, struct igb_rx *rx)
{
    trace_igb_rx_rss_started();

    if (!rx->info.rss.enabled) {
        rx->info.rss.hash = 0;
        rx->info.rss.type = 0;
        trace_igb_rx_rss_disabled();
        return;
    }

    rx->info.rss.type = igb_rss_get_hash_type(core, rx);
    trace_igb_rx_rss_type(rx->info.rss.type);

    if (rx->info.rss.type == E1000_MRQ_RSS_TYPE_NONE) {
        rx->info.rss.hash = 0;
        rx->info.queue = 0;
        return;
    }

    rx->info.rss.hash = igb_rss_calc_hash(core, rx);
    rx->info.queue = E1000_RSS_QUEUE(&core->mac[RETA], rx->info.rss.hash);
}

static void
igb_tx_setup_switch(IGBCore *core, struct igb_tx *tx, int queue_index)
{
    IGBCore *pfCore = (pci_is_vf(core->owner) ? core->pf : core);
    uint8_t pool = (pci_is_vf(core->owner) ?
        pcie_sriov_vf_number(core->owner) :
        pcie_sriov_get_num_vfs(core->owner));
    tx->info.mrqc_type = pfCore->mac[MRQC] & 0x07;
    tx->info.vmdq.enabled = (tx->info.mrqc_type == E1000_MRQC_ENABLE_VMDQ ||
        tx->info.mrqc_type == E1000_MRQC_ENABLE_VMDQ_RSS_MQ);
    tx->info.vmdq.replicate = pfCore->mac[VT_CTL] & E1000_VT_CTL_VM_REPL_EN;
    tx->info.vmdq.num_pools = pcie_sriov_get_num_vfs(pfCore->owner) + 1;

    size_t tot_len = net_tx_pkt_get_total_len(tx->tx_pkt);

    if ((pfCore->phy[0][PHY_CTRL] & MII_CR_LOOPBACK) ||
            ((pfCore->mac[RCTL] & E1000_RCTL_LBM_MAC) == E1000_RCTL_LBM_MAC)) {
        tx->info.local = true;
        tx->info.external = false;
        return;
    } else {
        tx->info.local = false;
        tx->info.external = true;
    }

    if (tx->info.vmdq.enabled) {
        bool vmdq_loopback_enabled =
            (pfCore->mac[DTXSWC] & E1000_DTXSWC_VMDQ_LOOPBACK_EN);
        struct eth_header *l2hdr = net_tx_pkt_get_l2hdr(tx->tx_pkt);
        eth_pkt_types_e pkt_type = net_tx_pkt_get_packet_type(tx->tx_pkt);
        bool tagged = e1000x_is_vlan_packet((uint8_t *)(l2hdr), pfCore->mac[VET]);

        igb_rx_uc_mac_filter(pfCore, &l2hdr->h_dest[0], &tx->info.vmdq.dpool_select);
        igb_rx_uc_mac_filter(pfCore, &l2hdr->h_source[0], &tx->info.vmdq.spool_select);
        /* MAC anti-spoof check failed */
        /* TODO: VLAN anti-spoof check */
        if (!(tx->info.vmdq.spool_select & BIT(pool)) &&
                (pfCore->mac[DTXSWC] >> E1000_DTXSWC_MAC_SPOOF_SHIFT) & BIT(pool)) {
            pfCore->mac[WVBR] |= BIT(pool + queue_index * 8);
            pfCore->mac[ICR] |= E1000_ICS_MDDET;
            igb_update_interrupt_state(pfCore);
            return;
        }
        if (vmdq_loopback_enabled) {
            tx->info.external = (pkt_type == ETH_PKT_UCAST) ?
                !tx->info.vmdq.dpool_select : true;
            if (!tx->info.vmdq.dpool_select) {
                for (int dpool = 0; dpool < tx->info.vmdq.num_pools; dpool++) {
                    uint32_t vmolr = pfCore->mac[VMOLR0 + dpool];
                    /* broadcast promiscuous */
                    if ((vmolr & E1000_VMOLR_BAM) &&
                            tx->info.vmdq.replicate &&
                            pkt_type == ETH_PKT_BCAST) {
                        tx->info.vmdq.dpool_select |= BIT(dpool);
                    }
                    /* unicast hash */
                    if ((vmolr & E1000_VMOLR_ROPE) &&
                            pkt_type == ETH_PKT_UCAST &&
                            igb_rx_uta_filter(core, (uint8_t *)l2hdr)) {
                        tx->info.vmdq.dpool_select |= BIT(dpool);
                    }
                    /* multicast hash */
                    if ((vmolr & E1000_VMOLR_ROMPE) &&
                            tx->info.vmdq.replicate &&
                            pkt_type == ETH_PKT_MCAST &&
                            igb_rx_mta_filter(core, (uint8_t *)l2hdr)) {
                        tx->info.vmdq.dpool_select |= BIT(dpool);
                    }
                    /* multicast promiscuous */
                    if ((vmolr & E1000_VMOLR_MPME) &&
                            tx->info.vmdq.replicate &&
                            pkt_type == ETH_PKT_MCAST) {
                        tx->info.vmdq.dpool_select |= BIT(dpool);
                    }
                }
            }
            /* same src/dst pool, only loopback if DTXSWC.LLE is true */
            if ((tx->info.vmdq.dpool_select & BIT(pool)) &&
                    !((pfCore->mac[DTXSWC] >> E1000_DTXSWC_LLE_SHIFT) & BIT(pool))) {
                tx->info.vmdq.dpool_select &= ~BIT(pool);
            }
            if (e1000x_vlan_rx_filter_enabled(pfCore->mac)) {
                uint8_t pool_select = 0;
                if (!tagged) {
                    for (int dpool = 0; dpool < tx->info.vmdq.num_pools; dpool++) {
                        if (pfCore->mac[VMOLR0 + dpool] & E1000_VMOLR_AUPE) {
                            pool_select |= BIT(dpool);
                        }
                    }
                } else {
                    uint16_t vid = lduw_be_p((uint8_t *)(l2hdr) + ETH_HLEN) & 0xfff;
                    for (int i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
                        uint32_t vlvf = pfCore->mac[VLVF0 + i];
                        if ((vlvf & E1000_VLVF_VLANID_ENABLE) &&
                                ((vlvf & E1000_VLVF_VLANID_MASK) == vid))  {
                            pool_select = (vlvf >> E1000_VLVF_POOLSEL_SHIFT) &
                                E1000_VLVF_POOLSEL_MASK;
                            break;
                        }
                    }
                }
                tx->info.vmdq.dpool_select &= pool_select;
            }
            if (tx->info.vmdq.dpool_select) {
                int dpool = 0;
                core->mac[VFGOTLBC] += tot_len;
                core->mac[VFGPTLBC]++;
                for (; dpool < (tx->info.vmdq.num_pools - 1); dpool++) {
                    if ((tx->info.vmdq.dpool_select & BIT(dpool)) &&
                            pfCore->vfs[dpool]) {
                        pfCore->vfs[dpool]->mac[VFGORLBC] += tot_len;
                        pfCore->vfs[dpool]->mac[VFGPRLBC]++;
                    }
                }
                /* pf is the last pool */
                if (tx->info.vmdq.dpool_select & BIT(dpool)) {
                    pfCore->mac[VFGORLBC] += tot_len;
                    pfCore->mac[VFGPRLBC]++;
                }
                tx->info.local = true;
            }
        }
    }
}

static void
igb_tx_setup_offloads(IGBCore *core, struct igb_tx *tx, struct igb_tx_ctx *ctx)
{
    if (!ctx->valid) {
        trace_igb_wrn_ctx_unknown(ctx->idx);
        return;
    }

    if (tx->vlan_needed) {
        net_tx_pkt_setup_vlan_header_ex(tx->tx_pkt, ctx->vlan, core->mac[VET]);
    }

    if (tx->cptse) {
        net_tx_pkt_build_vheader(tx->tx_pkt, true, true, ctx->mss);
        net_tx_pkt_update_ip_checksums(tx->tx_pkt);
        e1000x_inc_reg_if_not_full(core->mac, TSCTC);
        return;
    }

    if (tx->sum_needed & E1000_TXD_POPTS_TXSM) {
        net_tx_pkt_build_vheader(tx->tx_pkt, false, true, 0);
    }

    if (tx->sum_needed & E1000_TXD_POPTS_IXSM) {
        net_tx_pkt_update_ip_hdr_checksum(tx->tx_pkt);
    }
}

static bool
igb_tx_pkt_send(IGBCore *core, struct igb_tx *tx, int queue_index)
{
    struct igb_tx_ctx *ctx = &core->tx_ctx[tx->ctx_id];
    int target_queue = (pci_is_vf(core->owner) ?
            (pcie_sriov_vf_number(core->owner) + queue_index * 8) :
            queue_index);
    IGBCore *pfCore = (pci_is_vf(core->owner) ? core->pf : core);
    NetClientState *queue = qemu_get_subqueue(pfCore->owner_nic,
        MIN(pfCore->max_queue_num, target_queue));
    bool ret_val = true;

    igb_tx_setup_offloads(pfCore, tx, ctx);
    igb_tx_setup_switch(core, tx, queue_index);
    net_tx_pkt_dump(tx->tx_pkt);

    if (tx->info.external) {
        if (!net_tx_pkt_send(tx->tx_pkt, queue)) {
            ret_val = false;
        }
    }
    if (tx->info.local) {
        if (!net_tx_pkt_send_loopback(tx->tx_pkt, queue)) {
            ret_val = false;
        }
    }
    return ret_val;
}

static void
igb_tx_update_stats(IGBCore *core, struct NetTxPkt *tx_pkt)
{
    bool pf = !pci_is_vf(core->owner);
    IGBCore *pfCore = (pf ? core : core->pf);

    static const int PTCregs[6] = { PTC64, PTC127, PTC255, PTC511,
                                    PTC1023, PTC1522 };

    size_t tot_len = net_tx_pkt_get_total_len(tx_pkt);

    e1000x_increase_size_stats(pfCore->mac, PTCregs, tot_len);
    e1000x_inc_reg_if_not_full(pfCore->mac, TPT);
    e1000x_grow_8reg_if_not_full(pfCore->mac, TOTL, tot_len);

    if (pf) {
        switch (net_tx_pkt_get_packet_type(tx_pkt)) {
        case ETH_PKT_BCAST:
            e1000x_inc_reg_if_not_full(core->mac, BPTC);
            break;
        case ETH_PKT_MCAST:
            e1000x_inc_reg_if_not_full(core->mac, MPTC);
            break;
        case ETH_PKT_UCAST:
            break;
        default:
            g_assert_not_reached();
        }

        core->mac[GPTC] = core->mac[TPT];
        core->mac[GOTCL] = core->mac[TOTL];
        core->mac[GOTCH] = core->mac[TOTH];
    } else {
        core->mac[VFGOTC] += tot_len;
        core->mac[VFGPTC]++;
    }
}

static void
igb_tx_pkt_reset(struct igb_tx *tx) {
    net_tx_pkt_reset(tx->tx_pkt);
    memset(&tx->info, 0, sizeof(tx->info));
    tx->is_first = true;
    tx->skip_current_pkt = false;
    tx->vlan_needed = false;
    tx->cptse = false;
    tx->sum_needed = false;
}

static void
igb_rx_pkt_reset(struct igb_rx *rx) {
    memset(&rx->info, 0, sizeof(rx->info));
    rx->mac_pif = false;
    rx->mac_match = false;
    rx->vlan_match = false;
    rx->eth_pkt_type = 0;
}

static void
igb_rx_pkt_clone(struct igb_rx *new_rx, struct igb_rx *rx) {
    memcpy(&new_rx->info, &rx->info, sizeof(rx->info));
    new_rx->mac_pif = rx->mac_pif;
    new_rx->mac_match = rx->mac_match;
    new_rx->vlan_match = rx->vlan_match;
    new_rx->eth_pkt_type = rx->eth_pkt_type;
    net_rx_pkt_set_vhdr(new_rx->rx_pkt, net_rx_pkt_get_vhdr(rx->rx_pkt));
    net_rx_pkt_set_packet_type(new_rx->rx_pkt, rx->eth_pkt_type);
}

static void
igb_tx_process_tx_ctx_descr(struct e1000_adv_tx_context_desc *cp,
                         struct igb_tx_ctx *tx_ctx)
{
    tx_ctx->ip_len = cp->vlan_macip_lens & 0x1FF;
    tx_ctx->mac_len = (cp->vlan_macip_lens >> E1000_ADVTXD_MACLEN_SHIFT) & 0x7F;
    tx_ctx->vlan = (cp->vlan_macip_lens >> 16) & 0xFF;
    tx_ctx->tucmd = (cp->type_tucmd_mlhl >> E1000_ADVTXD_TUCMD_SHIFT) &
        E1000_ADVTXD_TUCMD_MASK;
    tx_ctx->idx = (cp->mss_l4len_idx >> E1000_ADVTXD_IDX_SHIFT) &
        E1000_ADVTXD_IDX_MASK;
    tx_ctx->l4_len = (cp->mss_l4len_idx >> E1000_ADVTXD_L4LEN_SHIFT) & 0xFF;
    tx_ctx->mss = (cp->mss_l4len_idx >> E1000_ADVTXD_MSS_SHIFT) & 0xFFFF;
    tx_ctx->valid = true;
}

static inline bool
igb_tx_insert_vlan(IGBCore *core, struct igb_tx *tx, bool dcmd_vle) {
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));
    struct igb_tx_ctx *ctx = &core->tx_ctx[tx->ctx_id];

    if (!pf && (core->pf->mac[VMVIR0 + vf_idx] & E1000_VMVIR_VLANA_DEFAULT)) {
        ctx->vlan = core->pf->mac[VMVIR0 + vf_idx] & 0x0fff;
        return e1000x_vlan_enabled(core->mac);
    }

    return dcmd_vle && e1000x_vlan_enabled(core->mac);
}

static void
igb_process_tx_desc(IGBCore *core,
                       struct igb_tx *tx,
                       uint8_t *desc,
                       bool *data,
                       int queue_index)
{
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));
    union e1000_adv_tx_desc *dp = (union e1000_adv_tx_desc *) desc;
    struct e1000_adv_tx_context_desc *cp;
    uint32_t cmd_type_len = le32_to_cpu(dp->read.cmd_type_len);
    uint32_t olinfo_status = le32_to_cpu(dp->read.olinfo_status);
    uint32_t dtype = cmd_type_len &
        (E1000_ADVTXD_DTYP_DATA | E1000_ADVTXD_DCMD_DEXT);
    size_t length = cmd_type_len & 0xFFFF;
    bool eop = cmd_type_len & E1000_ADVTXD_DCMD_EOP;
    uint64_t addr = le64_to_cpu(dp->read.buffer_addr);
    uint8_t ctx_id = (olinfo_status >> E1000_ADVTXD_IDX_SHIFT) &
        E1000_ADVTXD_IDX_MASK;

    if ((dtype & E1000_ADVTXD_DTYP_DATA) == E1000_ADVTXD_DTYP_DATA) {
        /* data descriptor */
        trace_igb_tx_data_desc((void *)(intptr_t)dp->read.buffer_addr,
            dp->read.cmd_type_len, dp->read.olinfo_status);
        *data = true;
        igb_process_ts_option(core, dp);
        if (tx->is_first) {
            tx->sum_needed = (olinfo_status >> E1000_ADVTXD_POPTS_SHIFT) &
                E1000_ADVTXD_POPTS_MASK;
            tx->cptse = (cmd_type_len & E1000_ADVTXD_DCMD_TSE) ? true : false;
            tx->vlan_needed = igb_tx_insert_vlan(core, tx, cmd_type_len &
                E1000_ADVTXD_DCMD_VLE);
            tx->is_first = false;
        }
    } else if ((dtype & E1000_ADVTXD_DTYP_DATA) == E1000_ADVTXD_DTYP_CTXT) {
        /* context descriptor */
        *data = false;
        cp = (struct e1000_adv_tx_context_desc *) desc;
        trace_igb_tx_ctxt_desc(cp->mss_l4len_idx, cp->type_tucmd_mlhl,
            cp->vlan_macip_lens);
        igb_tx_process_tx_ctx_descr(cp, &core->tx_ctx[ctx_id]);
        return;
    } else {
        /* legacy descriptor */
        *data = false;
        trace_igb_wrn_no_desctype_support((dtype & E1000_ADVTXD_DTYP_DATA));
        return;
    }

    if (!tx->skip_current_pkt) {
        if (!net_tx_pkt_add_raw_fragment(tx->tx_pkt, addr, length)) {
            trace_igb_tx_skip_current_pkt(
                (void *)(intptr_t)dp->read.buffer_addr,
                pf, vf_idx);
            tx->skip_current_pkt = true;
        }
    }

    if (eop) {
        if (!tx->skip_current_pkt && net_tx_pkt_parse(tx->tx_pkt)) {
            if (igb_tx_pkt_send(core, tx, queue_index)) {
                trace_igb_tx_pkt_send_success(
                    (void *)(intptr_t)dp->read.buffer_addr,
                    pf, vf_idx, queue_index,
                    net_tx_pkt_get_total_len(tx->tx_pkt));
                igb_tx_update_stats(core, tx->tx_pkt);
            } else {
                trace_igb_tx_pkt_send_failure(
                    (void *)(intptr_t)dp->read.buffer_addr,
                    pf, vf_idx, queue_index);
            }
        }
        igb_tx_pkt_reset(tx);
    }
}

static inline uint32_t
igb_get_msix_vector_other_causes(IGBCore *core)
{
    int num_vectors = IGB_NUM_VECTORS(core->owner);
    int shift = (pci_is_vf(core->owner) ? 0 : 0x08);

    uint32_t vector = core->mac[IVAR_MISC_IGB] >> shift;
    if (IGB_IVAR_ENTRY_VALID(vector)) {
        if (IGB_IVAR_ENTRY_VEC(core->owner, vector) < num_vectors) {
            return BIT(IGB_IVAR_ENTRY_VEC(core->owner, vector));
        } else {
            trace_igb_wrn_msix_vec_wrong(vector);
        }
    } else {
        trace_igb_wrn_msix_invalid(vector);
    }

    return 0;
}

static inline uint32_t
igb_get_msix_vector(IGBCore *core, bool tx, uint16_t index)
{
    int num_vectors = IGB_NUM_VECTORS(core->owner);
    int num_rows = (pci_is_vf(core->owner) ? 1 : 8);

    uint16_t col_off = tx ? 1 : 0;
    uint16_t col = index < num_rows ? col_off : col_off + 2;
    uint16_t row = index < num_rows ? index : index - num_rows;

    uint32_t vector = (core->mac[IVAR_IGB + row] >> (col * 8));
    if (IGB_IVAR_ENTRY_VALID(vector)) {
        if (IGB_IVAR_ENTRY_VEC(core->owner, vector) < num_vectors) {
            return BIT(IGB_IVAR_ENTRY_VEC(core->owner, vector));
        } else {
            trace_igb_wrn_msix_vec_wrong(vector);
        }
    } else {
        trace_igb_wrn_msix_invalid(vector);
    }

    return 0;
}

static inline uint32_t
igb_tx_wb_interrupt_cause(IGBCore *core, int queue_idx)
{
    if (!msix_enabled(core->owner)) {
        core->mac[ICR] |= E1000_ICR_TXDW;
        igb_update_interrupt_state(core);
        return 0;
    }

    return igb_get_msix_vector(core, true, queue_idx);
}

static inline uint32_t
igb_rx_wb_interrupt_cause(IGBCore *core, int queue_idx)
{
    if (!msix_enabled(core->owner)) {
        core->mac[ICR] |= E1000_ICS_RXDW;
        igb_update_interrupt_state(core);
        return 0;
    }

    return igb_get_msix_vector(core, false, queue_idx);
}

static uint32_t
igb_txdesc_writeback(IGBCore *core, dma_addr_t base,
                     void *desc, int queue_idx)
{
    union e1000_adv_tx_desc *dp = (union e1000_adv_tx_desc *) desc;
    uint32_t cmd_type_len = le32_to_cpu(dp->read.cmd_type_len);
    uint32_t olinfo_status = le32_to_cpu(dp->read.olinfo_status);

    if (!(cmd_type_len & E1000_ADVTXD_DCMD_RS)) {
        return 0;
    }

    olinfo_status |= E1000_ADVTXD_STA_DD;

    dp->read.olinfo_status = cpu_to_le32(olinfo_status);
    pci_dma_write(core->owner, base + ((char *)&dp->wb - (char *)dp),
                  &dp->wb, sizeof(dp->wb));
    return igb_tx_wb_interrupt_cause(core, queue_idx);
}

static inline bool
igb_ring_empty(IGBCore *core, const IGBRingInfo *r)
{
    return core->mac[r->dh] == core->mac[r->dt] ||
        core->mac[r->dt] >= core->mac[r->dlen] / E1000_RING_DESC_LEN;
}

static inline uint64_t
igb_ring_base(IGBCore *core, const IGBRingInfo *r)
{
    uint64_t bah = core->mac[r->dbah];
    uint64_t bal = core->mac[r->dbal];

    return (bah << 32) + bal;
}

static inline uint64_t
igb_ring_head_descr(IGBCore *core, const IGBRingInfo *r)
{
    return igb_ring_base(core, r) + E1000_RING_DESC_LEN * core->mac[r->dh];
}

static inline void
igb_ring_advance(IGBCore *core, const IGBRingInfo *r, uint32_t count)
{
    core->mac[r->dh] += count;

    if (core->mac[r->dh] * E1000_RING_DESC_LEN >= core->mac[r->dlen]) {
        core->mac[r->dh] = 0;
    }
}

static inline uint32_t
igb_ring_free_descr_num(IGBCore *core, const IGBRingInfo *r)
{
    trace_igb_ring_free_space(r->idx, core->mac[r->dlen],
                                 core->mac[r->dh],  core->mac[r->dt]);

    if (core->mac[r->dh] <= core->mac[r->dt]) {
        return core->mac[r->dt] - core->mac[r->dh];
    }

    if (core->mac[r->dh] > core->mac[r->dt]) {
        return core->mac[r->dlen] / E1000_RING_DESC_LEN +
               core->mac[r->dt] - core->mac[r->dh];
    }

    g_assert_not_reached();
    return 0;
}

static inline bool
igb_ring_enabled(IGBCore *core, const IGBRingInfo *r)
{
    return core->mac[r->dlen] > 0;
}

static inline uint32_t
igb_ring_len(IGBCore *core, const IGBRingInfo *r)
{
    return core->mac[r->dlen];
}

static inline int
igb_mq_queue_idx(int base_reg_idx, int reg_idx)
{
    if (reg_idx < 0x0C000) {
        return (reg_idx - base_reg_idx) / 0x100;
    } else if (base_reg_idx < 0x03000) {
        return (reg_idx - base_reg_idx - (0x0C000 - 0x02800)) / 0x40;
    }
    return (reg_idx - base_reg_idx - (0x0E000 - 0x03800)) / 0x40;
}

static inline bool
igb_tx_enabled(IGBCore *core, int qidx) {
    bool enabled = (pci_is_vf(core->owner) ?
        (core->pf->mac[VFTE] & BIT(pcie_sriov_vf_number(core->owner))) :
        (core->mac[TCTL] & E1000_TCTL_EN));

    return enabled &&
        (core->mac[E1000_TXDCTL_REG(qidx) >> 2] & E1000_TXDCTL_QUEUE_ENABLE);
}

static void
igb_start_xmit(IGBCore *core, int qidx)
{
    bool data_desc = true;
    dma_addr_t base;
    uint8_t desc[sizeof(union e1000_adv_tx_desc)];
    const IGBRingInfo *txi = &core->tx_ring_info[qidx];
    uint32_t cause = 0;
    uint32_t txqe = E1000_ICS_TXQE;

    if (!igb_tx_enabled(core, qidx)) {
        trace_igb_tx_disabled();
        return;
    }

    trace_igb_tx_start_xmit();
    while (!igb_ring_empty(core, txi)) {
        base = igb_ring_head_descr(core, txi);

        pci_dma_read(core->owner, base, &desc[0], sizeof(desc));

        igb_process_tx_desc(core, &core->tx[qidx], &desc[0], &data_desc, txi->idx);
        if (data_desc) {
            cause |= igb_txdesc_writeback(core, base, &desc[0], txi->idx);
        }

        /* TX queue not empty */
        txqe = 0;
        igb_ring_advance(core, txi, 1);
    }

    trace_igb_tx_interrupt_set(cause, txqe);
    core->mac[ICR] |= txqe;
    igb_set_interrupt_cause(core, cause);
}

static bool
igb_has_rxbufs(IGBCore *core, const IGBRingInfo *r,
                  size_t total_size)
{
    uint32_t bufs = igb_ring_free_descr_num(core, r);

    trace_igb_rx_has_buffers(r->idx, bufs, total_size,
                                core->rx_desc_buf_size[r->idx]);

    return total_size <= bufs * core->rx_desc_buf_size[r->idx];
}

static void
igb_parse_rxbufsize(IGBCore *core, int qidx, uint32_t default_rxbuf_size)
{
    uint32_t srrctl = core->mac[E1000_SRRCTL_REG(qidx) >> 2];
    if (srrctl & E1000_SRRCTL_BSIZEPKT_MASK) {
        core->rx_desc_buf_size[qidx] =
            (srrctl & E1000_SRRCTL_BSIZEPKT_MASK) << E1000_SRRCTL_BSIZEPKT_SHIFT;
    } else {
        core->rx_desc_buf_size[qidx] = default_rxbuf_size;
    }
    trace_igb_rx_desc_buff_size(qidx, core->rx_desc_buf_size[qidx]);
}

void
igb_start_recv(IGBCore *core)
{
    IGBCore *c = (pci_is_vf(core->owner) ? core->pf : core);
    if (!e1000x_rx_ready(c->owner, c->mac)) {
        trace_igb_rx_disabled();
        return;
    }

    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        igb_parse_rxbufsize(core, i, e1000x_rxbufsize(c->mac[RCTL]));
    }

    trace_igb_rx_start_recv();
    for (int i = 0; i <= c->max_queue_num; i++) {
        qemu_flush_queued_packets(qemu_get_subqueue(c->owner_nic, i));
    }
}

bool
igb_can_receive(IGBCore *core)
{
    int i;

    if (!e1000x_rx_ready(core->owner, core->mac)) {
        trace_igb_rx_disabled();
        return false;
    }

    for (i = 0; i < IGB_NUM_QUEUES; i++) {
        IGBRingInfo *rxi = &core->rx_ring_info[i];
        if (igb_ring_enabled(core, rxi) &&
                igb_has_rxbufs(core, rxi, 1) &&
                core->mac[E1000_RXDCTL_REG(rxi->idx) >> 2] & E1000_RXDCTL_QUEUE_ENABLE) {
            trace_igb_rx_can_recv();
            return true;
        }
    }

    trace_igb_rx_can_recv_rings_full();
    return false;
}

ssize_t
igb_receive(IGBCore *core, const uint8_t *buf, size_t size)
{
    const struct iovec iov = {
        .iov_base = (uint8_t *)buf,
        .iov_len = size
    };

    return igb_receive_iov(core, &iov, 1);
}

static bool
igb_rx_uc_mac_filter(IGBCore *core, const uint8_t *buf, uint8_t *pool_select)
{
    uint32_t ra[2];

    for (int i = 0; i < IGB_RAR_ENTRIES; ++i) {
        uint32_t rah = core->mac[E1000_RAH(i) >> 2];
        uint32_t ral = core->mac[E1000_RAL(i) >> 2];
        if (!(rah & E1000_RAH_AV)) {
            continue;
        }
        ra[0] = cpu_to_le32(ral);
        ra[1] = cpu_to_le32(rah);
        if (!memcmp(buf, (uint8_t *)ra, 6)) {
            trace_igb_rx_filter_ucast_match(i, MAC_ARG(buf));
            core->rx.mac_match = true;
            if (pool_select) {
                *pool_select |= (rah & E1000_RAH_POOL_MASK) >> E1000_RAH_POOL_SHIFT;
            }
            return true;
        }
    }
    trace_igb_rx_filter_ucast_mismatch(MAC_ARG(buf));
    return false;
}

static inline uint16_t
igb_rx_get_mta_lookup_idx(IGBCore *core, uint8_t *buf) {
    uint16_t addr_bits = (buf[5] << 8) | buf[4];
    int rctl_mo = (core->mac[RCTL] >> E1000_RCTL_MO_SHIFT) & 0x03;
    int addr_shift[] = {4, 3, 2, 0};

    return (addr_bits >> addr_shift[rctl_mo]) & 0xfff;
}

static bool
igb_rx_uta_filter(IGBCore *core, uint8_t *buf)
{
    uint16_t uta_idx = igb_rx_get_mta_lookup_idx(core, buf);
    if (core->mac[UTA + (uta_idx >> 5)] & BIT(uta_idx & 0x1f)) {
        trace_igb_rx_filter_uta_match(MAC_ARG(buf), uta_idx);
        return true;
    }
    trace_igb_rx_filter_uta_mismatch(MAC_ARG(buf), uta_idx);
    return false;
}

static bool
igb_rx_mta_filter(IGBCore *core, uint8_t *buf)
{
    uint16_t mta_idx = igb_rx_get_mta_lookup_idx(core, buf);
    if (core->mac[MTA + (mta_idx >> 5)] & BIT(mta_idx & 0x1f)) {
        trace_igb_rx_filter_mta_match(MAC_ARG(buf), mta_idx);
        return true;
    }
    trace_igb_rx_filter_mta_mismatch(MAC_ARG(buf), mta_idx);
    return false;
}

static void
igb_rx_mac_filter(IGBCore *core, uint8_t *buf)
{
    uint32_t rctl = core->mac[RCTL];
    uint32_t vmolr;

    switch (core->rx.info.mrqc_type) {
    case E1000_MRQC_ENABLE_VMDQ_RSS_MQ:
        core->rx.info.rss.enabled = true;
        /* fallthrough */
    case E1000_MRQC_ENABLE_VMDQ:
        core->rx.info.vmdq.enabled = true;
        core->rx.info.vmdq.replicate = core->mac[VT_CTL] & E1000_VT_CTL_VM_REPL_EN;
        if (core->mac[VT_CTL] & E1000_VT_CTL_IGNORE_MAC) {
            trace_igb_rx_filter_ignore_mac();
            core->rx.mac_match = true;
            break;
        }
        if (igb_rx_uc_mac_filter(core, buf, &core->rx.info.vmdq.pool_select)) {
            break;
        }
        for (int pool = 0; pool < core->rx.info.vmdq.num_pools; pool++) {
            vmolr = core->mac[VMOLR0 + pool];
            /* broadcast promiscuous */
            if ((vmolr & E1000_VMOLR_BAM) &&
                    core->rx.info.vmdq.replicate &&
                    core->rx.eth_pkt_type == ETH_PKT_BCAST) {
                core->rx.mac_match = true;
                trace_igb_rx_filter_promisc_match(ETH_PKT_BCAST);
                core->rx.info.vmdq.pool_select |= BIT(pool);
            }
            /* unicast hash */
            if ((vmolr & E1000_VMOLR_ROPE) &&
                    core->rx.eth_pkt_type == ETH_PKT_UCAST) {
                core->rx.mac_pif = igb_rx_uta_filter(core, buf);
                core->rx.info.vmdq.pool_select |=
                    (core->rx.mac_pif ? BIT(pool) : 0);
            }
            /* multicast hash */
            if ((vmolr & E1000_VMOLR_ROMPE) &&
                    core->rx.info.vmdq.replicate &&
                    core->rx.eth_pkt_type == ETH_PKT_MCAST) {
                core->rx.mac_pif = igb_rx_mta_filter(core, buf);
                core->rx.info.vmdq.pool_select |=
                    (core->rx.mac_pif ? BIT(pool) : 0);
            }
            /* multicast promiscuous */
            if ((vmolr & E1000_VMOLR_MPME) &&
                    core->rx.info.vmdq.replicate &&
                    core->rx.eth_pkt_type == ETH_PKT_MCAST) {
                core->rx.mac_match = true;
                trace_igb_rx_filter_promisc_match(ETH_PKT_BCAST);
                core->rx.info.vmdq.pool_select |= BIT(pool);
            }
        }
        break;
    case E1000_MRQC_ENABLE_RSS_MQ:
        core->rx.info.rss.enabled = true;
        /* fallthrough */
    default:
        if (igb_rx_uc_mac_filter(core, buf, NULL)) {
            break;
        }
        switch (core->rx.eth_pkt_type) {
        case ETH_PKT_UCAST:
            if (rctl & E1000_RCTL_UPE) {
                trace_igb_rx_filter_promisc_match(ETH_PKT_UCAST);
                core->rx.mac_match = true;
            } else if (igb_rx_uta_filter(core, buf)) {
                core->rx.mac_pif = true;
            }
            break;
        case ETH_PKT_MCAST:
            if (rctl & E1000_RCTL_MPE) {
                trace_igb_rx_filter_promisc_match(ETH_PKT_UCAST);
                core->rx.mac_match = true;
            } else if (igb_rx_mta_filter(core, buf)) {
                core->rx.mac_pif = true;
            }
            break;
        case ETH_PKT_BCAST:
            if (rctl & E1000_RCTL_BAM) {
                trace_igb_rx_filter_promisc_match(ETH_PKT_UCAST);
                core->rx.mac_match = true;
            } else if (igb_rx_mta_filter(core, buf)) {
                core->rx.mac_pif = true;
            }
            break;
        default:
            g_assert_not_reached();
            break;
        }
        break;
    }
}

static bool
igb_rx_vfta_filter(IGBCore *core, const uint16_t vid)
{
    if (core->mac[VFTA + (vid >> 5)] & BIT(vid & 0x1f)) {
        trace_igb_rx_filter_vlan_mismatch(vid);
        return true;
    }

    trace_igb_rx_filter_vlan_mismatch(vid);
    return false;
}

static void
igb_rx_vlan_filter(IGBCore *core, const uint8_t *buf)
{
    uint32_t vlvf, vmolr;
    uint8_t pool_select = 0;
    bool tagged = e1000x_is_vlan_packet(buf, core->mac[VET]);
    uint16_t vid = lduw_be_p(buf + ETH_HLEN) & 0xfff;

    if (!e1000x_vlan_rx_filter_enabled(core->mac)) {
        /* VLAN filter disabled, allow all */
        core->rx.vlan_match = true;
        return;
    }

    switch (core->rx.info.mrqc_type) {
    case E1000_MRQC_ENABLE_VMDQ_RSS_MQ:
    case E1000_MRQC_ENABLE_VMDQ:
        if (!tagged) {
            for (int pool = 0; pool < core->rx.info.vmdq.num_pools; pool++) {
                vmolr = core->mac[VMOLR0 + pool];
                /* allow untagged */
                if (vmolr & E1000_VMOLR_AUPE) {
                    trace_igb_rx_filter_vlan_untagged_match();
                    pool_select |= BIT(pool);
                    core->rx.vlan_match = true;
                }
            }
            core->rx.info.vmdq.pool_select &= pool_select;
            break;
        } else if (igb_rx_vfta_filter(core, vid)) {
            core->rx.vlan_match = true;
        }
        for (int i = 0; i < E1000_VLVF_ARRAY_SIZE; i++) {
            vlvf = core->mac[VLVF0 + i];
            if ((vlvf & E1000_VLVF_VLANID_ENABLE) &&
                    ((vlvf & E1000_VLVF_VLANID_MASK) == vid))  {
                core->rx.info.vmdq.pool_select &=
                    (vlvf >> E1000_VLVF_POOLSEL_SHIFT) & E1000_VLVF_POOLSEL_MASK;
                break;
            }
        }
        break;
    default:
        /* allow untagged */
        if (!tagged) {
            trace_igb_rx_filter_vlan_untagged_match();
            core->rx.vlan_match = true;
        } else if (igb_rx_vfta_filter(core, vid)) {
            core->rx.vlan_match = true;
        }
        break;
    }
}

static void
igb_rx_vmdq_set_default_pool(IGBCore *core)
{
    uint32_t vt_ctl = core->mac[VT_CTL];
    uint8_t default_pool = (vt_ctl & E1000_VT_CTL_DISABLE_DEF_POOL) ? 0 :
        ((vt_ctl & E1000_VT_CTL_DEFAULT_POOL_MASK) >>
          E1000_VT_CTL_DEFAULT_POOL_SHIFT);

    if (core->rx.info.vmdq.enabled && !core->rx.info.vmdq.replicate) {
        switch (core->rx.eth_pkt_type) {
        case ETH_PKT_UCAST:
            if (!core->rx.info.vmdq.pool_select) {
                core->rx.info.vmdq.pool_select = default_pool;
            }
            break;
        case ETH_PKT_MCAST:
        case ETH_PKT_BCAST:
            if (!core->rx.info.vmdq.pool_select && !core->rx.mac_match) {
                core->rx.info.vmdq.pool_select = default_pool;
            }
            break;
        default:
            g_assert_not_reached();
            break;
        }
    }
}

static inline bool
igb_is_oversized(IGBCore *core, size_t size)
{
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));
    bool lpe = (pf ? core->mac[RCTL] & E1000_RCTL_LPE :
        core->pf->mac[VMOLR0 + vf_idx] & E1000_VMOLR_LPE);
    bool sbp = (pf ? core : core->pf)->mac[RCTL] & E1000_RCTL_SBP;
    int maximum_ethernet_vlan_size = 1522;
    int maximum_ethernet_lpe_size =
        (pf ? core->mac[RLPML] & E1000_VMOLR_RLPML_MASK :
        core->pf->mac[VMOLR0 + vf_idx] & E1000_VMOLR_RLPML_MASK);

    if (size > maximum_ethernet_lpe_size ||
        (size > maximum_ethernet_vlan_size && !lpe && !sbp)) {
        trace_e1000x_rx_oversized(size);
        return true;
    }

    return false;
}

static inline bool
igb_rx_l3_cso_enabled(IGBCore *core)
{
    return !!(core->mac[RXCSUM] & E1000_RXCSUM_IPOFLD);
}

static inline bool
igb_rx_l4_cso_enabled(IGBCore *core)
{
    return !!(core->mac[RXCSUM] & E1000_RXCSUM_TUOFLD);
}

static void
igb_verify_csum_in_sw(IGBCore *core,
                         struct NetRxPkt *pkt,
                         uint32_t *status_flags,
                         bool istcp, bool isudp)
{
    bool csum_valid;
    uint32_t csum_error;

    if (igb_rx_l3_cso_enabled(core)) {
        if (!net_rx_pkt_validate_l3_csum(pkt, &csum_valid)) {
            trace_igb_rx_metadata_l3_csum_validation_failed();
        } else {
            csum_error = csum_valid ? 0 : E1000_RXDEXT_STATERR_IPE;
            *status_flags |= E1000_RXD_STAT_IPCS | csum_error;
        }
    } else {
        trace_igb_rx_metadata_l3_cso_disabled();
    }

    if (!igb_rx_l4_cso_enabled(core)) {
        trace_igb_rx_metadata_l4_cso_disabled();
        return;
    }

    if (!net_rx_pkt_validate_l4_csum(pkt, &csum_valid)) {
        trace_igb_rx_metadata_l4_csum_validation_failed();
        return;
    }

    csum_error = csum_valid ? 0 : E1000_RXDEXT_STATERR_TCPE;

    if (istcp) {
        *status_flags |= E1000_RXD_STAT_TCPCS |
                         csum_error;
    } else if (isudp) {
        *status_flags |= E1000_RXD_STAT_TCPCS |
                         E1000_RXD_STAT_UDPCS |
                         csum_error;
    }
}

static void
igb_build_rx_metadata(IGBCore *core,
                         struct NetRxPkt *pkt,
                         const IGBRxQueueInfo *rx_queue_info,
                         uint32_t *rss, uint16_t *mrq,
                         uint32_t *status_flags,
                         uint16_t *ip_id,
                         uint16_t *vlan_tag)
{
    struct virtio_net_hdr *vhdr;
    bool isip4, isip6, istcp, isudp;
    uint32_t pkt_type;

    net_rx_pkt_get_protocols(pkt, &isip4, &isip6, &isudp, &istcp);
    trace_igb_rx_metadata_protocols(isip4, isip6, isudp, istcp);

    /* VLAN state */
    if (net_rx_pkt_is_vlan_stripped(pkt)) {
        *status_flags |= E1000_RXD_STAT_VP;
        *vlan_tag = cpu_to_le16(net_rx_pkt_get_vlan_tag(pkt));
        trace_igb_rx_metadata_vlan(*vlan_tag);
    }

    /* Packet parsing results */
    if ((core->mac[RXCSUM] & E1000_RXCSUM_PCSD) != 0) {
        if (rx_queue_info->rss.enabled) {
            *rss = cpu_to_le32(rx_queue_info->rss.hash);
            *mrq = cpu_to_le32(rx_queue_info->rss.type |
                (rx_queue_info->queue << 8));
            trace_igb_rx_metadata_rss(*rss, *mrq);
        }
    } else if (isip4) {
            *status_flags |= E1000_RXD_STAT_IPIDV;
            *ip_id = cpu_to_le16(net_rx_pkt_get_ip_id(pkt));
            trace_igb_rx_metadata_ip_id(*ip_id);
    }


    if (isip6 && (core->mac[RFCTL] & E1000_RFCTL_IPV6_DIS)) {
        trace_igb_rx_metadata_ipv6_filtering_disabled();
        pkt_type = E1000_RXD_PKT_MAC;
    } else if (istcp || isudp) {
        pkt_type = isip4 ? E1000_RXD_PKT_IP4_XDP : E1000_RXD_PKT_IP6_XDP;
    } else if (isip4 || isip6) {
        pkt_type = isip4 ? E1000_RXD_PKT_IP4 : E1000_RXD_PKT_IP6;
    } else {
        pkt_type = E1000_RXD_PKT_MAC;
    }

    *status_flags |= E1000_RXD_PKT_TYPE(pkt_type);
    trace_igb_rx_metadata_pkt_type(pkt_type);

    /* RX CSO information */
    if (isip6 && (core->mac[RFCTL] & E1000_RFCTL_IPV6_XSUM_DIS)) {
        trace_igb_rx_metadata_ipv6_sum_disabled();
        goto func_exit;
    }

    /* skip checksum validation for non IP packets */
    if (!isip4 && !isip6) {
        trace_igb_rx_metadata_no_l3_hdr();
        goto func_exit;
    }

    if (!net_rx_pkt_has_virt_hdr(pkt)) {
        trace_igb_rx_metadata_no_virthdr();
        igb_verify_csum_in_sw(core, pkt, status_flags, istcp, isudp);
        goto func_exit;
    }

    vhdr = net_rx_pkt_get_vhdr(pkt);

    if (!(vhdr->flags & VIRTIO_NET_HDR_F_DATA_VALID) &&
        !(vhdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
        trace_igb_rx_metadata_virthdr_no_csum_info();
        igb_verify_csum_in_sw(core, pkt, status_flags, istcp, isudp);
        goto func_exit;
    }

    if (igb_rx_l3_cso_enabled(core)) {
        *status_flags |= isip4 ? E1000_RXD_STAT_IPCS : 0;
    } else {
        trace_igb_rx_metadata_l3_cso_disabled();
    }

    if (igb_rx_l4_cso_enabled(core)) {
        if (istcp) {
            *status_flags |= E1000_RXD_STAT_TCPCS;
        } else if (isudp) {
            *status_flags |= E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS;
        }
    } else {
        trace_igb_rx_metadata_l4_cso_disabled();
    }

    trace_igb_rx_metadata_status_flags(*status_flags);

func_exit:
    *status_flags = cpu_to_le32(*status_flags);
}

static inline void
igb_pci_dma_write_rx_desc(IGBCore *core, dma_addr_t addr,
                             union e1000_adv_rx_desc *desc,
                             dma_addr_t len)
{
    PCIDevice *dev = core->owner;

    size_t offset = offsetof(union e1000_adv_rx_desc,
        wb.upper.status_error);
    uint32_t status = desc->wb.upper.status_error;

    desc->wb.upper.status_error &= ~E1000_RXD_STAT_DD;
    pci_dma_write(dev, addr, desc, len);

    if (status & E1000_RXD_STAT_DD) {
        desc->wb.upper.status_error = status;
        pci_dma_write(dev, addr + offset, &status, sizeof(status));
    }
}

static void
igb_rx_update_stats(IGBCore *core,
                       size_t data_size,
                       size_t data_fcs_size)
{
    bool pf = !pci_is_vf(core->owner);
    IGBCore *pfCore = (pf ? core : core->pf);

    int PRCregs[6] = { PRC64, PRC127, PRC255, PRC511,
                    PRC1023, PRC1522 };
    e1000x_increase_size_stats(pfCore->mac, PRCregs, data_fcs_size);
    e1000x_inc_reg_if_not_full(pfCore->mac, TPR);
    /*
     * TOR - Total Octets Received:
     * This register includes bytes received in a packet from the <Destination
     * Address> field through the <CRC> field, inclusively.
     * Always include FCS length (4) in size.
     */
    e1000x_grow_8reg_if_not_full(pfCore->mac, TORL, data_size + 4);

    if (pf) {
        core->mac[GPRC] = core->mac[TPR];
        core->mac[GORCL] = core->mac[TORL];
        core->mac[GORCH] = core->mac[TORH];

        switch (net_rx_pkt_get_packet_type(core->rx.rx_pkt)) {
        case ETH_PKT_BCAST:
            e1000x_inc_reg_if_not_full(core->mac, BPRC);
            break;

        case ETH_PKT_MCAST:
            e1000x_inc_reg_if_not_full(core->mac, MPRC);
            break;

        default:
            break;
        }
    } else {
        core->mac[VFGORC] += data_size + 4;
        core->mac[VFGPRC]++;
        if (net_rx_pkt_get_packet_type(core->rx.rx_pkt) == ETH_PKT_MCAST) {
            core->mac[VFMPRC]++;
        }
    }
}

static inline bool
igb_rx_descr_threshold_hit(IGBCore *core, const IGBRingInfo *rxi)
{
    uint32_t srrctl = core->mac[E1000_SRRCTL_REG(rxi->idx) >> 2];
    uint32_t rdmts = (srrctl & E1000_SRRCTL_RDMTS_MASK) >>
        E1000_SRRCTL_RDMTS_SHIFT;
    return igb_ring_free_descr_num(core, rxi) ==
           rdmts * (igb_ring_len(core, rxi) / core->rx_desc_len);
}

static bool
igb_rx_write_packet(IGBCore *core, struct NetRxPkt *pkt,
                             const IGBRingInfo *rxi,
                             const IGBRxQueueInfo *rx_queue_info,
                             size_t fcs_len)
{
    dma_addr_t base;
    union e1000_adv_rx_desc desc;
    size_t desc_size;
    size_t desc_offset = 0;
    size_t write_offset = 0;
    size_t iov_ofs = 0;

    struct iovec *iov = net_rx_pkt_get_iovec(pkt);
    size_t size = net_rx_pkt_get_total_len(pkt);
    size_t total_size = size + fcs_len;

    do {
        desc_size = total_size - desc_offset;
        if (desc_size > core->rx_desc_buf_size[rxi->idx]) {
            desc_size = core->rx_desc_buf_size[rxi->idx];
        }

        if (igb_ring_empty(core, rxi)) {
            return false;
        }

        base = igb_ring_head_descr(core, rxi);
        memset(&desc, 0, sizeof(desc));
        pci_dma_read(core->owner, base, &desc, core->rx_desc_len);
        trace_igb_rx_descr(rxi->idx, base, core->rx_desc_len);

        if (desc.read.pkt_addr) {
            hwaddr pkt_addr = le64_to_cpu(desc.read.pkt_addr);
            if (desc_offset < size) {
                size_t iov_copy;
                size_t copy_size = size - desc_offset;
                if (copy_size > core->rx_desc_buf_size[rxi->idx]) {
                    copy_size = core->rx_desc_buf_size[rxi->idx];
                }

                /* Copy packet payload */
                while (copy_size) {
                    iov_copy = MIN(copy_size, iov->iov_len - iov_ofs);

                    /* Copy packet payload */
                    pci_dma_write(core->owner, pkt_addr + write_offset,
                        iov->iov_base + iov_ofs, copy_size);

                    write_offset += iov_copy;
                    copy_size -= iov_copy;
                    iov_ofs += iov_copy;
                    if (iov_ofs == iov->iov_len) {
                        iov++;
                        iov_ofs = 0;
                    }
                }

                if (desc_offset + desc_size >= total_size) {
                    /* Simulate FCS checksum presence in the last descriptor */
                    static const uint32_t fcs_pad;
                    pci_dma_write(core->owner, pkt_addr + write_offset,
                        (const char *) &fcs_pad, fcs_len);
                }

            }
        } else { /* as per intel docs; skip descriptors with null buf addr */
            trace_igb_rx_null_descriptor();
        }

        desc_offset += desc_size;
        desc.wb.upper.length = cpu_to_le16(total_size);
        if (desc_offset >= total_size) {
            /* Mark end of packet in the last descriptor */
            desc.wb.upper.status_error =
                E1000_RXD_STAT_DD | E1000_RXD_STAT_EOP;
            desc.wb.upper.status_error |=
                (core->rx.mac_pif ? E1000_RXD_STAT_PIF : 0);
            igb_build_rx_metadata(core, pkt, rx_queue_info,
                                    &desc.wb.lower.hi_dword.rss,
                                    &desc.wb.lower.lo_dword.pkt_info,
                                    &desc.wb.upper.status_error,
                                    &desc.wb.lower.hi_dword.csum_ip.ip_id,
                                    &desc.wb.upper.vlan);
        } else {
            desc.wb.upper.status_error = E1000_RXD_STAT_DD;
        }
        igb_pci_dma_write_rx_desc(core, base, &desc, core->rx_desc_len);
        igb_ring_advance(core, rxi, 1);
    } while (desc_offset < total_size);

    igb_rx_update_stats(core, size, total_size);
    return true;
}

static inline void
igb_rx_fix_l4_csum(IGBCore *core, struct NetRxPkt *pkt)
{
    if (net_rx_pkt_has_virt_hdr(pkt)) {
        struct virtio_net_hdr *vhdr = net_rx_pkt_get_vhdr(pkt);

        if (vhdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
            net_rx_pkt_fix_l4_csum(pkt);
        }
    }
}

static inline bool
igb_rx_queue_packet(IGBCore *core, size_t fcs_len)
{
    const IGBRingInfo *rxi = &core->rx_ring_info[core->rx.info.queue];
    size_t size = net_rx_pkt_get_total_len(core->rx.rx_pkt);
    size_t total_size = size + fcs_len;
    bool retval = false;
    uint32_t n = 0;
    uint32_t l = 0;
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));

    if (!(core->mac[E1000_RXDCTL_REG(rxi->idx) >> 2] & E1000_RXDCTL_QUEUE_ENABLE)) {
        trace_igb_rx_rss_not_dispatched_to_queue(pf, vf_idx, rxi->idx);
        return 0;
    }

    trace_igb_rx_rss_dispatched_to_queue(pf, vf_idx, rxi->idx);

    if (igb_has_rxbufs(core, rxi, total_size)) {
        igb_rx_fix_l4_csum(core, core->rx.rx_pkt);
        retval = igb_rx_write_packet(core, core->rx.rx_pkt, rxi, &core->rx.info, fcs_len);

        /* Check if receive descriptor minimum threshold hit */
        if (igb_rx_descr_threshold_hit(core, rxi)) {
            l |= E1000_ICS_RXDMT0;
        }
        n |= igb_rx_wb_interrupt_cause(core, rxi->idx);

        trace_igb_rx_written_to_guest(n, l);
    } else {
        l |= E1000_ICS_RXO;
        trace_igb_rx_not_written_to_guest(n, l);
    }

    trace_igb_rx_interrupt_set(n, l);

    core->mac[ICR] |= l;
    igb_set_interrupt_cause(core, n);

    return retval;
}

ssize_t
igb_receive_iov(IGBCore *core, const struct iovec *iov, int iovcnt)
{
    uint8_t min_buf[ETH_ZLEN];
    struct iovec min_iov;
    uint8_t *filter_buf;
    size_t size, orig_size;
    ssize_t retval;
    size_t iov_ofs = 0;
    size_t fcs_len = e1000x_fcs_len(core->mac);

    trace_igb_rx_receive_iov(iovcnt, iov_size(iov, iovcnt));

    if (!e1000x_hw_rx_enabled(core->mac)) {
        return -1;
    }

    /* Pull virtio header in */
    if (core->has_vnet) {
        net_rx_pkt_set_vhdr_iovec(core->rx.rx_pkt, iov, iovcnt);
        iov_ofs = sizeof(struct virtio_net_hdr);
    }

    filter_buf = iov->iov_base + iov_ofs;
    retval = orig_size = iov_size(iov, iovcnt);
    size = orig_size - iov_ofs;

    if (size < sizeof(min_buf)) {
        e1000x_inc_reg_if_not_full(core->mac, RUC);
        /* Pad to minimum Ethernet frame length */
        iov_to_buf(iov, iovcnt, iov_ofs, min_buf, size);
        memset(&min_buf[size], 0, sizeof(min_buf) - size);
        min_iov.iov_base = filter_buf = min_buf;
        min_iov.iov_len = size = sizeof(min_buf);
        iovcnt = 1;
        iov = &min_iov;
        iov_ofs = 0;
    } else if (iov->iov_len < ETH_MAX_L2_HDR_LEN) {
        /* This is very unlikely, but may happen. */
        iov_to_buf(iov, iovcnt, iov_ofs, min_buf, ETH_MAX_L2_HDR_LEN);
        filter_buf = min_buf;
    }

    core->rx.eth_pkt_type = get_eth_packet_type(PKT_GET_ETH_HDR(filter_buf));
    net_rx_pkt_set_packet_type(core->rx.rx_pkt, core->rx.eth_pkt_type);

    /* rx filter and pool selection */
    core->rx.info.mrqc_type = core->mac[MRQC] & 0x07;
    /* pf is the last pool if VMDq is enabled */
    core->rx.info.vmdq.num_pools = pcie_sriov_get_num_vfs(core->owner) + 1;
    trace_igb_rx_filter_queue_info(core->rx.info.mrqc_type);
    igb_rx_mac_filter(core, filter_buf);
    igb_rx_vlan_filter(core, filter_buf);
    igb_rx_vmdq_set_default_pool(core);

    trace_igb_rx_filter_status(core->rx.mac_pif, core->rx.mac_match, core->rx.vlan_match);
    /* check mac/vlan filter results */
    if ((!core->rx.mac_pif && !core->rx.mac_match) ||
        !core->rx.vlan_match) {
        trace_igb_rx_filter_dropped();
        goto out;
    }

    if (core->rx.info.vmdq.enabled) {
        uint8_t oversized = 0;
        int pool = 0;
        if (!core->rx.info.vmdq.pool_select) {
            trace_igb_rx_vmdq_no_pool_selected();
            goto out;
        }
        trace_igb_rx_vmdq_pool_selected(core->rx.info.vmdq.pool_select);

        for (; pool < (core->rx.info.vmdq.num_pools - 1); pool++) {
            if ((core->rx.info.vmdq.pool_select & BIT(pool)) &&
                    (core->mac[VFRE] & BIT(pool))) {
                IGBCore *vfCore = core->vfs[pool];
                bool strip_vlan = (core->mac[VMOLR0 + pool] & E1000_VMOLR_STRVLAN) |
                    (core->rx.eth_pkt_type == ETH_PKT_MCAST ?
                        core->mac[RPLOLR] & E1000_RPLOLR_STRVLAN : 0);

                if (igb_is_oversized(vfCore, size)) {
                    oversized |= BIT(pool);
                    continue;
                }
                igb_rx_pkt_clone(&vfCore->rx, &core->rx);
                net_rx_pkt_attach_iovec_ex(vfCore->rx.rx_pkt, iov, iovcnt, iov_ofs,
                                        strip_vlan, core->mac[VET]);
                vfCore->rx.info.rss.enabled = core->mac[VMOLR0 + pool] & E1000_VMOLR_RSSE;
                igb_rss_parse_packet(core, &vfCore->rx);
                if (!igb_rx_queue_packet(vfCore, fcs_len)) {
                    retval = 0;
                    continue;
                }
            }
        }

        /* also send to pf if needed, pf is always the last pool */
        if (core->rx.info.vmdq.pool_select & BIT(pool)) {
            if (igb_is_oversized(core, size)) {
                oversized |= BIT(pool);
            } else {
                net_rx_pkt_attach_iovec_ex(core->rx.rx_pkt, iov, iovcnt, iov_ofs,
                    e1000x_vlan_enabled(core->mac), core->mac[VET]);
                igb_rss_parse_packet(core, &core->rx);
                /* adjust PF queue number when VMDq is enabled */
                core->rx.info.queue = pool + core->rx.info.queue * 8;
                if (!igb_rx_queue_packet(core, fcs_len)) {
                    retval = 0;
                }
            }
        }

        /* increment ROC if oversized for all pools */
        if (oversized == core->rx.info.vmdq.pool_select) {
            e1000x_inc_reg_if_not_full(core->mac, ROC);
        }
    } else {
        if (igb_is_oversized(core, size)) {
            e1000x_inc_reg_if_not_full(core->mac, ROC);
            goto out;
        }
        net_rx_pkt_attach_iovec_ex(core->rx.rx_pkt, iov, iovcnt, iov_ofs,
            e1000x_vlan_enabled(core->mac), core->mac[VET]);
        igb_rss_parse_packet(core, &core->rx);
        if (!igb_rx_queue_packet(core, fcs_len)) {
            retval = 0;
        }
    }

out:
    igb_rx_pkt_reset(&core->rx);
    return retval;
}

static inline bool
igb_have_autoneg(IGBCore *core)
{
    return !pci_is_vf(core->owner) && core->phy[0][PHY_CTRL] & MII_CR_AUTO_NEG_EN;
}

static void igb_update_flowctl_status(IGBCore *core)
{
    if (igb_have_autoneg(core) &&
        core->phy[0][PHY_STATUS] & MII_SR_AUTONEG_COMPLETE) {
        trace_igb_link_autoneg_flowctl(true);
        core->mac[CTRL] |= E1000_CTRL_TFCE | E1000_CTRL_RFCE;
    } else {
        trace_igb_link_autoneg_flowctl(false);
    }
}

static inline void
igb_link_down(IGBCore *core)
{
    e1000x_update_regs_on_link_down(core->mac, core->phy[0]);
    igb_update_flowctl_status(core);
}

static inline void
igb_set_phy_ctrl(IGBCore *core, int index, uint16_t val)
{
    /* bits 0-5 reserved; MII_CR_[RESTART_AUTO_NEG,RESET] are self clearing */
    core->phy[0][PHY_CTRL] = val & ~(0x3f |
                                     MII_CR_RESET |
                                     MII_CR_RESTART_AUTO_NEG);

    if ((val & MII_CR_RESTART_AUTO_NEG) &&
        igb_have_autoneg(core)) {
        e1000x_restart_autoneg(core->mac, core->phy[0], core->autoneg_timer);
    }
}

static void
igb_set_phy_oem_bits(IGBCore *core, int index, uint16_t val)
{
    core->phy[0][PHY_OEM_BITS] = val & ~BIT(10);

    if (val & BIT(10) &&
        igb_have_autoneg(core)) {
        e1000x_restart_autoneg(core->mac, core->phy[0], core->autoneg_timer);
    }
}

static void
igb_set_phy_page(IGBCore *core, int index, uint16_t val)
{
    core->phy[0][PHY_PAGE] = val & PHY_PAGE_RW_MASK;
}

void
igb_core_set_link_status(IGBCore *core)
{
    NetClientState *nc = qemu_get_queue(core->owner_nic);
    uint32_t old_status = core->mac[STATUS];

    trace_igb_link_status_changed(nc->link_down ? false : true);

    if (nc->link_down) {
        e1000x_update_regs_on_link_down(core->mac, core->phy[0]);
    } else {
        if (igb_have_autoneg(core) &&
            !(core->phy[0][PHY_STATUS] & MII_SR_AUTONEG_COMPLETE)) {
            e1000x_restart_autoneg(core->mac, core->phy[0],
                                   core->autoneg_timer);
        } else {
            e1000x_update_regs_on_link_up(core->mac, core->phy[0]);
            igb_start_recv(core);
        }
    }

    if (core->mac[STATUS] != old_status) {
        core->mac[ICR] |= E1000_ICR_LSC;
        igb_update_interrupt_state(core);
    }
}

static void
igb_set_tctl(IGBCore *core, int index, uint32_t val)
{
    core->mac[TCTL] = val;
    trace_igb_tx_set_tctl(core->mac[TCTL]);
}

static void
igb_set_ctrl(IGBCore *core, int index, uint32_t val)
{
    trace_igb_core_ctrl_write(index, val);

    /* RST is self clearing */
    core->mac[CTRL] = val & ~E1000_CTRL_RST;
    core->mac[CTRL_DUP] = core->mac[CTRL];

    trace_igb_link_set_params(
        !!(val & E1000_CTRL_ASDE),
        (val & E1000_CTRL_SPD_SEL) >> E1000_CTRL_SPD_SHIFT,
        !!(val & E1000_CTRL_FRCSPD),
        !!(val & E1000_CTRL_FRCDPX),
        !!(val & E1000_CTRL_RFCE),
        !!(val & E1000_CTRL_TFCE));

    if (val & E1000_CTRL_RST) {
        if (!pci_is_vf(core->owner)) {
            trace_igb_core_ctrl_pf_sw_reset();
            e1000x_reset_mac_addr(core->owner_nic, core->mac, core->permanent_mac);
            for (int i = 0; i < pcie_sriov_get_num_vfs(core->owner); i++) {
                core->vfs[i]->mac[V2PMAILBOX0] |= E1000_V2PMAILBOX_RSTI;
                igb_set_interrupt_cause(core->vfs[i],
                    igb_get_msix_vector_other_causes(core->vfs[i]));
            }
        } else {
            int vf_idx = pcie_sriov_vf_number(core->owner);
            trace_igb_core_ctrl_vf_sw_reset(vf_idx);
            /* disable Rx and Tx for the VF*/
            core->pf->mac[VFTE] &= ~BIT(vf_idx);
            core->pf->mac[VFRE] &= ~BIT(vf_idx);
            /* indicate VF reset to PF */
            core->pf->mac[VFLRE] |= BIT(vf_idx);
            igb_update_interrupt_state(core->pf);
        }
    }

    if (val & E1000_CTRL_PHY_RST) {
        trace_igb_core_ctrl_phy_reset();
        core->mac[STATUS] |= E1000_STATUS_PHYRA;
    }
}

static void
igb_set_rfctl(IGBCore *core, int index, uint32_t val)
{
    trace_igb_rx_set_rfctl(val);

    if (!(val & E1000_RFCTL_ISCSI_DIS)) {
        trace_igb_wrn_iscsi_filtering_not_supported();
    }

    if (!(val & E1000_RFCTL_NFSW_DIS)) {
        trace_igb_wrn_nfsw_filtering_not_supported();
    }

    if (!(val & E1000_RFCTL_NFSR_DIS)) {
        trace_igb_wrn_nfsr_filtering_not_supported();
    }

    core->mac[RFCTL] = val;
}

static void
igb_set_rctl(IGBCore *core, int index, uint32_t val)
{
    core->mac[RCTL] = val;
    trace_igb_rx_set_rctl(core->mac[RCTL]);

    igb_start_recv(core);
}

static
void(*igb_phyreg_writeops[IGB_PHY_PAGES][IGB_PHY_PAGE_SIZE])
(IGBCore *, int, uint16_t) = {
    [0] = {
        [PHY_CTRL]     = igb_set_phy_ctrl,
        [PHY_PAGE]     = igb_set_phy_page,
        [PHY_OEM_BITS] = igb_set_phy_oem_bits
    }
};

static inline void
igb_clear_ims_bits(IGBCore *core, uint32_t bits)
{
    trace_igb_irq_clear_ims(bits, core->mac[IMS], core->mac[IMS] & ~bits);
    core->mac[IMS] &= ~bits;
}

static inline bool
igb_postpone_interrupt(bool *interrupt_pending,
                           IgbIntrDelayTimer *timer)
{
    if (timer->running) {
        trace_igb_irq_postponed_by_xitr(timer->delay_reg << 2);

        *interrupt_pending = true;
        return true;
    }

    if (timer->core->mac[timer->delay_reg] != 0) {
        igb_intrmgr_rearm_timer(timer);
    }

    return false;
}

static inline bool
igb_eitr_should_postpone(IGBCore *core, int idx)
{
    return igb_postpone_interrupt(&core->eitr_intr_pending[idx],
                                     &core->eitr[idx]);
}

static void
igb_msix_notify_one(IGBCore *core, uint32_t cause, uint32_t vec)
{
    uint32_t effective_eiac;
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));

    if (!igb_eitr_should_postpone(core, vec)) {
        trace_igb_irq_msix_notify_vec(pf, vf_idx, vec);
        msix_notify(core->owner, vec);
    }

    if (core->mac[GPIE] & E1000_GPIE_EIAME) {
        trace_igb_irq_eiam_clear_eiame(core->mac[EIAM], cause);
        core->mac[EIAM] &= ~cause;
    }

    trace_igb_irq_eicr_clear_eiac(core->mac[EICR], core->mac[EIAC_IGB]);

    effective_eiac = core->mac[EIAC_IGB] & cause;

    core->mac[EICR] &= ~effective_eiac;
    core->msi_causes_pending &= ~effective_eiac;

    if (!(core->mac[GPIE] & E1000_GPIE_EIAME)) {
        core->mac[EIMS] &= ~effective_eiac;
    }
}

static void
igb_msix_notify(IGBCore *core, uint32_t causes)
{
    uint32_t cause = 0;
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (int vec = 0; vec < num_vectors; vec++) {
        cause = causes & BIT(vec);
        if (cause) {
            igb_msix_notify_one(core, cause, vec);
        }
    }
}

static void
igb_msix_clear(IGBCore *core, uint32_t causes)
{
    uint32_t cause = 0;
    int num_vectors = IGB_NUM_VECTORS(core->owner);

    for (int vec = 0; vec < num_vectors; vec++) {
        cause = causes & BIT(vec);
        if (cause) {
            msix_clr_pending(core->owner, vec);
        }
    }
}

static void
igb_send_msi(IGBCore *core, bool msix)
{
    uint32_t causes = core->mac[EICR] & core->mac[EIMS];

    core->msi_causes_pending &= causes;
    causes ^= core->msi_causes_pending;
    if (causes == 0) {
        return;
    }
    core->msi_causes_pending |= causes;

    if (msix) {
        igb_msix_notify(core, causes);
    } else {
        for (int vec = 0; vec < IGB_NUM_VECTORS(core->owner); vec++) {
            int cause = causes & BIT(vec);
            if (cause && !igb_eitr_should_postpone(core, vec)) {
                trace_igb_irq_msi_notify(cause);
                msi_notify(core->owner, vec);
            }
        }
    }
}

static void
igb_update_interrupt_state(IGBCore *core)
{
    bool interrupts_pending;
    bool is_msix = msix_enabled(core->owner);
    bool pf = !pci_is_vf(core->owner);
    uint8_t vf_idx = (pf ? 0 : pcie_sriov_vf_number(core->owner));
    uint32_t mbvfimr = core->mac[MBVFIMR] | (core->mac[MBVFIMR] << 16);

    if ((mbvfimr & core->mac[MBVFICR]) ||
            (core->mac[MBVFIMR] & core->mac[VFLRE])) {
        core->mac[ICR] |= E1000_ICR_VMMB;
    }

    trace_igb_irq_pending_mbvf_interrupts(pf, vf_idx,
        mbvfimr & core->mac[MBVFICR],
        core->mac[MBVFIMR] & core->mac[VFLRE],
        core->mac[VFLRE], core->mac[MBVFICR],
        mbvfimr);

    if (core->mac[IMS] & core->mac[ICR] & ~E1000_ICR_ASSERTED) {
        core->mac[ICR] |= E1000_ICR_ASSERTED;
        core->mac[EICR] |= igb_get_msix_vector_other_causes(core);
    }

    trace_igb_irq_pending_other_interrupts(pf, vf_idx,
        core->mac[ICR] & core->mac[IMS],
        core->mac[ICR], core->mac[IMS]);

    /*
     * Copied from igb/igb_ethtool.c
     * Reading EICS for EICR because they read the
     * same but EICS does not clear on read
     */
    core->mac[ICS] = core->mac[ICR];
    core->mac[EICS] = core->mac[EICR];

    interrupts_pending = (core->mac[EIMS] & core->mac[EICR]) ? true : false;
    if (!interrupts_pending) {
        core->msi_causes_pending = 0;
    }

    trace_igb_irq_pending_interrupts(pf, vf_idx,
                                        core->mac[EICR] & core->mac[EIMS],
                                        core->mac[EICR], core->mac[EIMS]);

    if (is_msix || msi_enabled(core->owner)) {
        if (interrupts_pending) {
            igb_send_msi(core, is_msix);
        }
    } else {
        if (interrupts_pending) {
            for (int vec = 0; vec < IGB_NUM_VECTORS(core->owner); vec++) {
                int cause = (core->mac[EIMS] & core->mac[EICR]) & BIT(vec);
                if (cause && !igb_eitr_should_postpone(core, vec)) {
                    igb_raise_legacy_irq(core);
                    break;
                }
            }
        } else {
            igb_lower_legacy_irq(core);
        }
    }
}

static void
igb_set_interrupt_cause(IGBCore *core, uint32_t val)
{
    trace_igb_irq_set_cause_entry(val, core->mac[EICR]);

    core->mac[EICR] |= val;

    trace_igb_irq_set_cause_exit(val, core->mac[EICR]);

    igb_update_interrupt_state(core);
}

static inline void
igb_autoneg_timer(void *opaque)
{
    IGBCore *core = opaque;
    if (!qemu_get_queue(core->owner_nic)->link_down) {
        e1000x_update_regs_on_autoneg_done(core->mac, core->phy[0]);
        igb_start_recv(core);

        igb_update_flowctl_status(core);
        /* signal link status change to the guest */
        core->mac[ICR] |= E1000_ICR_LSC;
        igb_update_interrupt_state(core);
    }
}

static inline uint16_t
igb_get_reg_index_with_offset(const uint16_t *mac_reg_access, hwaddr addr)
{
    uint16_t index = (addr & 0x1ffff) >> 2;
    return index + (mac_reg_access[index] & 0xfffe);
}

static const char igb_phy_regcap[IGB_PHY_PAGES][0x20] = {
    [0] = {
        [PHY_CTRL]          = PHY_ANYPAGE | PHY_RW,
        [PHY_STATUS]        = PHY_ANYPAGE | PHY_R,
        [PHY_ID1]           = PHY_ANYPAGE | PHY_R,
        [PHY_ID2]           = PHY_ANYPAGE | PHY_R,
        [PHY_AUTONEG_ADV]   = PHY_ANYPAGE | PHY_RW,
        [PHY_LP_ABILITY]    = PHY_ANYPAGE | PHY_R,
        [PHY_AUTONEG_EXP]   = PHY_ANYPAGE | PHY_R,
        [PHY_NEXT_PAGE_TX]  = PHY_ANYPAGE | PHY_RW,
        [PHY_LP_NEXT_PAGE]  = PHY_ANYPAGE | PHY_R,
        [PHY_1000T_CTRL]    = PHY_ANYPAGE | PHY_RW,
        [PHY_1000T_STATUS]  = PHY_ANYPAGE | PHY_R,
        [PHY_EXT_STATUS]    = PHY_ANYPAGE | PHY_R,
        [PHY_PAGE]          = PHY_ANYPAGE | PHY_RW,

        [PHY_COPPER_CTRL1]           = PHY_RW,
        [PHY_COPPER_STAT1]           = PHY_R,
        [PHY_COPPER_CTRL3]           = PHY_RW,
        [PHY_RX_ERR_CNTR]            = PHY_R,
        [PHY_OEM_BITS]               = PHY_RW,
        [PHY_BIAS_1]                 = PHY_RW,
        [PHY_BIAS_2]                 = PHY_RW,
        [IGP01E1000_PHY_PAGE_SELECT] = PHY_RW,
        [PHY_COPPER_INT_ENABLE]      = PHY_RW,
        [PHY_COPPER_STAT2]           = PHY_R,
        [PHY_COPPER_CTRL2]           = PHY_RW
    },
    [2] = {
        [PHY_MAC_CTRL1]         = PHY_RW,
        [PHY_MAC_INT_ENABLE]    = PHY_RW,
        [PHY_MAC_STAT]          = PHY_R,
        [PHY_MAC_CTRL2]         = PHY_RW
    },
    [3] = {
        [PHY_LED_03_FUNC_CTRL1] = PHY_RW,
        [PHY_LED_03_POL_CTRL]   = PHY_RW,
        [PHY_LED_TIMER_CTRL]    = PHY_RW,
        [PHY_LED_45_CTRL]       = PHY_RW
    },
    [5] = {
        [PHY_1000T_SKEW]        = PHY_R,
        [PHY_1000T_SWAP]        = PHY_R
    },
    [6] = {
        [PHY_CRC_COUNTERS]      = PHY_R
    }
};

static bool
igb_phy_reg_check_cap(IGBCore *core, uint32_t addr,
                         char cap, uint8_t *page)
{
    *page =
        (igb_phy_regcap[0][addr] & PHY_ANYPAGE) ? 0
                                                    : core->phy[0][PHY_PAGE];

    if (*page >= IGB_PHY_PAGES) {
        return false;
    }

    return igb_phy_regcap[*page][addr] & cap;
}

static void
igb_phy_reg_write(IGBCore *core, uint8_t page,
                     uint32_t addr, uint16_t data)
{
    assert(page < IGB_PHY_PAGES);
    assert(addr < IGB_PHY_PAGE_SIZE);

    if (igb_phyreg_writeops[page][addr]) {
        igb_phyreg_writeops[page][addr](core, addr, data);
    } else {
        core->phy[page][addr] = data;
    }
}

static void
igb_set_mdic(IGBCore *core, int index, uint32_t val)
{
    uint32_t data = val & E1000_MDIC_DATA_MASK;
    uint32_t addr = ((val & E1000_MDIC_REG_MASK) >> E1000_MDIC_REG_SHIFT);
    uint8_t page;

    if ((val & E1000_MDIC_PHY_MASK) >> E1000_MDIC_PHY_SHIFT != 1) { /* phy # */
        val = core->mac[MDIC] | E1000_MDIC_ERROR;
    } else if (val & E1000_MDIC_OP_READ) {
        if (!igb_phy_reg_check_cap(core, addr, PHY_R, &page)) {
            trace_igb_core_mdic_read_unhandled(page, addr);
            val |= E1000_MDIC_ERROR;
        } else {
            val = (val ^ data) | core->phy[page][addr];
            trace_igb_core_mdic_read(page, addr, val);
        }
    } else if (val & E1000_MDIC_OP_WRITE) {
        if (!igb_phy_reg_check_cap(core, addr, PHY_W, &page)) {
            trace_igb_core_mdic_write_unhandled(page, addr);
            val |= E1000_MDIC_ERROR;
        } else {
            trace_igb_core_mdic_write(page, addr, data);
            igb_phy_reg_write(core, page, addr, data);
        }
    }
    core->mac[MDIC] = val | E1000_MDIC_READY;

    if (val & E1000_MDIC_INT_EN) {
        core->mac[ICR] |= E1000_ICR_MDAC;
        igb_update_interrupt_state(core);
    }
}

static void
igb_set_rdt(IGBCore *core, int index, uint32_t val)
{
    int qidx = igb_mq_queue_idx(E1000_RDT_REG(0), (index << 2));
    core->mac[index] = val & 0xffff;

    trace_igb_rx_set_rdt(qidx, val);
    igb_start_recv(core);
}

static void
igb_set_srrctl(IGBCore *core, int index, uint32_t val)
{
    int qidx = igb_mq_queue_idx(E1000_SRRCTL_REG(0), (index << 2));
    core->mac[index] = val;

    trace_igb_rx_set_srrctl(qidx, val);
    if (!(core->mac[index] & E1000_SRRCTL_DESCTYPE_ADV_ONEBUF)) {
        trace_igb_wrn_no_desctype_support(val);
    }
}

static void
igb_set_status(IGBCore *core, int index, uint32_t val)
{
    if ((val & E1000_STATUS_PHYRA) == 0) {
        core->mac[index] &= ~E1000_STATUS_PHYRA;
    }
}

static void
igb_set_ctrlext(IGBCore *core, int index, uint32_t val)
{
    trace_igb_link_set_ext_params(!!(val & E1000_CTRL_EXT_ASDCHK),
                                     !!(val & E1000_CTRL_EXT_SPD_BYPS),
                                     !!(val & E1000_CTRL_EXT_PFRSTD));

    /* Zero self-clearing bits */
    val &= ~(E1000_CTRL_EXT_ASDCHK | E1000_CTRL_EXT_EE_RST);
    core->mac[CTRL_EXT] = val;

    if (!pci_is_vf(core->owner) && (core->mac[CTRL_EXT] & E1000_CTRL_EXT_PFRSTD)) {
        for (int i = 0; i < pcie_sriov_get_num_vfs(core->owner); i++) {
            core->vfs[i]->mac[V2PMAILBOX0] &= ~E1000_V2PMAILBOX_RSTI;
            core->vfs[i]->mac[V2PMAILBOX0] |= E1000_V2PMAILBOX_RSTD;
        }
    }
}

static void
igb_set_fcrth(IGBCore *core, int index, uint32_t val)
{
    core->mac[FCRTH] = val & 0xFFF8;
}

static void
igb_set_fcrtl(IGBCore *core, int index, uint32_t val)
{
    core->mac[FCRTL] = val & 0x8000FFF8;
}

static inline void
igb_set_16bit(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & 0xffff;
}

static void
igb_set_vet(IGBCore *core, int index, uint32_t val)
{
    core->mac[VET] = val & 0xffff;
    trace_igb_vlan_vet(core->mac[VET]);
}

static void
igb_set_dlen(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & E1000_XDLEN_IGB_MASK;
}

static void
igb_set_dbal(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & E1000_XDBAL_IGB_MASK;
}

static void
igb_set_tdt(IGBCore *core, int index, uint32_t val)
{
    int qidx = igb_mq_queue_idx(E1000_TDT_REG(0), (index << 2));

    core->mac[index] = val & 0xffff;

    igb_start_xmit(core, qidx);
}

static void
igb_set_ics(IGBCore *core, int index, uint32_t val)
{
    trace_igb_irq_write_ics(val);
    core->mac[ICR] |= val;
    igb_update_interrupt_state(core);
}

static void
igb_set_imc(IGBCore *core, int index, uint32_t val)
{
    trace_igb_irq_ims_clear_set_imc(val);
    igb_clear_ims_bits(core, val);
    igb_update_interrupt_state(core);
}

static void
igb_set_ims(IGBCore *core, int index, uint32_t val)
{
    uint32_t ims = core->mac[IMS];
    uint32_t ims_new = val & ~ims;

    core->mac[IMS] |= ims_new;

    if (ims_new & core->mac[ICR]) {
        igb_update_interrupt_state(core);
    }
}

static uint32_t
igb_mac_readreg(IGBCore *core, int index)
{
    return core->mac[index];
}

static uint32_t
igb_mac_ics_read(IGBCore *core, int index)
{
    trace_igb_irq_read_ics(core->mac[ICS]);
    return core->mac[ICS];
}

static uint32_t
igb_mac_ims_read(IGBCore *core, int index)
{
    trace_igb_irq_read_ims(core->mac[IMS]);
    return core->mac[IMS];
}

static uint32_t
igb_mac_swsm_read(IGBCore *core, int index)
{
    uint32_t val = core->mac[SWSM];
    core->mac[SWSM] = val | E1000_SWSM_SMBI;
    return val;
}

static uint32_t
igb_mac_eitr_read(IGBCore *core, int index)
{
    return core->eitr_guest_value[index - EITR_IGB];
}

static uint32_t
igb_mac_icr_read(IGBCore *core, int index)
{
    uint32_t ret = core->mac[ICR];
    uint32_t other_vec = igb_get_msix_vector_other_causes(core);

    trace_igb_irq_icr_read_entry(ret);

    if ((core->mac[GPIE] & E1000_GPIE_NSICR) ||
            (core->mac[ICS] == 0 ||
             core->mac[ICR] & E1000_ICR_ASSERTED)) {
        core->mac[ICS] = core->mac[ICR] = 0;
        core->mac[EICR] &= ~other_vec;
        igb_update_interrupt_state(core);
    }

    trace_igb_irq_icr_read_exit(core->mac[ICR]);
    return ret;
}

static uint32_t
igb_mac_read_clr4(IGBCore *core, int index)
{
    uint32_t ret = core->mac[index];

    core->mac[index] = 0;
    return ret;
}

static uint32_t
igb_mac_read_clr8(IGBCore *core, int index)
{
    uint32_t ret = core->mac[index];

    core->mac[index] = 0;
    core->mac[index - 1] = 0;
    return ret;
}

static uint32_t
igb_get_ctrl(IGBCore *core, int index)
{
    uint32_t val = core->mac[CTRL];

    trace_igb_link_read_params(
        !!(val & E1000_CTRL_ASDE),
        (val & E1000_CTRL_SPD_SEL) >> E1000_CTRL_SPD_SHIFT,
        !!(val & E1000_CTRL_FRCSPD),
        !!(val & E1000_CTRL_FRCDPX),
        !!(val & E1000_CTRL_RFCE),
        !!(val & E1000_CTRL_TFCE));

    return val;
}

static uint32_t
igb_get_status(IGBCore *core, int index)
{
    IGBCore *c = (pci_is_vf(core->owner) ? core->pf : core);
    uint32_t res = c->mac[STATUS];

    if (!(c->mac[CTRL] & E1000_CTRL_GIO_MASTER_DISABLE)) {
        res |= E1000_STATUS_GIO_MASTER_ENABLE;
    }

    if (c->mac[CTRL] & E1000_CTRL_FRCDPX) {
        res |= (c->mac[CTRL] & E1000_CTRL_FD) ? E1000_STATUS_FD : 0;
    } else {
        res |= E1000_STATUS_FD;
    }

    if ((c->mac[CTRL] & E1000_CTRL_FRCSPD) ||
        (c->mac[CTRL_EXT] & E1000_CTRL_EXT_SPD_BYPS)) {
        switch (c->mac[CTRL] & E1000_CTRL_SPD_SEL) {
        case E1000_CTRL_SPD_10:
            res |= E1000_STATUS_SPEED_10;
            break;
        case E1000_CTRL_SPD_100:
            res |= E1000_STATUS_SPEED_100;
            break;
        case E1000_CTRL_SPD_1000:
        default:
            res |= E1000_STATUS_SPEED_1000;
            break;
        }
    } else {
        res |= E1000_STATUS_SPEED_1000;
    }

    res |= ((pcie_sriov_get_num_vfs(c->owner) & 0x0F) << E1000_STATUS_NUM_VFS_SHIFT);

    trace_igb_link_status(
        !!(res & E1000_STATUS_LU),
        !!(res & E1000_STATUS_FD),
        (res & E1000_STATUS_SPEED_MASK) >> E1000_STATUS_SPEED_SHIFT,
        (res & E1000_STATUS_ASDV) >> E1000_STATUS_ASDV_SHIFT);

    return res;
}

static void
igb_mac_writereg(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val;
}

static void
igb_mac_setmacaddr(IGBCore *core, int index, uint32_t val)
{
    uint32_t macaddr[2];

    core->mac[index] = val;

    macaddr[0] = cpu_to_le32(core->mac[RA]);
    macaddr[1] = cpu_to_le32(core->mac[RA + 1]);
    qemu_format_nic_info_str(qemu_get_queue(core->owner_nic),
        (uint8_t *) macaddr);

    trace_igb_mac_set_sw(MAC_ARG(macaddr));
}

static void
igb_set_eecd(IGBCore *core, int index, uint32_t val)
{
    static const uint32_t ro_bits = E1000_EECD_PRES          |
                                    E1000_EECD_AUTO_RD       |
                                    E1000_EECD_SIZE_EX_MASK;

    core->mac[EECD] = (core->mac[EECD] & ro_bits) | (val & ~ro_bits);
}

static void
igb_set_eerd(IGBCore *core, int index, uint32_t val)
{
    uint32_t addr = (val >> E1000_EERW_ADDR_SHIFT) & E1000_EERW_ADDR_MASK;
    uint32_t flags = 0;
    uint32_t data = 0;

    if ((addr < IGB_EEPROM_SIZE) && (val & E1000_EERW_START)) {
        data = core->eeprom[addr];
        flags = E1000_EERW_DONE;
    }

    core->mac[EERD] = flags                           |
                      (addr << E1000_EERW_ADDR_SHIFT) |
                      (data << E1000_EERW_DATA_SHIFT);
}

static void
igb_set_eewr(IGBCore *core, int index, uint32_t val)
{
    uint32_t addr = (val >> E1000_EERW_ADDR_SHIFT) & E1000_EERW_ADDR_MASK;
    uint32_t data = (val >> E1000_EERW_DATA_SHIFT) & E1000_EERW_DATA_MASK;
    uint32_t flags = 0;

    if ((addr < IGB_EEPROM_SIZE) && (val & E1000_EERW_START)) {
        core->eeprom[addr] = data;
        flags = E1000_EERW_DONE;
    }

    core->mac[EERD] = flags                           |
                      (addr << E1000_EERW_ADDR_SHIFT) |
                      (data << E1000_EERW_DATA_SHIFT);
}

static void
igb_set_eitr(IGBCore *core, int index, uint32_t val)
{
    uint32_t interval = val & 0xffff;
    uint32_t eitr_num = index - EITR_IGB;

    trace_igb_irq_eitr_set(eitr_num, val);

    core->eitr_guest_value[eitr_num] = interval;
    core->mac[index] = interval;
}

static void
igb_set_icr(IGBCore *core, int index, uint32_t val)
{
    core->mac[ICR] &= ~val;
}

static void
igb_set_eicr(IGBCore *core, int index, uint32_t val)
{
    core->mac[EICR] &= ~val;
}

static void
igb_set_eics(IGBCore *core, int index, uint32_t val)
{
    uint32_t interrupts_pending;
    bool is_msix = msix_enabled(core->owner);

    core->mac[EICR] |= (val & E1000_EICR_MASK);
    interrupts_pending = core->mac[EICR] & core->mac[EIMS];
    if (interrupts_pending) {
        igb_send_msi(core, is_msix);
    }
}

static void
igb_set_eims(IGBCore *core, int index, uint32_t val)
{
    uint32_t eims = core->mac[EIMS];
    uint32_t eims_new;

    val &= E1000_EICR_MASK;
    eims_new = val & ~eims;

    core->mac[EIMS] |= eims_new;

    if (eims_new &&
        (core->mac[GPIE] & E1000_GPIE_PBA) &&
        msix_enabled(core->owner)) {
        igb_msix_clear(core, eims_new);
    }

    if (eims_new & core->mac[EICR]) {
        igb_update_interrupt_state(core);
    }
}

static void
igb_set_eimc(IGBCore *core, int index, uint32_t val)
{
    core->mac[EIMS] &= ~(val & E1000_EICR_MASK);
}

static void
igb_set_eiam(IGBCore *core, int index, uint32_t val)
{
    core->mac[EIAM] |= (val & E1000_EICR_MASK);
}

static void
igb_set_eiac(IGBCore *core, int index, uint32_t val)
{
    core->mac[EIAC_IGB] |= (val & E1000_EICR_MASK);
}

static void
igb_update_rx_offloads(IGBCore *core)
{
    int cso_state = igb_rx_l4_cso_enabled(core);

    trace_igb_rx_set_cso(cso_state);

    if (core->has_vnet && !pci_is_vf(core->owner)) {
        qemu_set_offload(qemu_get_queue(core->owner_nic)->peer,
                         cso_state, 0, 0, 0, 0);
    }
}

static void
igb_set_rxcsum(IGBCore *core, int index, uint32_t val)
{
    core->mac[RXCSUM] = val;
    igb_update_rx_offloads(core);
}

static void
igb_set_gcr(IGBCore *core, int index, uint32_t val)
{
    uint32_t ro_bits = core->mac[GCR] & E1000_GCR_RO_BITS;
    core->mac[GCR] = (val & ~E1000_GCR_RO_BITS) | ro_bits;
}

static void
igb_set_vflre(IGBCore *core, int index, uint32_t val) {
    core->mac[VFLRE] &= ~val;
}

static void
igb_set_mbvficr(IGBCore *core, int index, uint32_t val) {
    core->mac[index] &= ~val;
}

static void
igb_set_mbvfimr(IGBCore *core, int index, uint32_t val) {
    core->mac[index] = val & 0xFF;
}

static void
igb_set_pf_mbox(IGBCore *core, int index, uint32_t val) {
    int vf_idx = index - P2VMAILBOX0;
    IGBCore *vfCore = core->vfs[vf_idx];
    bool send_vf_interrupt = false;
    uint32_t ro_bits = core->mac[index] & E1000_P2VMAILBOX_RO_BITS;
    uint8_t msix_vector_other = igb_get_msix_vector_other_causes(vfCore);

    trace_igb_mbx_pf_write(vf_idx, val);
    if (vfCore) {
        if (val & E1000_P2VMAILBOX_STS) {
            vfCore->mac[V2PMAILBOX0] |= E1000_V2PMAILBOX_PFSTS;
            send_vf_interrupt = true;
        }
        if (val & E1000_P2VMAILBOX_ACK) {
            vfCore->mac[V2PMAILBOX0] |= E1000_V2PMAILBOX_PFACK;
            send_vf_interrupt = true;
        }
        if (val & E1000_P2VMAILBOX_PFU &&
            (vfCore->mac[V2PMAILBOX0] & E1000_V2PMAILBOX_VFU)) {
            val &= ~E1000_P2VMAILBOX_PFU;
        }
        if (val & E1000_P2VMAILBOX_RVFU) {
            vfCore->mac[V2PMAILBOX0] &= ~E1000_V2PMAILBOX_VFU;
            core->mac[MBVFICR] &= ~(E1000_MBVFICR_VFREQ_VF1 << vf_idx);
            core->mac[MBVFICR] &= ~(E1000_MBVFICR_VFACK_VF1 << vf_idx);
        }
    }
    core->mac[index] = (val & ~E1000_P2VMAILBOX_RO_BITS) | ro_bits;

    if (send_vf_interrupt && msix_vector_other) {
        igb_set_interrupt_cause(vfCore, msix_vector_other);
    }
}

static uint32_t
igb_get_pf_mbox(IGBCore *core, int index) {
    int vf_idx = index - P2VMAILBOX0;
    IGBCore *vfCore = core->vfs[vf_idx];
    uint32_t ret_val = core->mac[index] & ~E1000_P2VMAILBOX_WO_BITS;
    uint32_t vfu = 0;

    if (!pci_is_vf(core->owner) && vfCore) {
        /* VFU bit is mirrored from VFMailbox VFU */
        vfu = vfCore->mac[V2PMAILBOX0] & E1000_V2PMAILBOX_VFU;
    }
    trace_igb_mbx_pf_read(vf_idx, ret_val | vfu);
    return ret_val | vfu;
}

static void
igb_set_vf_mbox(IGBCore *core, int index, uint32_t val) {
    int vf_idx = pcie_sriov_vf_number(core->owner);
    IGBCore *pfCore = core->pf;
    bool send_pf_interrupt = false;
    uint32_t ro_bits = core->mac[index] & E1000_V2PMAILBOX_RO_BITS;

    trace_igb_mbx_vf_write(vf_idx, val);
    if (pfCore) {
        if (val & E1000_V2PMAILBOX_REQ) {
            pfCore->mac[MBVFICR] |= E1000_MBVFICR_VFREQ_VF1 << vf_idx;
            send_pf_interrupt = true;
        }
        if (val & E1000_V2PMAILBOX_ACK) {
            pfCore->mac[MBVFICR] |= E1000_MBVFICR_VFACK_VF1 << vf_idx;
            send_pf_interrupt = true;
        }
        if (val & E1000_V2PMAILBOX_VFU &&
            (pfCore->mac[P2VMAILBOX0 + vf_idx] & E1000_P2VMAILBOX_PFU)) {
            val &= ~E1000_V2PMAILBOX_VFU;
        }
    }
    core->mac[index] = (val & ~E1000_V2PMAILBOX_RO_BITS) | ro_bits;

    if (send_pf_interrupt) {
        igb_update_interrupt_state(pfCore);
    }
}

static uint32_t
igb_get_vf_mbox(IGBCore *core, int index) {
    int vf_idx = pcie_sriov_vf_number(core->owner);
    IGBCore *pfCore = core->pf;
    uint32_t ret_val = core->mac[index] & ~E1000_V2PMAILBOX_WO_BITS;
    uint32_t pfu = 0;

    if (pci_is_vf(core->owner) && pfCore) {
        /* PFU bit is mirrored from PFMailbox PFU */
        pfu = pfCore->mac[P2VMAILBOX0 + vf_idx] & E1000_P2VMAILBOX_PFU;
    }

    core->mac[index] &= ~E1000_V2PMAILBOX_R2C_BITS;
    trace_igb_mbx_vf_read(vf_idx, ret_val | pfu);
    return ret_val | pfu;
}

static void
igb_set_vmbmem(IGBCore *core, int index, uint32_t val) {
    int vf_idx = (index - VMBMEM0) / E1000_VFMAILBOX_SIZE;
    int off = (index - VMBMEM0) % E1000_VFMAILBOX_SIZE;
    if (pci_is_vf(core->owner)) {
        vf_idx = pcie_sriov_vf_number(core->owner);
        core->pf->mac[VMBMEM0 + (E1000_VFMAILBOX_SIZE * vf_idx) + off] = val;
        trace_igb_vmbmem_vf_write(vf_idx, off, val);
    } else {
        trace_igb_vmbmem_pf_write(vf_idx, off, val);
        core->mac[index] = val;
    }
}

static uint32_t
igb_get_vmbmem(IGBCore *core, int index) {
    int vf_idx = (index - VMBMEM0) / E1000_VFMAILBOX_SIZE;
    int off = (index - VMBMEM0) % E1000_VFMAILBOX_SIZE;
    int ret_val = core->mac[index];
    if (pci_is_vf(core->owner)) {
        vf_idx = pcie_sriov_vf_number(core->owner);
        ret_val = core->pf->mac[VMBMEM0 + (E1000_VFMAILBOX_SIZE * vf_idx) + off];
        trace_igb_vmbmem_vf_read(vf_idx, off, ret_val);
    } else {
        trace_igb_vmbmem_pf_read(vf_idx, off, ret_val);
    }
    return ret_val;
}

#define igb_getreg(x)    [x] = igb_mac_readreg
typedef uint32_t (*readops)(IGBCore *, int);
static const readops igb_macreg_readops[] = {
    igb_getreg(PBA),
    igb_getreg(WUFC),
    igb_getreg(MANC),
    igb_getreg(TOTL),
    igb_getreg(LATECOL),
    igb_getreg(SEQEC),
    igb_getreg(XONTXC),
    igb_getreg(WUS),
    igb_getreg(GORCL),
    igb_getreg(MGTPRC),
    igb_getreg(EERD),
    igb_getreg(EIAC_IGB),
    igb_getreg(MANC2H),
    igb_getreg(RXCSUM),
    igb_getreg(GSCL_3),
    igb_getreg(GSCN_2),
    igb_getreg(FCAH),
    igb_getreg(FCRTH),
    igb_getreg(FLOP),
    igb_getreg(FLASHT),
    igb_getreg(RXSTMPH),
    igb_getreg(TXSTMPL),
    igb_getreg(TIMADJL),
    igb_getreg(TNCRS),
    igb_getreg(RJC),
    igb_getreg(IAM),
    igb_getreg(GSCL_2),
    igb_getreg(FLSWDATA),
    igb_getreg(RXSATRH),
    igb_getreg(TIPG),
    igb_getreg(FLMNGCTL),
    igb_getreg(FLMNGCNT),
    igb_getreg(TSYNCTXCTL),
    igb_getreg(EXTCNF_SIZE),
    igb_getreg(EXTCNF_CTRL),
    igb_getreg(EEMNGDATA),
    igb_getreg(CTRL_EXT),
    igb_getreg(SYSTIMH),
    igb_getreg(EEMNGCTL),
    igb_getreg(FLMNGDATA),
    igb_getreg(TSYNCRXCTL),
    igb_getreg(LEDCTL),
    igb_getreg(TCTL),
    igb_getreg(ECOL),
    igb_getreg(DC),
    igb_getreg(RLEC),
    igb_getreg(XOFFTXC),
    igb_getreg(RFC),
    igb_getreg(RNBC),
    igb_getreg(MGTPTC),
    igb_getreg(TIMINCA),
    igb_getreg(RXCFGL),
    igb_getreg(MFUTP01),
    igb_getreg(FACTPS),
    igb_getreg(GSCL_1),
    igb_getreg(GSCN_0),
    igb_getreg(GCR2),
    igb_getreg(RDT1),
    igb_getreg(FCTTV),
    igb_getreg(EEWR),
    igb_getreg(FLSWCTL),
    igb_getreg(RXSATRL),
    igb_getreg(SYSTIML),
    igb_getreg(RXUDP),
    igb_getreg(TORL),
    igb_getreg(MCC),
    igb_getreg(WUC),
    igb_getreg(EECD),
    igb_getreg(MFUTP23),
    igb_getreg(FCRTV),
    igb_getreg(RCTL),
    igb_getreg(MDIC),
    igb_getreg(FCRUC),
    igb_getreg(VET),
    igb_getreg(SCC),
    igb_getreg(COLC),
    igb_getreg(CEXTERR),
    igb_getreg(XOFFRXC),
    igb_getreg(IPAV),
    igb_getreg(GOTCL),
    igb_getreg(MGTPDC),
    igb_getreg(GCR),
    igb_getreg(POEMB),
    igb_getreg(MFVAL),
    igb_getreg(FUNCTAG),
    igb_getreg(GSCL_4),
    igb_getreg(GSCN_3),
    igb_getreg(MRQC),
    igb_getreg(RDLEN1),
    igb_getreg(FCT),
    igb_getreg(FLA),
    igb_getreg(FLOL),
    igb_getreg(RXSTMPL),
    igb_getreg(TXSTMPH),
    igb_getreg(TIMADJH),
    igb_getreg(FCRTL),
    igb_getreg(XONRXC),
    igb_getreg(TSCTFC),
    igb_getreg(RFCTL),
    igb_getreg(GSCN_1),
    igb_getreg(FCAL),
    igb_getreg(FLSWCNT),
    igb_getreg(DTXSWC),
    igb_getreg(WVBR),
    igb_getreg(RLPML),
    igb_getreg(MBVFICR),
    igb_getreg(MBVFIMR),
    igb_getreg(VFLRE),
    igb_getreg(VFRE),
    igb_getreg(VFTE),
    igb_getreg(VT_CTL),
    igb_getreg(VFGPRC),
    igb_getreg(VFGORC),
    igb_getreg(VFMPRC),
    igb_getreg(VFGPTC),
    igb_getreg(VFGOTC),
    igb_getreg(VFGOTLBC),
    igb_getreg(VFGPTLBC),
    igb_getreg(VFGORLBC),
    igb_getreg(VFGPRLBC),
    [TOTH]    = igb_mac_read_clr8,
    [GOTCH]   = igb_mac_read_clr8,
    [PRC64]   = igb_mac_read_clr4,
    [PRC255]  = igb_mac_read_clr4,
    [PRC1023] = igb_mac_read_clr4,
    [PTC64]   = igb_mac_read_clr4,
    [PTC255]  = igb_mac_read_clr4,
    [PTC1023] = igb_mac_read_clr4,
    [GPRC]    = igb_mac_read_clr4,
    [TPT]     = igb_mac_read_clr4,
    [RUC]     = igb_mac_read_clr4,
    [BPRC]    = igb_mac_read_clr4,
    [MPTC]    = igb_mac_read_clr4,
    [IAC]     = igb_mac_read_clr4,
    [ICR]     = igb_mac_icr_read,
    [RDFH]    = IGB_LOW_BITS_READ(13),
    [RDFHS]   = IGB_LOW_BITS_READ(13),
    [RDFPC]   = IGB_LOW_BITS_READ(13),
    [TDFH]    = IGB_LOW_BITS_READ(13),
    [TDFHS]   = IGB_LOW_BITS_READ(13),
    [STATUS]  = igb_get_status,
    [PBS]     = IGB_LOW_BITS_READ(6),
    [ICS]     = igb_mac_ics_read,
    [AIT]     = IGB_LOW_BITS_READ(16),
    [TORH]    = igb_mac_read_clr8,
    [GORCH]   = igb_mac_read_clr8,
    [PRC127]  = igb_mac_read_clr4,
    [PRC511]  = igb_mac_read_clr4,
    [PRC1522] = igb_mac_read_clr4,
    [PTC127]  = igb_mac_read_clr4,
    [PTC511]  = igb_mac_read_clr4,
    [PTC1522] = igb_mac_read_clr4,
    [GPTC]    = igb_mac_read_clr4,
    [TPR]     = igb_mac_read_clr4,
    [ROC]     = igb_mac_read_clr4,
    [MPRC]    = igb_mac_read_clr4,
    [BPTC]    = igb_mac_read_clr4,
    [TSCTC]   = igb_mac_read_clr4,
    [RDFT]    = IGB_LOW_BITS_READ(13),
    [RDFTS]   = IGB_LOW_BITS_READ(13),
    [TDFPC]   = IGB_LOW_BITS_READ(13),
    [TDFT]    = IGB_LOW_BITS_READ(13),
    [TDFTS]   = IGB_LOW_BITS_READ(13),
    [CTRL]    = igb_get_ctrl,
    [SWSM]    = igb_mac_swsm_read,
    [IMS]     = igb_mac_ims_read,

    [CRCERRS ... MPC]      = igb_mac_readreg,
    [IP6AT ... IP6AT + 3]  = igb_mac_readreg,
    [IP4AT ... IP4AT + 6]  = igb_mac_readreg,
    [RA ... RA + 31]       = igb_mac_readreg,
    [RA2 ... RA2 + 15]     = igb_mac_readreg,
    [WUPM ... WUPM + 31]   = igb_mac_readreg,
    [UTA ... UTA + 127]    = igb_mac_readreg,
    [MTA ... MTA + 127]    = igb_mac_readreg,
    [VFTA ... VFTA + 127]  = igb_mac_readreg,
    [FFMT ... FFMT + 254]  = IGB_LOW_BITS_READ(4),
    [FFVT ... FFVT + 254]  = igb_mac_readreg,
    [MDEF ... MDEF + 7]    = igb_mac_readreg,
    [FFLT ... FFLT + 10]   = IGB_LOW_BITS_READ(11),
    [FTFT ... FTFT + 254]  = igb_mac_readreg,
    [PBM ... PBM + 10239]  = igb_mac_readreg,
    [RETA ... RETA + 31]   = igb_mac_readreg,
    [RSSRK ... RSSRK + 31] = igb_mac_readreg,
    [MAVTV0 ... MAVTV3]    = igb_mac_readreg,
    [EITR_IGB...EITR_IGB + IGB_MSIX_VEC_NUM - 1] = igb_mac_eitr_read,

    [FWSM]       = igb_mac_readreg,
    [SW_FW_SYNC] = igb_mac_readreg,
    [EICR]       = igb_mac_read_clr4,
    [EIMS]       = igb_mac_readreg,
    [EIAM]       = igb_mac_readreg,
    [IVAR_IGB ... IVAR_IGB + 7]     = igb_mac_readreg,
    [IVAR_MISC_IGB]                 = igb_mac_readreg,
    [RPLOLR]                        = igb_mac_readreg,
    [P2VMAILBOX0 ... P2VMAILBOX7]   = igb_get_pf_mbox,
    [V2PMAILBOX0 ... V2PMAILBOX7]   = igb_get_vf_mbox,
    [VMBMEM0 ... VMBMEM7 + (E1000_VFMAILBOX_SIZE - 1)] = igb_get_vmbmem,
    [VMOLR0 ... VMOLR7]             = igb_mac_readreg,
    [VLVF0 ... VLVF7]               = igb_mac_readreg,
    [VMVIR0 ... VMVIR7]             = igb_mac_readreg,
    [E1000_TDBAL_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TDBAL_REG(7) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TDBAH_REG(7) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TDLEN_REG(7) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TDH_REG(7) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TDT_REG(7) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(0) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(1) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(2) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(3) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(4) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(5) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(6) >> 2] = igb_mac_readreg,
    [E1000_TXDCTL_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RDBAL_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RDBAH_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RDLEN_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RDH_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RDT_REG(7) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(0) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(1) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(2) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(3) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(4) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(5) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(6) >> 2] = igb_mac_readreg,
    [E1000_RXDCTL_REG(7) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(0) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(1) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(2) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(3) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(4) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(5) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(6) >> 2] = igb_mac_readreg,
    [E1000_SRRCTL_REG(7) >> 2] = igb_mac_readreg
};
enum { IGB_NREADOPS = ARRAY_SIZE(igb_macreg_readops) };

#define igb_putreg(x)    [x] = igb_mac_writereg
typedef void (*writeops)(IGBCore *, int, uint32_t);
static const writeops igb_macreg_writeops[] = {
    igb_putreg(PBA),
    igb_putreg(SWSM),
    igb_putreg(WUFC),
    igb_putreg(LEDCTL),
    igb_putreg(FCAL),
    igb_putreg(FCRUC),
    igb_putreg(AIT),
    igb_putreg(TDFH),
    igb_putreg(TDFT),
    igb_putreg(TDFHS),
    igb_putreg(TDFTS),
    igb_putreg(TDFPC),
    igb_putreg(WUC),
    igb_putreg(WUS),
    igb_putreg(RDFH),
    igb_putreg(RDFT),
    igb_putreg(RDFHS),
    igb_putreg(RDFTS),
    igb_putreg(RDFPC),
    igb_putreg(IPAV),
    igb_putreg(TIMINCA),
    igb_putreg(IAM),
    igb_putreg(FLSWDATA),
    igb_putreg(POEMB),
    igb_putreg(PBS),
    igb_putreg(MFUTP01),
    igb_putreg(MFUTP23),
    igb_putreg(MANC),
    igb_putreg(MANC2H),
    igb_putreg(MFVAL),
    igb_putreg(EXTCNF_CTRL),
    igb_putreg(FACTPS),
    igb_putreg(FUNCTAG),
    igb_putreg(GSCL_1),
    igb_putreg(GSCL_2),
    igb_putreg(GSCL_3),
    igb_putreg(GSCL_4),
    igb_putreg(GSCN_0),
    igb_putreg(GSCN_1),
    igb_putreg(GSCN_2),
    igb_putreg(GSCN_3),
    igb_putreg(GCR2),
    igb_putreg(MRQC),
    igb_putreg(FLOP),
    igb_putreg(FLOL),
    igb_putreg(FLSWCTL),
    igb_putreg(FLSWCNT),
    igb_putreg(FLA),
    igb_putreg(TIPG),
    igb_putreg(RXSTMPH),
    igb_putreg(RXSTMPL),
    igb_putreg(RXSATRL),
    igb_putreg(RXSATRH),
    igb_putreg(TXSTMPL),
    igb_putreg(TXSTMPH),
    igb_putreg(SYSTIML),
    igb_putreg(SYSTIMH),
    igb_putreg(TIMADJL),
    igb_putreg(TIMADJH),
    igb_putreg(RXUDP),
    igb_putreg(RXCFGL),
    igb_putreg(TSYNCRXCTL),
    igb_putreg(TSYNCTXCTL),
    igb_putreg(EXTCNF_SIZE),
    igb_putreg(EEMNGCTL),
    igb_putreg(RA),
    igb_putreg(DTXSWC),
    igb_putreg(WVBR),
    igb_putreg(RLPML),
    igb_putreg(VFRE),
    igb_putreg(VFTE),
    igb_putreg(VT_CTL),
    igb_putreg(VFGPRC),
    igb_putreg(VFGORC),
    igb_putreg(VFMPRC),
    igb_putreg(VFGPTC),
    igb_putreg(VFGOTC),
    igb_putreg(VFGOTLBC),
    igb_putreg(VFGPTLBC),
    igb_putreg(VFGORLBC),
    igb_putreg(VFGPRLBC),
    [TCTL]     = igb_set_tctl,
    [MDIC]     = igb_set_mdic,
    [ICS]      = igb_set_ics,
    [IMC]      = igb_set_imc,
    [IMS]      = igb_set_ims,
    [ICR]      = igb_set_icr,
    [EECD]     = igb_set_eecd,
    [RCTL]     = igb_set_rctl,
    [CTRL]     = igb_set_ctrl,
    [EERD]     = igb_set_eerd,
    [GCR]      = igb_set_gcr,
    [RXCSUM]   = igb_set_rxcsum,
    [STATUS]   = igb_set_status,
    [CTRL_EXT] = igb_set_ctrlext,
    [FCAH]     = igb_set_16bit,
    [FCT]      = igb_set_16bit,
    [FCTTV]    = igb_set_16bit,
    [FCRTV]    = igb_set_16bit,
    [FCRTH]    = igb_set_fcrth,
    [FCRTL]    = igb_set_fcrtl,
    [VET]      = igb_set_vet,
    [FLASHT]   = igb_set_16bit,
    [EEWR]     = igb_set_eewr,
    [CTRL_DUP] = igb_set_ctrl,
    [RFCTL]    = igb_set_rfctl,
    [RA + 1]   = igb_mac_setmacaddr,

    [IP6AT ... IP6AT + 3]    = igb_mac_writereg,
    [IP4AT ... IP4AT + 6]    = igb_mac_writereg,
    [RA + 2 ... RA + 31]     = igb_mac_writereg,
    [RA2 ... RA2 + 15]       = igb_mac_writereg,
    [WUPM ... WUPM + 31]     = igb_mac_writereg,
    [UTA ... UTA + 127]      = igb_mac_writereg,
    [MTA ... MTA + 127]      = igb_mac_writereg,
    [VFTA ... VFTA + 127]    = igb_mac_writereg,
    [FFMT ... FFMT + 254]    = igb_mac_writereg,
    [FFVT ... FFVT + 254]    = igb_mac_writereg,
    [PBM ... PBM + 10239]    = igb_mac_writereg,
    [MDEF ... MDEF + 7]      = igb_mac_writereg,
    [FFLT ... FFLT + 10]     = igb_mac_writereg,
    [FTFT ... FTFT + 254]    = igb_mac_writereg,
    [RETA ... RETA + 31]     = igb_mac_writereg,
    [RSSRK ... RSSRK + 31]   = igb_mac_writereg,
    [MAVTV0 ... MAVTV3]      = igb_mac_writereg,
    [EITR_IGB...EITR_IGB + IGB_MSIX_VEC_NUM - 1] = igb_set_eitr,

    [FWSM]     = igb_mac_writereg,
    [SW_FW_SYNC] = igb_mac_writereg,
    [EICR] = igb_set_eicr,
    [EICS] = igb_set_eics,
    [EIAC_IGB] = igb_set_eiac,
    [EIAM] = igb_set_eiam,
    [EIMC] = igb_set_eimc,
    [EIMS] = igb_set_eims,
    [IVAR_IGB ... IVAR_IGB + 7]     = igb_mac_writereg,
    [IVAR_MISC_IGB]                 = igb_mac_writereg,
    [RPLOLR]                        = igb_mac_writereg,
    [MBVFICR]                       = igb_set_mbvficr,
    [MBVFIMR]                       = igb_set_mbvfimr,
    [VFLRE]                         = igb_set_vflre,
    [P2VMAILBOX0 ... P2VMAILBOX7]   = igb_set_pf_mbox,
    [V2PMAILBOX0 ... V2PMAILBOX7]   = igb_set_vf_mbox,
    [VMBMEM0 ... VMBMEM7 + (E1000_VFMAILBOX_SIZE - 1)] = igb_set_vmbmem,
    [VMOLR0 ... VMOLR7]             = igb_mac_writereg,
    [VLVF0 ... VLVF7]               = igb_mac_writereg,
    [VMVIR0 ... VMVIR7]             = igb_mac_writereg,
    [E1000_TDBAL_REG(0) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(1) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(2) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(3) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(4) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(5) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(6) >> 2] = igb_set_dbal,
    [E1000_TDBAL_REG(7) >> 2] = igb_set_dbal,
    [E1000_TDBAH_REG(0) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(1) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(2) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(3) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(4) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(5) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(6) >> 2] = igb_mac_writereg,
    [E1000_TDBAH_REG(7) >> 2] = igb_mac_writereg,
    [E1000_TDLEN_REG(0) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(1) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(2) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(3) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(4) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(5) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(6) >> 2] = igb_set_dlen,
    [E1000_TDLEN_REG(7) >> 2] = igb_set_dlen,
    [E1000_TDH_REG(0) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(1) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(2) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(3) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(4) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(5) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(6) >> 2] = igb_set_16bit,
    [E1000_TDH_REG(7) >> 2] = igb_set_16bit,
    [E1000_TDT_REG(0) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(1) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(2) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(3) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(4) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(5) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(6) >> 2] = igb_set_tdt,
    [E1000_TDT_REG(7) >> 2] = igb_set_tdt,
    [E1000_TXDCTL_REG(0) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(1) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(2) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(3) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(4) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(5) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(6) >> 2] = igb_mac_writereg,
    [E1000_TXDCTL_REG(7) >> 2] = igb_mac_writereg,
    [E1000_RDBAL_REG(0) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(1) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(2) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(3) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(4) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(5) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(6) >> 2] = igb_set_dbal,
    [E1000_RDBAL_REG(7) >> 2] = igb_set_dbal,
    [E1000_RDBAH_REG(0) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(1) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(2) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(3) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(4) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(5) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(6) >> 2] = igb_mac_writereg,
    [E1000_RDBAH_REG(7) >> 2] = igb_mac_writereg,
    [E1000_RDLEN_REG(0) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(1) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(2) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(3) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(4) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(5) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(6) >> 2] = igb_set_dlen,
    [E1000_RDLEN_REG(7) >> 2] = igb_set_dlen,
    [E1000_RDH_REG(0) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(1) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(2) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(3) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(4) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(5) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(6) >> 2] = igb_set_16bit,
    [E1000_RDH_REG(7) >> 2] = igb_set_16bit,
    [E1000_RDT_REG(0) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(1) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(2) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(3) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(4) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(5) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(6) >> 2] = igb_set_rdt,
    [E1000_RDT_REG(7) >> 2] = igb_set_rdt,
    [E1000_RXDCTL_REG(0) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(1) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(2) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(3) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(4) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(5) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(6) >> 2] = igb_mac_writereg,
    [E1000_RXDCTL_REG(7) >> 2] = igb_mac_writereg,
    [E1000_SRRCTL_REG(0) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(1) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(2) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(3) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(4) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(5) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(6) >> 2] = igb_set_srrctl,
    [E1000_SRRCTL_REG(7) >> 2] = igb_set_srrctl
};
enum { IGB_NWRITEOPS = ARRAY_SIZE(igb_macreg_writeops) };

enum { MAC_ACCESS_PARTIAL = 1 };

/*
 * The array below combines alias offsets of the index values for the
 * MAC registers that have aliases, with the indication of not fully
 * implemented registers (lowest bit). This combination is possible
 * because all of the offsets are even.
 */
static const uint16_t mac_reg_access[IGB_MAC_SIZE] = {
    /* Alias index offsets */
    [FCRTL_A] = 0x07fe, [FCRTH_A] = 0x0802,
    [RDH0_A]  = 0x09bc, [RDT0_A]  = 0x09bc,
    [RDFH_A]  = 0xe904, [RDFT_A]  = 0xe904,
    [TDH_A]   = 0x0cf8, [TDT_A]   = 0x0cf8,
    [TDFH_A]  = 0xed00, [TDFT_A]  = 0xed00,
    [RA_A ... RA_A + 31]      = 0x14f0,
    [VFTA_A ... VFTA_A + 127] = 0x1400,
    [RDBAL0_A ... RDLEN0_A] = 0x09bc,
    [TDBAL_A ... TDLEN_A]   = 0x0cf8,
    /* Access options */
    [RDFH]  = MAC_ACCESS_PARTIAL,    [RDFT]  = MAC_ACCESS_PARTIAL,
    [RDFHS] = MAC_ACCESS_PARTIAL,    [RDFTS] = MAC_ACCESS_PARTIAL,
    [RDFPC] = MAC_ACCESS_PARTIAL,
    [TDFH]  = MAC_ACCESS_PARTIAL,    [TDFT]  = MAC_ACCESS_PARTIAL,
    [TDFHS] = MAC_ACCESS_PARTIAL,    [TDFTS] = MAC_ACCESS_PARTIAL,
    [TDFPC] = MAC_ACCESS_PARTIAL,    [EECD]  = MAC_ACCESS_PARTIAL,
    [PBM]   = MAC_ACCESS_PARTIAL,    [FLA]   = MAC_ACCESS_PARTIAL,
    [FCAL]  = MAC_ACCESS_PARTIAL,    [FCAH]  = MAC_ACCESS_PARTIAL,
    [FCT]   = MAC_ACCESS_PARTIAL,    [FCTTV] = MAC_ACCESS_PARTIAL,
    [FCRTV] = MAC_ACCESS_PARTIAL,    [FCRTL] = MAC_ACCESS_PARTIAL,
    [FCRTH] = MAC_ACCESS_PARTIAL,    [MAVTV0 ... MAVTV3] = MAC_ACCESS_PARTIAL,
    [TXDCTL ... TXDCTL + IGB_NUM_QUEUES] = MAC_ACCESS_PARTIAL
};

void
igb_core_write(IGBCore *core, hwaddr addr, uint64_t val, unsigned size)
{
    uint16_t index = igb_get_reg_index_with_offset(mac_reg_access, addr);

    if (index < IGB_NWRITEOPS && igb_macreg_writeops[index]) {
        if (mac_reg_access[index] & MAC_ACCESS_PARTIAL) {
            trace_igb_wrn_regs_write_trivial(index << 2);
        }
        trace_igb_core_write(index << 2, size, val);
        igb_macreg_writeops[index](core, index, val);
    } else if (index < IGB_NREADOPS && igb_macreg_readops[index]) {
        trace_igb_wrn_regs_write_ro(index << 2, size, val);
    } else {
        trace_igb_wrn_regs_write_unknown(index << 2, size, val);
    }
}

uint64_t
igb_core_read(IGBCore *core, hwaddr addr, unsigned size)
{
    uint64_t val;
    uint16_t index = igb_get_reg_index_with_offset(mac_reg_access, addr);

    if (index < IGB_NREADOPS && igb_macreg_readops[index]) {
        if (mac_reg_access[index] & MAC_ACCESS_PARTIAL) {
            trace_igb_wrn_regs_read_trivial(index << 2);
        }
        val = igb_macreg_readops[index](core, index);
        trace_igb_core_read(index << 2, size, val);
        return val;
    } else {
        trace_igb_wrn_regs_read_unknown(index << 2, size);
    }
    return 0;
}

static inline void
igb_autoneg_pause(IGBCore *core)
{
    if (igb_have_autoneg(core)) {
        timer_del(core->autoneg_timer);
    }
}

static void
igb_autoneg_resume(IGBCore *core)
{
    if (igb_have_autoneg(core) &&
        !(core->phy[0][PHY_STATUS] & MII_SR_AUTONEG_COMPLETE)) {
        qemu_get_queue(core->owner_nic)->link_down = false;
        timer_mod(core->autoneg_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 500);
    }
}

static void
igb_vm_state_change(void *opaque, bool running, RunState state)
{
    IGBCore *core = opaque;

    if (running) {
        trace_igb_vm_state_running();
        igb_intrmgr_resume(core);
        igb_autoneg_resume(core);
    } else {
        trace_igb_vm_state_stopped();
        igb_autoneg_pause(core);
        igb_intrmgr_pause(core);
    }
}

void
igb_core_pci_realize(IGBCore        *core,
                     const uint16_t *eeprom_templ,
                     uint32_t        eeprom_size,
                     const uint8_t  *macaddr)
{
    core->rx_desc_len = sizeof(union e1000_adv_rx_desc);
    igb_intrmgr_pci_realize(core);

    core->vmstate =
        qemu_add_vm_change_state_handler(igb_vm_state_change, core);

    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        net_tx_pkt_init(&core->tx[i].tx_pkt, core->owner,
                        IGB_MAX_TX_FRAGS, core->has_vnet);
    }

    net_rx_pkt_init(&core->rx.rx_pkt, core->has_vnet);

    if (!pci_is_vf(core->owner)) {
        core->autoneg_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                    igb_autoneg_timer, core);
    }
    if (eeprom_templ) {
        e1000x_core_prepare_eeprom(core->eeprom,
                                   eeprom_templ,
                                   eeprom_size,
                                   PCI_DEVICE_GET_CLASS(core->owner)->device_id,
                                   macaddr);
    }
    igb_update_rx_offloads(core);

    /* setup tx and rx rings */
    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        core->tx_ring_info[i].dbah = E1000_TDBAH_REG(i) >> 2;
        core->tx_ring_info[i].dbal = E1000_TDBAL_REG(i) >> 2;
        core->tx_ring_info[i].dlen = E1000_TDLEN_REG(i) >> 2;
        core->tx_ring_info[i].dh = E1000_TDH_REG(i) >> 2;
        core->tx_ring_info[i].dt = E1000_TDT_REG(i) >> 2;
        core->tx_ring_info[i].idx = i;

        core->rx_ring_info[i].dbah = E1000_RDBAH_REG(i) >> 2;
        core->rx_ring_info[i].dbal = E1000_RDBAL_REG(i) >> 2;
        core->rx_ring_info[i].dlen = E1000_RDLEN_REG(i) >> 2;
        core->rx_ring_info[i].dh = E1000_RDH_REG(i) >> 2;
        core->rx_ring_info[i].dt = E1000_RDT_REG(i) >> 2;
        core->rx_ring_info[i].idx = i;
    }
}

void
igb_core_pci_uninit(IGBCore *core)
{
    int i;

    memset(&core->vfs, 0, sizeof(core->vfs));
    core->pf = NULL;

    if (igb_have_autoneg(core)) {
        timer_free(core->autoneg_timer);
    }

    igb_intrmgr_pci_unint(core);

    qemu_del_vm_change_state_handler(core->vmstate);

    for (i = 0; i < IGB_NUM_QUEUES; i++) {
        net_tx_pkt_reset(core->tx[i].tx_pkt);
        net_tx_pkt_uninit(core->tx[i].tx_pkt);
    }

    net_rx_pkt_uninit(core->rx.rx_pkt);
}

static const uint16_t
igb_phy_reg_init[IGB_PHY_PAGES][IGB_PHY_PAGE_SIZE] = {
    [0] = {
        [PHY_CTRL] =   MII_CR_SPEED_SELECT_MSB  |
                       MII_CR_FULL_DUPLEX       |
                       MII_CR_AUTO_NEG_EN,

        [PHY_STATUS] = MII_SR_EXTENDED_CAPS     |
                       MII_SR_LINK_STATUS       |
                       MII_SR_AUTONEG_CAPS      |
                       MII_SR_PREAMBLE_SUPPRESS |
                       MII_SR_EXTENDED_STATUS   |
                       MII_SR_10T_HD_CAPS       |
                       MII_SR_10T_FD_CAPS       |
                       MII_SR_100X_HD_CAPS      |
                       MII_SR_100X_FD_CAPS,

        [PHY_ID1]               = 0x2a8,
        [PHY_ID2]               = E1000_PHY_ID2_82576x,
        [PHY_AUTONEG_ADV]       = 0xde1,
        [PHY_LP_ABILITY]        = 0x7e0,
        [PHY_AUTONEG_EXP]       = BIT(2),
        [PHY_NEXT_PAGE_TX]      = BIT(0) | BIT(13),
        [PHY_1000T_CTRL]        = BIT(8) | BIT(9) | BIT(10) | BIT(11),
        [PHY_1000T_STATUS]      = 0x3c00,
        [PHY_EXT_STATUS]        = BIT(12) | BIT(13),

        [PHY_COPPER_CTRL1]      = BIT(5) | BIT(6) | BIT(8) | BIT(9) |
                                  BIT(12) | BIT(13),
        [PHY_COPPER_STAT1]      = BIT(3) | BIT(10) | BIT(11) | BIT(13) | BIT(15)
    },
    [2] = {
        [PHY_MAC_CTRL1]         = BIT(3) | BIT(7),
        [PHY_MAC_CTRL2]         = BIT(1) | BIT(2) | BIT(6) | BIT(12)
    },
    [3] = {
        [PHY_LED_TIMER_CTRL]    = BIT(0) | BIT(2) | BIT(14)
    }
};

static const uint32_t igb_mac_reg_init[] = {
    [PBA]           =     0x00140014,
    [LEDCTL]        =  BIT(1) | BIT(8) | BIT(9) | BIT(15) | BIT(17) | BIT(18),
    [EXTCNF_CTRL]   = BIT(3),
    [EEMNGCTL]      = BIT(18) | BIT(19) | BIT(31),
    [FLASHT]        = 0x2,
    [FLSWCTL]       = BIT(30) | BIT(31),
    [FLOL]          = BIT(0),
    [E1000_RXDCTL_REG(0) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(1) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(2) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(3) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(4) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(5) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(6) >> 2]  = BIT(16),
    [E1000_RXDCTL_REG(7) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(0) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(1) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(2) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(3) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(4) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(5) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(6) >> 2]  = BIT(16),
    [E1000_TXDCTL_REG(7) >> 2]  = BIT(16),
    [TIPG]          = 0x8 | (0x8 << 10) | (0x6 << 20),
    [RXCFGL]        = 0x88F7,
    [RXUDP]         = 0x319,
    [CTRL]          = E1000_CTRL_FD | E1000_CTRL_SWDPIN2 | E1000_CTRL_SWDPIN0 |
                      E1000_CTRL_SPD_1000 | E1000_CTRL_SLU |
                      E1000_CTRL_ADVD3WUC,
    [STATUS]        = E1000_STATUS_ASDV_1000 | E1000_STATUS_LU | E1000_STATUS_IOV_MODE,
    [EECD]          = E1000_EECD_AUTO_RD | E1000_EECD_PRES,
    [EERD]          = E1000_EERW_DONE,
    [EEWR]          = E1000_EERW_DONE,
    [GCR]           = E1000_L0S_ADJUST |
                      E1000_L1_ENTRY_LATENCY_MSB |
                      E1000_L1_ENTRY_LATENCY_LSB,
    [TDFH]          = 0x600,
    [TDFT]          = 0x600,
    [TDFHS]         = 0x600,
    [TDFTS]         = 0x600,
    [POEMB]         = 0x30D,
    [PBS]           = 0x028,
    [FACTPS]        = E1000_FACTPS_LAN0_ON | 0x20000000,
    [SWSM]          = 0,
    [VET]           = 0x8100,
    [RXCSUM]        = E1000_RXCSUM_IPOFLD | E1000_RXCSUM_TUOFLD,
    [RLPML]         = 0x2600,
    [MBVFIMR]       = 0XFF,
    [EITR_IGB...EITR_IGB + IGB_MSIX_VEC_NUM - 1] = IGB_START_EITR,
    [V2PMAILBOX0] = E1000_V2PMAILBOX_RSTI,
    [VMOLR0 ... VMOLR7] = 0x2600 | BIT(31),
};

void
igb_core_reset(IGBCore *core)
{
    int i;

    if (igb_have_autoneg(core)) {
        timer_del(core->autoneg_timer);
    }

    igb_intrmgr_reset(core);

    memset(core->phy, 0, sizeof core->phy);
    memmove(core->phy, igb_phy_reg_init, sizeof igb_phy_reg_init);
    memset(core->mac, 0, sizeof core->mac);
    memmove(core->mac, igb_mac_reg_init, sizeof igb_mac_reg_init);

    if (!pci_is_vf(core->owner)) {
        if (qemu_get_queue(core->owner_nic)->link_down) {
            igb_link_down(core);
        }
        e1000x_reset_mac_addr(core->owner_nic, core->mac, core->permanent_mac);
        for (int i = 0; i < pcie_sriov_get_num_vfs(core->owner); i++) {
            IGBCore *vfCore = core->vfs[i];
            if (vfCore) {
                vfCore->mac[V2PMAILBOX0] |= E1000_V2PMAILBOX_RSTI;
                igb_set_interrupt_cause(vfCore,
                    igb_get_msix_vector_other_causes(vfCore));
            }
        }
    } else {
        int vf_idx = pcie_sriov_vf_number(core->owner);
        /* disable Rx and Tx for the VF*/
        core->pf->mac[VFTE] &= ~BIT(vf_idx);
        core->pf->mac[VFRE] &= ~BIT(vf_idx);
        /* indicate VF reset to PF */
        core->pf->mac[VFLRE] |= BIT(vf_idx);
        igb_update_interrupt_state(core->pf);
    }

    for (i = 0; i < ARRAY_SIZE(core->tx); i++) {
        igb_tx_pkt_reset(&core->tx[i]);
    }
    igb_rx_pkt_reset(&core->rx);
    memset(core->tx_ctx, 0, sizeof(core->tx_ctx));
}

void igb_core_pre_save(IGBCore *core)
{
    int i;

    /*
     * If link is down and auto-negotiation is supported and ongoing,
     * complete auto-negotiation immediately. This allows us to look
     * at MII_SR_AUTONEG_COMPLETE to infer link status on load.
     */
    if (qemu_get_queue(core->owner_nic)->link_down && igb_have_autoneg(core)) {
        core->phy[0][PHY_STATUS] |= MII_SR_AUTONEG_COMPLETE;
        igb_update_flowctl_status(core);
    }

    for (i = 0; i < ARRAY_SIZE(core->tx); i++) {
        if (net_tx_pkt_has_fragments(core->tx[i].tx_pkt)) {
            core->tx[i].skip_current_pkt = true;
        }
    }
}

int
igb_core_post_load(IGBCore *core)
{
    if (!pci_is_vf(core->owner)) {
        NetClientState *nc = qemu_get_queue(core->owner_nic);

        /*
         * nc.link_down can't be migrated, so infer link_down according
         * to link status bit in core.mac[STATUS].
         */
        nc->link_down = (core->mac[STATUS] & E1000_STATUS_LU) == 0;
    }

    return 0;
}
