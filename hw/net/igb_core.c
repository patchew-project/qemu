/*
 * Core code for QEMU igb emulation
 *
 * Software developer's manuals:
 * http://www.intel.com/content/dam/doc/datasheet/82574l-gbe-controller-datasheet.pdf
 *
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Leonid Bloch <leonid@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * Based on work done by:
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
 * Copyright (c) 2008 Qumranet
 * Based on work done by:
 * Copyright (c) 2007 Dan Aloni
 * Copyright (c) 2004 Antony T Curtis
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
#include "hw/net/mii.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "sysemu/runstate.h"

#include "net_tx_pkt.h"
#include "net_rx_pkt.h"

#include "igb_common.h"
#include "e1000x_common.h"
#include "igb_core.h"

#include "trace.h"

/* No more then 7813 interrupts per second according to spec 10.2.4.2 */
#define E1000E_MIN_XITR     (500)

#define E1000E_MAX_TX_FRAGS (64)

union e1000_rx_desc_union {
    struct e1000_rx_desc legacy;
    union e1000_rx_desc_extended extended;
    union e1000_rx_desc_packet_split packet_split;
};

static ssize_t
igb_receive_internal(IGBCore *core, const struct iovec *iov, int iovcnt,
                     bool has_vnet);

static inline void
igb_set_interrupt_cause(IGBCore *core, uint32_t val);

static void igb_reset(IGBCore *core, bool sw);

static inline void
igb_process_ts_option(IGBCore *core, struct e1000_tx_desc *dp)
{
    if (le32_to_cpu(dp->upper.data) & E1000_TXD_EXTCMD_TSTAMP) {
        trace_e1000e_wrn_no_ts_support();
    }
}

static inline void
igb_process_snap_option(IGBCore *core, uint32_t cmd_and_length)
{
    if (cmd_and_length & E1000_TXD_CMD_SNAP) {
        trace_e1000e_wrn_no_snap_support();
    }
}

static inline void
igb_raise_legacy_irq(IGBCore *core)
{
    trace_e1000e_irq_legacy_notify(true);
    e1000x_inc_reg_if_not_full(core->mac, IAC);
    pci_set_irq(core->owner, 1);
}

static inline void
igb_lower_legacy_irq(IGBCore *core)
{
    trace_e1000e_irq_legacy_notify(false);
    pci_set_irq(core->owner, 0);
}

static inline void
igb_intrmgr_rearm_timer(IGBIntrDelayTimer *timer)
{
    int64_t delay_ns = (int64_t) timer->core->mac[timer->delay_reg] *
                                 timer->delay_resolution_ns;

    trace_e1000e_irq_rearm_timer(timer->delay_reg << 2, delay_ns);

    timer_mod(timer->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns);

    timer->running = true;
}

static void
igb_intmgr_timer_resume(IGBIntrDelayTimer *timer)
{
    if (timer->running) {
        igb_intrmgr_rearm_timer(timer);
    }
}

static void
igb_intmgr_timer_pause(IGBIntrDelayTimer *timer)
{
    if (timer->running) {
        timer_del(timer->timer);
    }
}

static inline void
igb_intrmgr_stop_timer(IGBIntrDelayTimer *timer)
{
    if (timer->running) {
        timer_del(timer->timer);
        timer->running = false;
    }
}

static inline void
igb_intrmgr_fire_delayed_interrupts(IGBCore *core)
{
    trace_e1000e_irq_fire_delayed_interrupts();
    igb_set_interrupt_cause(core, 0);
}

static void
igb_intrmgr_on_timer(void *opaque)
{
    IGBIntrDelayTimer *timer = opaque;

    trace_e1000e_irq_throttling_timer(timer->delay_reg << 2);

    timer->running = false;
    igb_intrmgr_fire_delayed_interrupts(timer->core);
}

static void
igb_intrmgr_on_throttling_timer(void *opaque)
{
    IGBIntrDelayTimer *timer = opaque;

    assert(!msix_enabled(timer->core->owner));

    timer->running = false;

    if (msi_enabled(timer->core->owner)) {
        trace_e1000e_irq_msi_notify_postponed();
        /* Clear msi_causes_pending to fire MSI eventually */
        timer->core->msi_causes_pending = 0;
        igb_set_interrupt_cause(timer->core, 0);
    } else {
        trace_e1000e_irq_legacy_notify_postponed();
        igb_set_interrupt_cause(timer->core, 0);
    }
}

static void
igb_intrmgr_on_msix_throttling_timer(void *opaque)
{
    IGBIntrDelayTimer *timer = opaque;
    int idx = timer - &timer->core->eitr[0];

    assert(msix_enabled(timer->core->owner));

    timer->running = false;

    trace_e1000e_irq_msix_notify_postponed_vec(idx);
    msix_notify(timer->core->owner, idx);
}

static void
igb_intrmgr_initialize_all_timers(IGBCore *core, bool create)
{
    int i;

    core->radv.delay_reg = RADV;
    core->rdtr.delay_reg = RDTR;
    core->raid.delay_reg = RAID;
    core->tadv.delay_reg = TADV;
    core->tidv.delay_reg = TIDV;

    core->radv.delay_resolution_ns = E1000_INTR_DELAY_NS_RES;
    core->rdtr.delay_resolution_ns = E1000_INTR_DELAY_NS_RES;
    core->raid.delay_resolution_ns = E1000_INTR_DELAY_NS_RES;
    core->tadv.delay_resolution_ns = E1000_INTR_DELAY_NS_RES;
    core->tidv.delay_resolution_ns = E1000_INTR_DELAY_NS_RES;

    core->radv.core = core;
    core->rdtr.core = core;
    core->raid.core = core;
    core->tadv.core = core;
    core->tidv.core = core;

    core->itr.core = core;
    core->itr.delay_reg = ITR;
    core->itr.delay_resolution_ns = E1000_INTR_THROTTLING_NS_RES;

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        core->eitr[i].core = core;
        core->eitr[i].delay_reg = EITR + i;
        core->eitr[i].delay_resolution_ns = E1000_INTR_THROTTLING_NS_RES;
    }

    if (!create) {
        return;
    }

    core->radv.timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, igb_intrmgr_on_timer, &core->radv);
    core->rdtr.timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, igb_intrmgr_on_timer, &core->rdtr);
    core->raid.timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, igb_intrmgr_on_timer, &core->raid);

    core->tadv.timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, igb_intrmgr_on_timer, &core->tadv);
    core->tidv.timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, igb_intrmgr_on_timer, &core->tidv);

    core->itr.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   igb_intrmgr_on_throttling_timer,
                                   &core->itr);

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        core->eitr[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                           igb_intrmgr_on_msix_throttling_timer,
                                           &core->eitr[i]);
    }
}

static inline void
igb_intrmgr_stop_delay_timers(IGBCore *core)
{
    igb_intrmgr_stop_timer(&core->radv);
    igb_intrmgr_stop_timer(&core->rdtr);
    igb_intrmgr_stop_timer(&core->raid);
    igb_intrmgr_stop_timer(&core->tidv);
    igb_intrmgr_stop_timer(&core->tadv);
}

static bool
igb_intrmgr_delay_rx_causes(IGBCore *core, uint32_t *causes)
{
    uint32_t delayable_causes;
    uint32_t rdtr = core->mac[RDTR];
    uint32_t radv = core->mac[RADV];
    uint32_t raid = core->mac[RAID];

    if (msix_enabled(core->owner)) {
        return false;
    }

    delayable_causes = E1000_ICR_RXQ0 |
                       E1000_ICR_RXQ1 |
                       E1000_ICR_RXT0;

    if (!(core->mac[RFCTL] & E1000_RFCTL_ACK_DIS)) {
        delayable_causes |= E1000_ICR_ACK;
    }

    /* Clean up all causes that may be delayed */
    core->delayed_causes |= *causes & delayable_causes;
    *causes &= ~delayable_causes;

    /*
     * Check if delayed RX interrupts disabled by client
     * or if there are causes that cannot be delayed
     */
    if ((rdtr == 0) || (*causes != 0)) {
        return false;
    }

    /*
     * Check if delayed RX ACK interrupts disabled by client
     * and there is an ACK packet received
     */
    if ((raid == 0) && (core->delayed_causes & E1000_ICR_ACK)) {
        return false;
    }

    /* All causes delayed */
    igb_intrmgr_rearm_timer(&core->rdtr);

    if (!core->radv.running && (radv != 0)) {
        igb_intrmgr_rearm_timer(&core->radv);
    }

    if (!core->raid.running && (core->delayed_causes & E1000_ICR_ACK)) {
        igb_intrmgr_rearm_timer(&core->raid);
    }

    return true;
}

static bool
igb_intrmgr_delay_tx_causes(IGBCore *core, uint32_t *causes)
{
    static const uint32_t delayable_causes = E1000_ICR_TXQ0 |
                                             E1000_ICR_TXQ1 |
                                             E1000_ICR_TXQE |
                                             E1000_ICR_TXDW;

    if (msix_enabled(core->owner)) {
        return false;
    }

    /* Clean up all causes that may be delayed */
    core->delayed_causes |= *causes & delayable_causes;
    *causes &= ~delayable_causes;

    /* If there are causes that cannot be delayed */
    if (*causes != 0) {
        return false;
    }

    /* All causes delayed */
    igb_intrmgr_rearm_timer(&core->tidv);

    if (!core->tadv.running && (core->mac[TADV] != 0)) {
        igb_intrmgr_rearm_timer(&core->tadv);
    }

    return true;
}

static uint32_t
igb_intmgr_collect_delayed_causes(IGBCore *core)
{
    uint32_t res;

    if (msix_enabled(core->owner)) {
        assert(core->delayed_causes == 0);
        return 0;
    }

    res = core->delayed_causes;
    core->delayed_causes = 0;

    igb_intrmgr_stop_delay_timers(core);

    return res;
}

static void
igb_intrmgr_fire_all_timers(IGBCore *core)
{
    int i;
    uint32_t val = igb_intmgr_collect_delayed_causes(core);

    trace_e1000e_irq_adding_delayed_causes(val, core->mac[ICR]);
    core->mac[ICR] |= val;

    if (core->itr.running) {
        timer_del(core->itr.timer);
        igb_intrmgr_on_throttling_timer(&core->itr);
    }

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        if (core->eitr[i].running) {
            timer_del(core->eitr[i].timer);
            igb_intrmgr_on_msix_throttling_timer(&core->eitr[i]);
        }
    }
}

static void
igb_intrmgr_resume(IGBCore *core)
{
    int i;

    igb_intmgr_timer_resume(&core->radv);
    igb_intmgr_timer_resume(&core->rdtr);
    igb_intmgr_timer_resume(&core->raid);
    igb_intmgr_timer_resume(&core->tidv);
    igb_intmgr_timer_resume(&core->tadv);

    igb_intmgr_timer_resume(&core->itr);

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        igb_intmgr_timer_resume(&core->eitr[i]);
    }
}

static void
igb_intrmgr_pause(IGBCore *core)
{
    int i;

    igb_intmgr_timer_pause(&core->radv);
    igb_intmgr_timer_pause(&core->rdtr);
    igb_intmgr_timer_pause(&core->raid);
    igb_intmgr_timer_pause(&core->tidv);
    igb_intmgr_timer_pause(&core->tadv);

    igb_intmgr_timer_pause(&core->itr);

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        igb_intmgr_timer_pause(&core->eitr[i]);
    }
}

static void
igb_intrmgr_reset(IGBCore *core)
{
    int i;

    core->delayed_causes = 0;

    igb_intrmgr_stop_delay_timers(core);

    igb_intrmgr_stop_timer(&core->itr);

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        igb_intrmgr_stop_timer(&core->eitr[i]);
    }
}

static void
igb_intrmgr_pci_unint(IGBCore *core)
{
    int i;

    timer_free(core->radv.timer);
    timer_free(core->rdtr.timer);
    timer_free(core->raid.timer);

    timer_free(core->tadv.timer);
    timer_free(core->tidv.timer);

    timer_free(core->itr.timer);

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
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

static inline bool
igb_rx_use_legacy_descriptor(IGBCore *core)
{
    return (core->mac[RFCTL] & E1000_RFCTL_EXTEN) ? false : true;
}

static inline bool
igb_rx_use_ps_descriptor(IGBCore *core)
{
    return !igb_rx_use_legacy_descriptor(core) &&
           (core->mac[RCTL] & E1000_RCTL_DTYP_PS);
}

static inline bool
igb_rss_enabled(IGBCore *core)
{
    return E1000_MRQC_ENABLED(core->mac[MRQC]) &&
           !igb_rx_csum_enabled(core) &&
           !igb_rx_use_legacy_descriptor(core);
}

typedef struct E1000E_RSSInfo_st {
    bool enabled;
    uint32_t hash;
    uint32_t queue;
    uint32_t type;
} E1000E_RSSInfo;

static uint32_t
igb_rss_get_hash_type(IGBCore *core, struct NetRxPkt *pkt)
{
    bool isip4, isip6, isudp, istcp;

    assert(igb_rss_enabled(core));

    net_rx_pkt_get_protocols(pkt, &isip4, &isip6, &isudp, &istcp);

    if (isip4) {
        bool fragment = net_rx_pkt_get_ip4_info(pkt)->fragment;

        trace_e1000e_rx_rss_ip4(fragment, istcp, core->mac[MRQC],
                                E1000_MRQC_EN_TCPIPV4(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV4(core->mac[MRQC]));

        if (!fragment && istcp && E1000_MRQC_EN_TCPIPV4(core->mac[MRQC])) {
            return E1000_MRQ_RSS_TYPE_IPV4TCP;
        }

        if (E1000_MRQC_EN_IPV4(core->mac[MRQC])) {
            return E1000_MRQ_RSS_TYPE_IPV4;
        }
    } else if (isip6) {
        eth_ip6_hdr_info *ip6info = net_rx_pkt_get_ip6_info(pkt);

        bool ex_dis = core->mac[RFCTL] & E1000_RFCTL_IPV6_EX_DIS;
        bool new_ex_dis = core->mac[RFCTL] & E1000_RFCTL_NEW_IPV6_EXT_DIS;

        /*
         * Following two traces must not be combined because resulting
         * event will have 11 arguments totally and some trace backends
         * (at least "ust") have limitation of maximum 10 arguments per
         * event. Events with more arguments fail to compile for
         * backends like these.
         */
        trace_e1000e_rx_rss_ip6_rfctl(core->mac[RFCTL]);
        trace_e1000e_rx_rss_ip6(ex_dis, new_ex_dis, istcp,
                                ip6info->has_ext_hdrs,
                                ip6info->rss_ex_dst_valid,
                                ip6info->rss_ex_src_valid,
                                core->mac[MRQC],
                                E1000_MRQC_EN_TCPIPV6(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV6EX(core->mac[MRQC]),
                                E1000_MRQC_EN_IPV6(core->mac[MRQC]));

        if ((!ex_dis || !ip6info->has_ext_hdrs) &&
            (!new_ex_dis || !(ip6info->rss_ex_dst_valid ||
                              ip6info->rss_ex_src_valid))) {

            if (istcp && !ip6info->fragment &&
                E1000_MRQC_EN_TCPIPV6(core->mac[MRQC])) {
                return E1000_MRQ_RSS_TYPE_IPV6TCP;
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
igb_rss_calc_hash(IGBCore *core, struct NetRxPkt *pkt, E1000E_RSSInfo *info)
{
    NetRxPktRssType type;

    assert(igb_rss_enabled(core));

    switch (info->type) {
    case E1000_MRQ_RSS_TYPE_IPV4:
        type = NetPktRssIpV4;
        break;
    case E1000_MRQ_RSS_TYPE_IPV4TCP:
        type = NetPktRssIpV4Tcp;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6TCP:
        type = NetPktRssIpV6TcpEx;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6:
        type = NetPktRssIpV6;
        break;
    case E1000_MRQ_RSS_TYPE_IPV6EX:
        type = NetPktRssIpV6Ex;
        break;
    default:
        assert(false);
        return 0;
    }

    return net_rx_pkt_calc_rss_hash(pkt, type, (uint8_t *) &core->mac[RSSRK]);
}

static void
igb_rss_parse_packet(IGBCore *core, struct NetRxPkt *pkt, E1000E_RSSInfo *info)
{
    trace_e1000e_rx_rss_started();

    if (!igb_rss_enabled(core)) {
        info->enabled = false;
        info->hash = 0;
        info->queue = 0;
        info->type = 0;
        trace_e1000e_rx_rss_disabled();
        return;
    }

    info->enabled = true;

    info->type = igb_rss_get_hash_type(core, pkt);

    trace_e1000e_rx_rss_type(info->type);

    if (info->type == E1000_MRQ_RSS_TYPE_NONE) {
        info->hash = 0;
        info->queue = 0;
        return;
    }

    info->hash = igb_rss_calc_hash(core, pkt, info);
    info->queue = E1000_RSS_QUEUE(&core->mac[RETA], info->hash);
}

static bool
igb_setup_tx_offloads(IGBCore *core, struct igb_tx *tx)
{
    if (tx->props.tse && tx->cptse) {
        if (!net_tx_pkt_build_vheader(tx->tx_pkt, true, true, tx->props.mss)) {
            return false;
        }

        net_tx_pkt_update_ip_checksums(tx->tx_pkt);
        e1000x_inc_reg_if_not_full(core->mac, TSCTC);
        return true;
    }

    if (tx->sum_needed & E1000_TXD_POPTS_TXSM) {
        if (!net_tx_pkt_build_vheader(tx->tx_pkt, false, true, 0)) {
            return false;
        }
    }

    if (tx->sum_needed & E1000_TXD_POPTS_IXSM) {
        net_tx_pkt_update_ip_hdr_checksum(tx->tx_pkt);
    }

    return true;
}

static void igb_tx_pkt_callback(void *core,
                                const struct iovec *iov,
                                int iovcnt,
                                const struct iovec *virt_iov,
                                int virt_iovcnt)
{
    igb_receive_internal(core, virt_iov, virt_iovcnt, true);
}

static bool
igb_tx_pkt_send(IGBCore *core, struct igb_tx *tx, int queue_index)
{
    int target_queue = MIN(core->max_queue_num, queue_index);
    NetClientState *queue = qemu_get_subqueue(core->owner_nic, target_queue);

    if (!igb_setup_tx_offloads(core, tx)) {
        return false;
    }

    net_tx_pkt_dump(tx->tx_pkt);

    if ((core->phy[0][MII_BMCR] & MII_BMCR_LOOPBACK) ||
        ((core->mac[RCTL] & E1000_RCTL_LBM_MAC) == E1000_RCTL_LBM_MAC)) {
        return net_tx_pkt_send_custom(tx->tx_pkt, false,
                                      igb_tx_pkt_callback, core);
    } else {
        return net_tx_pkt_send(tx->tx_pkt, queue);
    }
}

static void
igb_on_tx_done_update_stats(IGBCore *core, struct NetTxPkt *tx_pkt)
{
    static const int PTCregs[6] = { PTC64, PTC127, PTC255, PTC511,
                                    PTC1023, PTC1522 };

    size_t tot_len = net_tx_pkt_get_total_len(tx_pkt);

    e1000x_increase_size_stats(core->mac, PTCregs, tot_len);
    e1000x_inc_reg_if_not_full(core->mac, TPT);
    e1000x_grow_8reg_if_not_full(core->mac, TOTL, tot_len);

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
}

static void
igb_process_tx_desc(IGBCore *core,
                    struct igb_tx *tx,
                    struct e1000_tx_desc *dp,
                    int queue_index)
{
    uint32_t txd_lower = le32_to_cpu(dp->lower.data);
    uint32_t dtype = txd_lower & (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D);
    unsigned int split_size = txd_lower & 0xffff;
    uint64_t addr;
    struct e1000_context_desc *xp = (struct e1000_context_desc *)dp;
    bool eop = txd_lower & E1000_TXD_CMD_EOP;

    if (dtype == E1000_TXD_CMD_DEXT) { /* context descriptor */
        e1000x_read_tx_ctx_descr(xp, &tx->props);
        igb_process_snap_option(core, le32_to_cpu(xp->cmd_and_length));
        return;
    } else if (dtype == (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D)) {
        /* data descriptor */
        tx->sum_needed = le32_to_cpu(dp->upper.data) >> 8;
        tx->cptse = (txd_lower & E1000_TXD_CMD_TSE) ? 1 : 0;
        igb_process_ts_option(core, dp);
    } else {
        /* legacy descriptor */
        igb_process_ts_option(core, dp);
        tx->cptse = 0;
    }

    addr = le64_to_cpu(dp->buffer_addr);

    if (!tx->skip_cp) {
        if (!net_tx_pkt_add_raw_fragment(tx->tx_pkt, addr, split_size)) {
            tx->skip_cp = true;
        }
    }

    if (eop) {
        if (!tx->skip_cp && net_tx_pkt_parse(tx->tx_pkt)) {
            if (e1000x_vlan_enabled(core->mac) &&
                e1000x_is_vlan_txd(txd_lower)) {
                net_tx_pkt_setup_vlan_header_ex(tx->tx_pkt,
                    le16_to_cpu(dp->upper.fields.special), core->mac[VET]);
            }
            if (igb_tx_pkt_send(core, tx, queue_index)) {
                igb_on_tx_done_update_stats(core, tx->tx_pkt);
            }
        }

        tx->skip_cp = false;
        net_tx_pkt_reset(tx->tx_pkt);

        tx->sum_needed = 0;
        tx->cptse = 0;
    }
}

static inline uint32_t
igb_tx_wb_interrupt_cause(IGBCore *core, int queue_idx)
{
    if (!msix_enabled(core->owner)) {
        return E1000_ICR_TXDW;
    }

    return (queue_idx == 0) ? E1000_ICR_TXQ0 : E1000_ICR_TXQ1;
}

static inline uint32_t
igb_rx_wb_interrupt_cause(IGBCore *core, int queue_idx,
                             bool min_threshold_hit)
{
    if (!msix_enabled(core->owner)) {
        return E1000_ICS_RXT0 | (min_threshold_hit ? E1000_ICS_RXDMT0 : 0);
    }

    return (queue_idx == 0) ? E1000_ICR_RXQ0 : E1000_ICR_RXQ1;
}

static uint32_t
igb_txdesc_writeback(IGBCore *core, dma_addr_t base,
                     struct e1000_tx_desc *dp, bool *ide, int queue_idx)
{
    uint32_t txd_upper, txd_lower = le32_to_cpu(dp->lower.data);

    if (!(txd_lower & E1000_TXD_CMD_RS) &&
        !(core->mac[IVAR] & E1000_IVAR_TX_INT_EVERY_WB)) {
        return 0;
    }

    *ide = (txd_lower & E1000_TXD_CMD_IDE) ? true : false;

    txd_upper = le32_to_cpu(dp->upper.data) | E1000_TXD_STAT_DD;

    dp->upper.data = cpu_to_le32(txd_upper);
    pci_dma_write(core->owner, base + ((char *)&dp->upper - (char *)dp),
                  &dp->upper, sizeof(dp->upper));
    return igb_tx_wb_interrupt_cause(core, queue_idx);
}

typedef struct E1000E_RingInfo_st {
    int dbah;
    int dbal;
    int dlen;
    int dh;
    int dt;
    int idx;
} E1000E_RingInfo;

static inline bool
igb_ring_empty(IGBCore *core, const E1000E_RingInfo *r)
{
    return core->mac[r->dh] == core->mac[r->dt] ||
                core->mac[r->dt] >= core->mac[r->dlen] / E1000_RING_DESC_LEN;
}

static inline uint64_t
igb_ring_base(IGBCore *core, const E1000E_RingInfo *r)
{
    uint64_t bah = core->mac[r->dbah];
    uint64_t bal = core->mac[r->dbal];

    return (bah << 32) + bal;
}

static inline uint64_t
igb_ring_head_descr(IGBCore *core, const E1000E_RingInfo *r)
{
    return igb_ring_base(core, r) + E1000_RING_DESC_LEN * core->mac[r->dh];
}

static inline void
igb_ring_advance(IGBCore *core, const E1000E_RingInfo *r, uint32_t count)
{
    core->mac[r->dh] += count;

    if (core->mac[r->dh] * E1000_RING_DESC_LEN >= core->mac[r->dlen]) {
        core->mac[r->dh] = 0;
    }
}

static inline uint32_t
igb_ring_free_descr_num(IGBCore *core, const E1000E_RingInfo *r)
{
    trace_e1000e_ring_free_space(r->idx, core->mac[r->dlen],
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
igb_ring_enabled(IGBCore *core, const E1000E_RingInfo *r)
{
    return core->mac[r->dlen] > 0;
}

static inline uint32_t
igb_ring_len(IGBCore *core, const E1000E_RingInfo *r)
{
    return core->mac[r->dlen];
}

typedef struct IGB_TxRing_st {
    const E1000E_RingInfo *i;
    struct igb_tx *tx;
} IGB_TxRing;

static inline int
igb_mq_queue_idx(int base_reg_idx, int reg_idx)
{
    return (reg_idx - base_reg_idx) / (0x100 >> 2);
}

static inline void
igb_tx_ring_init(IGBCore *core, IGB_TxRing *txr, int idx)
{
    static const E1000E_RingInfo i[IGB_NUM_QUEUES] = {
        { TDBAH,  TDBAL,  TDLEN,  TDH,  TDT, 0 },
        { TDBAH1, TDBAL1, TDLEN1, TDH1, TDT1, 1 }
    };

    assert(idx < ARRAY_SIZE(i));

    txr->i     = &i[idx];
    txr->tx    = &core->tx[idx];
}

typedef struct E1000E_RxRing_st {
    const E1000E_RingInfo *i;
} E1000E_RxRing;

static inline void
igb_rx_ring_init(IGBCore *core, E1000E_RxRing *rxr, int idx)
{
    static const E1000E_RingInfo i[IGB_NUM_QUEUES] = {
        { RDBAH0, RDBAL0, RDLEN0, RDH0, RDT0, 0 },
        { RDBAH1, RDBAL1, RDLEN1, RDH1, RDT1, 1 }
    };

    assert(idx < ARRAY_SIZE(i));

    rxr->i      = &i[idx];
}

static void
igb_start_xmit(IGBCore *core, const IGB_TxRing *txr)
{
    dma_addr_t base;
    struct e1000_tx_desc desc;
    bool ide = false;
    const E1000E_RingInfo *txi = txr->i;
    uint32_t cause = E1000_ICS_TXQE;

    if (!(core->mac[TCTL] & E1000_TCTL_EN)) {
        trace_e1000e_tx_disabled();
        return;
    }

    while (!igb_ring_empty(core, txi)) {
        base = igb_ring_head_descr(core, txi);

        pci_dma_read(core->owner, base, &desc, sizeof(desc));

        trace_e1000e_tx_descr((void *)(intptr_t)desc.buffer_addr,
                              desc.lower.data, desc.upper.data);

        igb_process_tx_desc(core, txr->tx, &desc, txi->idx);
        cause |= igb_txdesc_writeback(core, base, &desc, &ide, txi->idx);

        igb_ring_advance(core, txi, 1);
    }

    if (!ide || !igb_intrmgr_delay_tx_causes(core, &cause)) {
        igb_set_interrupt_cause(core, cause);
    }
}

static bool
igb_has_rxbufs(IGBCore *core, const E1000E_RingInfo *r, size_t total_size)
{
    uint32_t bufs = igb_ring_free_descr_num(core, r);

    trace_e1000e_rx_has_buffers(r->idx, bufs, total_size,
                                core->rx_desc_buf_size);

    return total_size <= bufs / (core->rx_desc_len / E1000_MIN_RX_DESC_LEN) *
                         core->rx_desc_buf_size;
}

void
igb_start_recv(IGBCore *core)
{
    int i;

    trace_e1000e_rx_start_recv();

    for (i = 0; i <= core->max_queue_num; i++) {
        qemu_flush_queued_packets(qemu_get_subqueue(core->owner_nic, i));
    }
}

bool
igb_can_receive(IGBCore *core)
{
    int i;

    if (!e1000x_rx_ready(core->owner, core->mac)) {
        return false;
    }

    for (i = 0; i < IGB_NUM_QUEUES; i++) {
        E1000E_RxRing rxr;

        igb_rx_ring_init(core, &rxr, i);
        if (igb_ring_enabled(core, rxr.i) && igb_has_rxbufs(core, rxr.i, 1)) {
            trace_e1000e_rx_can_recv();
            return true;
        }
    }

    trace_e1000e_rx_can_recv_rings_full();
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

static bool
igb_receive_filter(IGBCore *core, const uint8_t *buf, int size)
{
    uint32_t rctl = core->mac[RCTL];

    if (e1000x_is_vlan_packet(buf, core->mac[VET]) &&
        e1000x_vlan_rx_filter_enabled(core->mac)) {
        uint16_t vid = lduw_be_p(&PKT_GET_VLAN_HDR(buf)->h_tci);
        uint32_t vfta =
            ldl_le_p((uint32_t *)(core->mac + VFTA) +
                     ((vid >> E1000_VFTA_ENTRY_SHIFT) & E1000_VFTA_ENTRY_MASK));
        if ((vfta & (1 << (vid & E1000_VFTA_ENTRY_BIT_SHIFT_MASK))) == 0) {
            trace_e1000e_rx_flt_vlan_mismatch(vid);
            return false;
        } else {
            trace_e1000e_rx_flt_vlan_match(vid);
        }
    }

    switch (net_rx_pkt_get_packet_type(core->rx_pkt)) {
    case ETH_PKT_UCAST:
        if (rctl & E1000_RCTL_UPE) {
            return true; /* promiscuous ucast */
        }
        break;

    case ETH_PKT_BCAST:
        if (rctl & E1000_RCTL_BAM) {
            return true; /* broadcast enabled */
        }
        break;

    case ETH_PKT_MCAST:
        if (rctl & E1000_RCTL_MPE) {
            return true; /* promiscuous mcast */
        }
        break;

    default:
        g_assert_not_reached();
    }

    return e1000x_rx_group_filter(core->mac, buf);
}

static inline void
igb_read_lgcy_rx_descr(IGBCore *core, struct e1000_rx_desc *desc,
                       hwaddr *buff_addr)
{
    *buff_addr = le64_to_cpu(desc->buffer_addr);
}

static inline void
igb_read_ext_rx_descr(IGBCore *core, union e1000_rx_desc_extended *desc,
                      hwaddr *buff_addr)
{
    *buff_addr = le64_to_cpu(desc->read.buffer_addr);
}

static inline void
igb_read_ps_rx_descr(IGBCore *core,
                     union e1000_rx_desc_packet_split *desc,
                     hwaddr buff_addr[MAX_PS_BUFFERS])
{
    int i;

    for (i = 0; i < MAX_PS_BUFFERS; i++) {
        buff_addr[i] = le64_to_cpu(desc->read.buffer_addr[i]);
    }

    trace_e1000e_rx_desc_ps_read(buff_addr[0], buff_addr[1],
                                 buff_addr[2], buff_addr[3]);
}

static inline void
igb_read_rx_descr(IGBCore *core, union e1000_rx_desc_union *desc,
                  hwaddr buff_addr[MAX_PS_BUFFERS])
{
    if (igb_rx_use_legacy_descriptor(core)) {
        igb_read_lgcy_rx_descr(core, &desc->legacy, &buff_addr[0]);
        buff_addr[1] = buff_addr[2] = buff_addr[3] = 0;
    } else {
        if (core->mac[RCTL] & E1000_RCTL_DTYP_PS) {
            igb_read_ps_rx_descr(core, &desc->packet_split, buff_addr);
        } else {
            igb_read_ext_rx_descr(core, &desc->extended, &buff_addr[0]);
            buff_addr[1] = buff_addr[2] = buff_addr[3] = 0;
        }
    }
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
            trace_e1000e_rx_metadata_l3_csum_validation_failed();
        } else {
            csum_error = csum_valid ? 0 : E1000_RXDEXT_STATERR_IPE;
            *status_flags |= E1000_RXD_STAT_IPCS | csum_error;
        }
    } else {
        trace_e1000e_rx_metadata_l3_cso_disabled();
    }

    if (!igb_rx_l4_cso_enabled(core)) {
        trace_e1000e_rx_metadata_l4_cso_disabled();
        return;
    }

    if (!net_rx_pkt_validate_l4_csum(pkt, &csum_valid)) {
        trace_e1000e_rx_metadata_l4_csum_validation_failed();
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

static inline bool
igb_is_tcp_ack(IGBCore *core, struct NetRxPkt *rx_pkt)
{
    if (!net_rx_pkt_is_tcp_ack(rx_pkt)) {
        return false;
    }

    if (core->mac[RFCTL] & E1000_RFCTL_ACK_DATA_DIS) {
        return !net_rx_pkt_has_tcp_data(rx_pkt);
    }

    return true;
}

static void
igb_build_rx_metadata(IGBCore *core,
                      struct NetRxPkt *pkt,
                      bool is_eop,
                      const E1000E_RSSInfo *rss_info,
                      uint32_t *rss, uint32_t *mrq,
                      uint32_t *status_flags,
                      uint16_t *ip_id,
                      uint16_t *vlan_tag)
{
    struct virtio_net_hdr *vhdr;
    bool isip4, isip6, istcp, isudp;
    uint32_t pkt_type;

    *status_flags = E1000_RXD_STAT_DD;

    /* No additional metadata needed for non-EOP descriptors */
    if (!is_eop) {
        goto func_exit;
    }

    *status_flags |= E1000_RXD_STAT_EOP;

    net_rx_pkt_get_protocols(pkt, &isip4, &isip6, &isudp, &istcp);
    trace_e1000e_rx_metadata_protocols(isip4, isip6, isudp, istcp);

    /* VLAN state */
    if (net_rx_pkt_is_vlan_stripped(pkt)) {
        *status_flags |= E1000_RXD_STAT_VP;
        *vlan_tag = cpu_to_le16(net_rx_pkt_get_vlan_tag(pkt));
        trace_e1000e_rx_metadata_vlan(*vlan_tag);
    }

    /* Packet parsing results */
    if ((core->mac[RXCSUM] & E1000_RXCSUM_PCSD) != 0) {
        if (rss_info->enabled) {
            *rss = cpu_to_le32(rss_info->hash);
            *mrq = cpu_to_le32(rss_info->type | (rss_info->queue << 8));
            trace_e1000e_rx_metadata_rss(*rss, *mrq);
        }
    } else if (isip4) {
            *status_flags |= E1000_RXD_STAT_IPIDV;
            *ip_id = cpu_to_le16(net_rx_pkt_get_ip_id(pkt));
            trace_e1000e_rx_metadata_ip_id(*ip_id);
    }

    if (istcp && igb_is_tcp_ack(core, pkt)) {
        *status_flags |= E1000_RXD_STAT_ACK;
        trace_e1000e_rx_metadata_ack();
    }

    if (isip6 && (core->mac[RFCTL] & E1000_RFCTL_IPV6_DIS)) {
        trace_e1000e_rx_metadata_ipv6_filtering_disabled();
        pkt_type = E1000_RXD_PKT_MAC;
    } else if (istcp || isudp) {
        pkt_type = isip4 ? E1000_RXD_PKT_IP4_XDP : E1000_RXD_PKT_IP6_XDP;
    } else if (isip4 || isip6) {
        pkt_type = isip4 ? E1000_RXD_PKT_IP4 : E1000_RXD_PKT_IP6;
    } else {
        pkt_type = E1000_RXD_PKT_MAC;
    }

    *status_flags |= E1000_RXD_PKT_TYPE(pkt_type);
    trace_e1000e_rx_metadata_pkt_type(pkt_type);

    /* RX CSO information */
    if (isip6 && (core->mac[RFCTL] & E1000_RFCTL_IPV6_XSUM_DIS)) {
        trace_e1000e_rx_metadata_ipv6_sum_disabled();
        goto func_exit;
    }

    vhdr = net_rx_pkt_get_vhdr(pkt);

    if (!(vhdr->flags & VIRTIO_NET_HDR_F_DATA_VALID) &&
        !(vhdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
        trace_e1000e_rx_metadata_virthdr_no_csum_info();
        igb_verify_csum_in_sw(core, pkt, status_flags, istcp, isudp);
        goto func_exit;
    }

    if (igb_rx_l3_cso_enabled(core)) {
        *status_flags |= isip4 ? E1000_RXD_STAT_IPCS : 0;
    } else {
        trace_e1000e_rx_metadata_l3_cso_disabled();
    }

    if (igb_rx_l4_cso_enabled(core)) {
        if (istcp) {
            *status_flags |= E1000_RXD_STAT_TCPCS;
        } else if (isudp) {
            *status_flags |= E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS;
        }
    } else {
        trace_e1000e_rx_metadata_l4_cso_disabled();
    }

    trace_e1000e_rx_metadata_status_flags(*status_flags);

func_exit:
    *status_flags = cpu_to_le32(*status_flags);
}

static inline void
igb_write_lgcy_rx_descr(IGBCore *core, struct e1000_rx_desc *desc,
                        struct NetRxPkt *pkt,
                        const E1000E_RSSInfo *rss_info,
                        uint16_t length)
{
    uint32_t status_flags, rss, mrq;
    uint16_t ip_id;

    assert(!rss_info->enabled);

    desc->length = cpu_to_le16(length);
    desc->csum = 0;

    igb_build_rx_metadata(core, pkt, pkt != NULL,
                             rss_info,
                             &rss, &mrq,
                             &status_flags, &ip_id,
                             &desc->special);
    desc->errors = (uint8_t) (le32_to_cpu(status_flags) >> 24);
    desc->status = (uint8_t) le32_to_cpu(status_flags);
}

static inline void
igb_write_ext_rx_descr(IGBCore *core, union e1000_rx_desc_extended *desc,
                       struct NetRxPkt *pkt,
                       const E1000E_RSSInfo *rss_info,
                       uint16_t length)
{
    memset(&desc->wb, 0, sizeof(desc->wb));

    desc->wb.upper.length = cpu_to_le16(length);

    igb_build_rx_metadata(core, pkt, pkt != NULL,
                          rss_info,
                          &desc->wb.lower.hi_dword.rss,
                          &desc->wb.lower.mrq,
                          &desc->wb.upper.status_error,
                          &desc->wb.lower.hi_dword.csum_ip.ip_id,
                          &desc->wb.upper.vlan);
}

static inline void
igb_write_ps_rx_descr(IGBCore *core,
                      union e1000_rx_desc_packet_split *desc,
                      struct NetRxPkt *pkt,
                      const E1000E_RSSInfo *rss_info,
                      size_t ps_hdr_len,
                      uint16_t(*written)[MAX_PS_BUFFERS])
{
    int i;

    memset(&desc->wb, 0, sizeof(desc->wb));

    desc->wb.middle.length0 = cpu_to_le16((*written)[0]);

    for (i = 0; i < PS_PAGE_BUFFERS; i++) {
        desc->wb.upper.length[i] = cpu_to_le16((*written)[i + 1]);
    }

    igb_build_rx_metadata(core, pkt, pkt != NULL,
                          rss_info,
                          &desc->wb.lower.hi_dword.rss,
                          &desc->wb.lower.mrq,
                          &desc->wb.middle.status_error,
                          &desc->wb.lower.hi_dword.csum_ip.ip_id,
                          &desc->wb.middle.vlan);

    desc->wb.upper.header_status =
        cpu_to_le16(ps_hdr_len | (ps_hdr_len ? E1000_RXDPS_HDRSTAT_HDRSP : 0));

    trace_e1000e_rx_desc_ps_write((*written)[0], (*written)[1],
                                  (*written)[2], (*written)[3]);
}

static inline void
igb_write_rx_descr(IGBCore *core, union e1000_rx_desc_union *desc,
struct NetRxPkt *pkt, const E1000E_RSSInfo *rss_info,
    size_t ps_hdr_len, uint16_t(*written)[MAX_PS_BUFFERS])
{
    if (igb_rx_use_legacy_descriptor(core)) {
        assert(ps_hdr_len == 0);
        igb_write_lgcy_rx_descr(core, &desc->legacy, pkt, rss_info,
                                (*written)[0]);
    } else {
        if (core->mac[RCTL] & E1000_RCTL_DTYP_PS) {
            igb_write_ps_rx_descr(core, &desc->packet_split, pkt, rss_info,
                                  ps_hdr_len, written);
        } else {
            assert(ps_hdr_len == 0);
            igb_write_ext_rx_descr(core, &desc->extended, pkt, rss_info,
                                   (*written)[0]);
        }
    }
}

static inline void
igb_pci_dma_write_rx_desc(IGBCore *core, dma_addr_t addr,
                          union e1000_rx_desc_union *desc, dma_addr_t len)
{
    PCIDevice *dev = core->owner;

    if (igb_rx_use_legacy_descriptor(core)) {
        struct e1000_rx_desc *d = &desc->legacy;
        size_t offset = offsetof(struct e1000_rx_desc, status);
        uint8_t status = d->status;

        d->status &= ~E1000_RXD_STAT_DD;
        pci_dma_write(dev, addr, desc, len);

        if (status & E1000_RXD_STAT_DD) {
            d->status = status;
            pci_dma_write(dev, addr + offset, &status, sizeof(status));
        }
    } else {
        if (core->mac[RCTL] & E1000_RCTL_DTYP_PS) {
            union e1000_rx_desc_packet_split *d = &desc->packet_split;
            size_t offset = offsetof(union e1000_rx_desc_packet_split,
                wb.middle.status_error);
            uint32_t status = d->wb.middle.status_error;

            d->wb.middle.status_error &= ~E1000_RXD_STAT_DD;
            pci_dma_write(dev, addr, desc, len);

            if (status & E1000_RXD_STAT_DD) {
                d->wb.middle.status_error = status;
                pci_dma_write(dev, addr + offset, &status, sizeof(status));
            }
        } else {
            union e1000_rx_desc_extended *d = &desc->extended;
            size_t offset = offsetof(union e1000_rx_desc_extended,
                wb.upper.status_error);
            uint32_t status = d->wb.upper.status_error;

            d->wb.upper.status_error &= ~E1000_RXD_STAT_DD;
            pci_dma_write(dev, addr, desc, len);

            if (status & E1000_RXD_STAT_DD) {
                d->wb.upper.status_error = status;
                pci_dma_write(dev, addr + offset, &status, sizeof(status));
            }
        }
    }
}

typedef struct e1000e_ba_state_st {
    uint16_t written[MAX_PS_BUFFERS];
    uint8_t cur_idx;
} e1000e_ba_state;

static inline void
igb_write_hdr_to_rx_buffers(IGBCore *core,
                            hwaddr ba[MAX_PS_BUFFERS],
                            e1000e_ba_state *bastate,
                            const char *data,
                            dma_addr_t data_len)
{
    assert(data_len <= core->rxbuf_sizes[0] - bastate->written[0]);

    pci_dma_write(core->owner, ba[0] + bastate->written[0], data, data_len);
    bastate->written[0] += data_len;

    bastate->cur_idx = 1;
}

static void
igb_write_to_rx_buffers(IGBCore *core,
                        hwaddr ba[MAX_PS_BUFFERS],
                        e1000e_ba_state *bastate,
                        const char *data,
                        dma_addr_t data_len)
{
    while (data_len > 0) {
        uint32_t cur_buf_len = core->rxbuf_sizes[bastate->cur_idx];
        uint32_t cur_buf_bytes_left = cur_buf_len -
                                      bastate->written[bastate->cur_idx];
        uint32_t bytes_to_write = MIN(data_len, cur_buf_bytes_left);

        trace_e1000e_rx_desc_buff_write(bastate->cur_idx,
                                        ba[bastate->cur_idx],
                                        bastate->written[bastate->cur_idx],
                                        data,
                                        bytes_to_write);

        pci_dma_write(core->owner,
            ba[bastate->cur_idx] + bastate->written[bastate->cur_idx],
            data, bytes_to_write);

        bastate->written[bastate->cur_idx] += bytes_to_write;
        data += bytes_to_write;
        data_len -= bytes_to_write;

        if (bastate->written[bastate->cur_idx] == cur_buf_len) {
            bastate->cur_idx++;
        }

        assert(bastate->cur_idx < MAX_PS_BUFFERS);
    }
}

static void
igb_update_rx_stats(IGBCore *core, size_t data_size, size_t data_fcs_size)
{
    e1000x_update_rx_total_stats(core->mac, data_size, data_fcs_size);

    switch (net_rx_pkt_get_packet_type(core->rx_pkt)) {
    case ETH_PKT_BCAST:
        e1000x_inc_reg_if_not_full(core->mac, BPRC);
        break;

    case ETH_PKT_MCAST:
        e1000x_inc_reg_if_not_full(core->mac, MPRC);
        break;

    default:
        break;
    }
}

static inline bool
igb_rx_descr_threshold_hit(IGBCore *core, const E1000E_RingInfo *rxi)
{
    return igb_ring_free_descr_num(core, rxi) ==
           igb_ring_len(core, rxi) >> core->rxbuf_min_shift;
}

static bool
igb_do_ps(IGBCore *core, struct NetRxPkt *pkt, size_t *hdr_len)
{
    bool isip4, isip6, isudp, istcp;
    bool fragment;

    if (!igb_rx_use_ps_descriptor(core)) {
        return false;
    }

    net_rx_pkt_get_protocols(pkt, &isip4, &isip6, &isudp, &istcp);

    if (isip4) {
        fragment = net_rx_pkt_get_ip4_info(pkt)->fragment;
    } else if (isip6) {
        fragment = net_rx_pkt_get_ip6_info(pkt)->fragment;
    } else {
        return false;
    }

    if (fragment && (core->mac[RFCTL] & E1000_RFCTL_IPFRSP_DIS)) {
        return false;
    }

    if (!fragment && (isudp || istcp)) {
        *hdr_len = net_rx_pkt_get_l5_hdr_offset(pkt);
    } else {
        *hdr_len = net_rx_pkt_get_l4_hdr_offset(pkt);
    }

    if ((*hdr_len > core->rxbuf_sizes[0]) ||
        (*hdr_len > net_rx_pkt_get_total_len(pkt))) {
        return false;
    }

    return true;
}

static void
igb_write_packet_to_guest(IGBCore *core, struct NetRxPkt *pkt,
                          const E1000E_RxRing *rxr,
                          const E1000E_RSSInfo *rss_info)
{
    PCIDevice *d = core->owner;
    dma_addr_t base;
    union e1000_rx_desc_union desc;
    size_t desc_size;
    size_t desc_offset = 0;
    size_t iov_ofs = 0;

    struct iovec *iov = net_rx_pkt_get_iovec(pkt);
    size_t size = net_rx_pkt_get_total_len(pkt);
    size_t total_size = size + e1000x_fcs_len(core->mac);
    const E1000E_RingInfo *rxi;
    size_t ps_hdr_len = 0;
    bool do_ps = igb_do_ps(core, pkt, &ps_hdr_len);
    bool is_first = true;

    rxi = rxr->i;

    do {
        hwaddr ba[MAX_PS_BUFFERS];
        e1000e_ba_state bastate = { { 0 } };
        bool is_last = false;

        desc_size = total_size - desc_offset;

        if (desc_size > core->rx_desc_buf_size) {
            desc_size = core->rx_desc_buf_size;
        }

        if (igb_ring_empty(core, rxi)) {
            return;
        }

        base = igb_ring_head_descr(core, rxi);

        pci_dma_read(d, base, &desc, core->rx_desc_len);

        trace_e1000e_rx_descr(rxi->idx, base, core->rx_desc_len);

        igb_read_rx_descr(core, &desc, ba);

        if (ba[0]) {
            if (desc_offset < size) {
                static const uint32_t fcs_pad;
                size_t iov_copy;
                size_t copy_size = size - desc_offset;
                if (copy_size > core->rx_desc_buf_size) {
                    copy_size = core->rx_desc_buf_size;
                }

                /* For PS mode copy the packet header first */
                if (do_ps) {
                    if (is_first) {
                        size_t ps_hdr_copied = 0;
                        do {
                            iov_copy = MIN(ps_hdr_len - ps_hdr_copied,
                                           iov->iov_len - iov_ofs);

                            igb_write_hdr_to_rx_buffers(core, ba, &bastate,
                                                      iov->iov_base, iov_copy);

                            copy_size -= iov_copy;
                            ps_hdr_copied += iov_copy;

                            iov_ofs += iov_copy;
                            if (iov_ofs == iov->iov_len) {
                                iov++;
                                iov_ofs = 0;
                            }
                        } while (ps_hdr_copied < ps_hdr_len);

                        is_first = false;
                    } else {
                        /* Leave buffer 0 of each descriptor except first */
                        /* empty as per spec 7.1.5.1                      */
                        igb_write_hdr_to_rx_buffers(core, ba, &bastate,
                                                       NULL, 0);
                    }
                }

                /* Copy packet payload */
                while (copy_size) {
                    iov_copy = MIN(copy_size, iov->iov_len - iov_ofs);

                    igb_write_to_rx_buffers(core, ba, &bastate,
                                            iov->iov_base + iov_ofs, iov_copy);

                    copy_size -= iov_copy;
                    iov_ofs += iov_copy;
                    if (iov_ofs == iov->iov_len) {
                        iov++;
                        iov_ofs = 0;
                    }
                }

                if (desc_offset + desc_size >= total_size) {
                    /* Simulate FCS checksum presence in the last descriptor */
                    igb_write_to_rx_buffers(core, ba, &bastate,
                          (const char *) &fcs_pad, e1000x_fcs_len(core->mac));
                }
            }
        } else { /* as per intel docs; skip descriptors with null buf addr */
            trace_e1000e_rx_null_descriptor();
        }
        desc_offset += desc_size;
        if (desc_offset >= total_size) {
            is_last = true;
        }

        igb_write_rx_descr(core, &desc, is_last ? core->rx_pkt : NULL,
                           rss_info, do_ps ? ps_hdr_len : 0, &bastate.written);
        igb_pci_dma_write_rx_desc(core, base, &desc, core->rx_desc_len);

        igb_ring_advance(core, rxi,
                         core->rx_desc_len / E1000_MIN_RX_DESC_LEN);

    } while (desc_offset < total_size);

    igb_update_rx_stats(core, size, total_size);
}

static inline void
igb_rx_fix_l4_csum(IGBCore *core, struct NetRxPkt *pkt)
{
    struct virtio_net_hdr *vhdr = net_rx_pkt_get_vhdr(pkt);

    if (vhdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
        net_rx_pkt_fix_l4_csum(pkt);
    }
}

ssize_t
igb_receive_iov(IGBCore *core, const struct iovec *iov, int iovcnt)
{
    return igb_receive_internal(core, iov, iovcnt, core->has_vnet);
}

static ssize_t
igb_receive_internal(IGBCore *core, const struct iovec *iov, int iovcnt,
                     bool has_vnet)
{
    static const int maximum_ethernet_hdr_len = (ETH_HLEN + 4);

    uint32_t n = 0;
    uint8_t min_buf[ETH_ZLEN];
    struct iovec min_iov;
    uint8_t *filter_buf;
    size_t size, orig_size;
    size_t iov_ofs = 0;
    E1000E_RxRing rxr;
    E1000E_RSSInfo rss_info;
    size_t total_size;
    ssize_t retval;
    bool rdmts_hit;

    trace_e1000e_rx_receive_iov(iovcnt);

    if (!e1000x_hw_rx_enabled(core->mac)) {
        return -1;
    }

    /* Pull virtio header in */
    if (has_vnet) {
        net_rx_pkt_set_vhdr_iovec(core->rx_pkt, iov, iovcnt);
        iov_ofs = sizeof(struct virtio_net_hdr);
    } else {
        net_rx_pkt_unset_vhdr(core->rx_pkt);
    }

    filter_buf = iov->iov_base + iov_ofs;
    orig_size = iov_size(iov, iovcnt);
    size = orig_size - iov_ofs;

    /* Pad to minimum Ethernet frame length */
    if (size < sizeof(min_buf)) {
        iov_to_buf(iov, iovcnt, iov_ofs, min_buf, size);
        memset(&min_buf[size], 0, sizeof(min_buf) - size);
        e1000x_inc_reg_if_not_full(core->mac, RUC);
        min_iov.iov_base = filter_buf = min_buf;
        min_iov.iov_len = size = sizeof(min_buf);
        iovcnt = 1;
        iov = &min_iov;
        iov_ofs = 0;
    } else if (iov->iov_len < maximum_ethernet_hdr_len) {
        /* This is very unlikely, but may happen. */
        iov_to_buf(iov, iovcnt, iov_ofs, min_buf, maximum_ethernet_hdr_len);
        filter_buf = min_buf;
    }

    /* Discard oversized packets if !LPE and !SBP. */
    if (e1000x_is_oversized(core->mac, size)) {
        return orig_size;
    }

    net_rx_pkt_set_packet_type(core->rx_pkt,
        get_eth_packet_type(PKT_GET_ETH_HDR(filter_buf)));

    if (!igb_receive_filter(core, filter_buf, size)) {
        trace_e1000e_rx_flt_dropped();
        return orig_size;
    }

    net_rx_pkt_attach_iovec_ex(core->rx_pkt, iov, iovcnt, iov_ofs,
                               e1000x_vlan_enabled(core->mac), core->mac[VET]);

    igb_rss_parse_packet(core, core->rx_pkt, &rss_info);
    igb_rx_ring_init(core, &rxr, rss_info.queue);

    trace_e1000e_rx_rss_dispatched_to_queue(rxr.i->idx);

    total_size = net_rx_pkt_get_total_len(core->rx_pkt) +
        e1000x_fcs_len(core->mac);

    if (igb_has_rxbufs(core, rxr.i, total_size)) {
        igb_rx_fix_l4_csum(core, core->rx_pkt);

        igb_write_packet_to_guest(core, core->rx_pkt, &rxr, &rss_info);

        retval = orig_size;

        /* Perform small receive detection (RSRPD) */
        if (total_size < core->mac[RSRPD]) {
            n |= E1000_ICS_SRPD;
        }

        /* Perform ACK receive detection */
        if  (!(core->mac[RFCTL] & E1000_RFCTL_ACK_DIS) &&
             (igb_is_tcp_ack(core, core->rx_pkt))) {
            n |= E1000_ICS_ACK;
        }

        /* Check if receive descriptor minimum threshold hit */
        rdmts_hit = igb_rx_descr_threshold_hit(core, rxr.i);
        n |= igb_rx_wb_interrupt_cause(core, rxr.i->idx, rdmts_hit);

        trace_e1000e_rx_written_to_guest(n);
    } else {
        n |= E1000_ICS_RXO;
        retval = 0;

        trace_e1000e_rx_not_written_to_guest(n);
    }

    if (!igb_intrmgr_delay_rx_causes(core, &n)) {
        trace_e1000e_rx_interrupt_set(n);
        igb_set_interrupt_cause(core, n);
    } else {
        trace_e1000e_rx_interrupt_delayed(n);
    }

    return retval;
}

static inline bool
igb_have_autoneg(IGBCore *core)
{
    return core->phy[0][MII_BMCR] & MII_BMCR_AUTOEN;
}

static void igb_update_flowctl_status(IGBCore *core)
{
    if (igb_have_autoneg(core) &&
        core->phy[0][MII_BMSR] & MII_BMSR_AN_COMP) {
        trace_e1000e_link_autoneg_flowctl(true);
        core->mac[CTRL] |= E1000_CTRL_TFCE | E1000_CTRL_RFCE;
    } else {
        trace_e1000e_link_autoneg_flowctl(false);
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
    /* bits 0-5 reserved; MII_BMCR_[ANRESTART,RESET] are self clearing */
    core->phy[0][MII_BMCR] = val & ~(0x3f |
                                     MII_BMCR_RESET |
                                     MII_BMCR_ANRESTART);

    if ((val & MII_BMCR_ANRESTART) &&
        igb_have_autoneg(core)) {
        e1000x_restart_autoneg(core->mac, core->phy[0], core->autoneg_timer);
    }
}

static void
igb_set_phy_oem_bits(IGBCore *core, int index, uint16_t val)
{
    core->phy[0][PHY_OEM_BITS] = val & ~BIT(10);

    if (val & BIT(10)) {
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

    trace_e1000e_link_status_changed(nc->link_down ? false : true);

    if (nc->link_down) {
        e1000x_update_regs_on_link_down(core->mac, core->phy[0]);
    } else {
        if (igb_have_autoneg(core) &&
            !(core->phy[0][MII_BMSR] & MII_BMSR_AN_COMP)) {
            e1000x_restart_autoneg(core->mac, core->phy[0],
                                   core->autoneg_timer);
        } else {
            e1000x_update_regs_on_link_up(core->mac, core->phy[0]);
            igb_start_recv(core);
        }
    }

    if (core->mac[STATUS] != old_status) {
        igb_set_interrupt_cause(core, E1000_ICR_LSC);
    }
}

static void
igb_set_ctrl(IGBCore *core, int index, uint32_t val)
{
    trace_e1000e_core_ctrl_write(index, val);

    /* RST is self clearing */
    core->mac[CTRL] = val & ~E1000_CTRL_RST;
    core->mac[CTRL_DUP] = core->mac[CTRL];

    trace_e1000e_link_set_params(
        !!(val & E1000_CTRL_ASDE),
        (val & E1000_CTRL_SPD_SEL) >> E1000_CTRL_SPD_SHIFT,
        !!(val & E1000_CTRL_FRCSPD),
        !!(val & E1000_CTRL_FRCDPX),
        !!(val & E1000_CTRL_RFCE),
        !!(val & E1000_CTRL_TFCE));

    if (val & E1000_CTRL_RST) {
        trace_e1000e_core_ctrl_sw_reset();
        igb_reset(core, true);
    }

    if (val & E1000_CTRL_PHY_RST) {
        trace_e1000e_core_ctrl_phy_reset();
        core->mac[STATUS] |= E1000_STATUS_PHYRA;
    }
}

static void
igb_set_rfctl(IGBCore *core, int index, uint32_t val)
{
    trace_e1000e_rx_set_rfctl(val);

    if (!(val & E1000_RFCTL_ISCSI_DIS)) {
        trace_e1000e_wrn_iscsi_filtering_not_supported();
    }

    if (!(val & E1000_RFCTL_NFSW_DIS)) {
        trace_e1000e_wrn_nfsw_filtering_not_supported();
    }

    if (!(val & E1000_RFCTL_NFSR_DIS)) {
        trace_e1000e_wrn_nfsr_filtering_not_supported();
    }

    core->mac[RFCTL] = val;
}

static void
igb_calc_per_desc_buf_size(IGBCore *core)
{
    int i;
    core->rx_desc_buf_size = 0;

    for (i = 0; i < ARRAY_SIZE(core->rxbuf_sizes); i++) {
        core->rx_desc_buf_size += core->rxbuf_sizes[i];
    }
}

static void
igb_parse_rxbufsize(IGBCore *core)
{
    uint32_t rctl = core->mac[RCTL];

    memset(core->rxbuf_sizes, 0, sizeof(core->rxbuf_sizes));

    if (rctl & E1000_RCTL_DTYP_MASK) {
        uint32_t bsize;

        bsize = core->mac[PSRCTL] & E1000_PSRCTL_BSIZE0_MASK;
        core->rxbuf_sizes[0] = (bsize >> E1000_PSRCTL_BSIZE0_SHIFT) * 128;

        bsize = core->mac[PSRCTL] & E1000_PSRCTL_BSIZE1_MASK;
        core->rxbuf_sizes[1] = (bsize >> E1000_PSRCTL_BSIZE1_SHIFT) * 1024;

        bsize = core->mac[PSRCTL] & E1000_PSRCTL_BSIZE2_MASK;
        core->rxbuf_sizes[2] = (bsize >> E1000_PSRCTL_BSIZE2_SHIFT) * 1024;

        bsize = core->mac[PSRCTL] & E1000_PSRCTL_BSIZE3_MASK;
        core->rxbuf_sizes[3] = (bsize >> E1000_PSRCTL_BSIZE3_SHIFT) * 1024;
    } else if (rctl & E1000_RCTL_FLXBUF_MASK) {
        int flxbuf = rctl & E1000_RCTL_FLXBUF_MASK;
        core->rxbuf_sizes[0] = (flxbuf >> E1000_RCTL_FLXBUF_SHIFT) * 1024;
    } else {
        core->rxbuf_sizes[0] = e1000x_rxbufsize(rctl);
    }

    trace_e1000e_rx_desc_buff_sizes(core->rxbuf_sizes[0], core->rxbuf_sizes[1],
                                    core->rxbuf_sizes[2], core->rxbuf_sizes[3]);

    igb_calc_per_desc_buf_size(core);
}

static void
igb_calc_rxdesclen(IGBCore *core)
{
    if (igb_rx_use_legacy_descriptor(core)) {
        core->rx_desc_len = sizeof(struct e1000_rx_desc);
    } else {
        if (core->mac[RCTL] & E1000_RCTL_DTYP_PS) {
            core->rx_desc_len = sizeof(union e1000_rx_desc_packet_split);
        } else {
            core->rx_desc_len = sizeof(union e1000_rx_desc_extended);
        }
    }
    trace_e1000e_rx_desc_len(core->rx_desc_len);
}

static void
igb_set_rx_control(IGBCore *core, int index, uint32_t val)
{
    core->mac[RCTL] = val;
    trace_e1000e_rx_set_rctl(core->mac[RCTL]);

    if (val & E1000_RCTL_EN) {
        igb_parse_rxbufsize(core);
        igb_calc_rxdesclen(core);
        core->rxbuf_min_shift = ((val / E1000_RCTL_RDMTS_QUAT) & 3) + 1 +
                                E1000_RING_DESC_LEN_SHIFT;

        igb_start_recv(core);
    }
}

static
void(*igb_phyreg_writeops[E1000E_PHY_PAGES][E1000E_PHY_PAGE_SIZE])
(IGBCore *, int, uint16_t) = {
    [0] = {
        [MII_BMCR]     = igb_set_phy_ctrl,
        [PHY_PAGE]     = igb_set_phy_page,
        [PHY_OEM_BITS] = igb_set_phy_oem_bits
    }
};

static inline void
igb_clear_ims_bits(IGBCore *core, uint32_t bits)
{
    trace_e1000e_irq_clear_ims(bits, core->mac[IMS], core->mac[IMS] & ~bits);
    core->mac[IMS] &= ~bits;
}

static inline bool
igb_postpone_interrupt(IGBIntrDelayTimer *timer)
{
    if (timer->running) {
        trace_e1000e_irq_postponed_by_xitr(timer->delay_reg << 2);

        return true;
    }

    if (timer->core->mac[timer->delay_reg] != 0) {
        igb_intrmgr_rearm_timer(timer);
    }

    return false;
}

static inline bool
igb_itr_should_postpone(IGBCore *core)
{
    return igb_postpone_interrupt(&core->itr);
}

static inline bool
igb_eitr_should_postpone(IGBCore *core, int idx)
{
    return igb_postpone_interrupt(&core->eitr[idx]);
}

static void
igb_msix_notify_one(IGBCore *core, uint32_t cause, uint32_t int_cfg)
{
    uint32_t effective_eiac;

    if (E1000_IVAR_ENTRY_VALID(int_cfg)) {
        uint32_t vec = E1000_IVAR_ENTRY_VEC(int_cfg);
        if (vec < IGB_MSIX_VEC_NUM) {
            if (!igb_eitr_should_postpone(core, vec)) {
                trace_e1000e_irq_msix_notify_vec(vec);
                msix_notify(core->owner, vec);
            }
        } else {
            trace_e1000e_wrn_msix_vec_wrong(cause, int_cfg);
        }
    } else {
        trace_e1000e_wrn_msix_invalid(cause, int_cfg);
    }

    if (core->mac[CTRL_EXT] & E1000_CTRL_EXT_EIAME) {
        trace_e1000e_irq_iam_clear_eiame(core->mac[IAM], cause);
        core->mac[IAM] &= ~cause;
    }

    trace_e1000e_irq_icr_clear_eiac(core->mac[ICR], core->mac[EIAC]);

    effective_eiac = core->mac[EIAC] & cause;

    core->mac[ICR] &= ~effective_eiac;
    core->msi_causes_pending &= ~effective_eiac;

    if (!(core->mac[CTRL_EXT] & E1000_CTRL_EXT_IAME)) {
        core->mac[IMS] &= ~effective_eiac;
    }
}

static void
igb_msix_notify(IGBCore *core, uint32_t causes)
{
    if (causes & E1000_ICR_RXQ0) {
        igb_msix_notify_one(core, E1000_ICR_RXQ0,
                               E1000_IVAR_RXQ0(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_RXQ1) {
        igb_msix_notify_one(core, E1000_ICR_RXQ1,
                               E1000_IVAR_RXQ1(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_TXQ0) {
        igb_msix_notify_one(core, E1000_ICR_TXQ0,
                               E1000_IVAR_TXQ0(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_TXQ1) {
        igb_msix_notify_one(core, E1000_ICR_TXQ1,
                               E1000_IVAR_TXQ1(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_OTHER) {
        igb_msix_notify_one(core, E1000_ICR_OTHER,
                               E1000_IVAR_OTHER(core->mac[IVAR]));
    }
}

static void
igb_msix_clear_one(IGBCore *core, uint32_t cause, uint32_t int_cfg)
{
    if (E1000_IVAR_ENTRY_VALID(int_cfg)) {
        uint32_t vec = E1000_IVAR_ENTRY_VEC(int_cfg);
        if (vec < IGB_MSIX_VEC_NUM) {
            trace_e1000e_irq_msix_pending_clearing(cause, int_cfg, vec);
            msix_clr_pending(core->owner, vec);
        } else {
            trace_e1000e_wrn_msix_vec_wrong(cause, int_cfg);
        }
    } else {
        trace_e1000e_wrn_msix_invalid(cause, int_cfg);
    }
}

static void
igb_msix_clear(IGBCore *core, uint32_t causes)
{
    if (causes & E1000_ICR_RXQ0) {
        igb_msix_clear_one(core, E1000_ICR_RXQ0,
                              E1000_IVAR_RXQ0(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_RXQ1) {
        igb_msix_clear_one(core, E1000_ICR_RXQ1,
                              E1000_IVAR_RXQ1(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_TXQ0) {
        igb_msix_clear_one(core, E1000_ICR_TXQ0,
                              E1000_IVAR_TXQ0(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_TXQ1) {
        igb_msix_clear_one(core, E1000_ICR_TXQ1,
                              E1000_IVAR_TXQ1(core->mac[IVAR]));
    }

    if (causes & E1000_ICR_OTHER) {
        igb_msix_clear_one(core, E1000_ICR_OTHER,
                              E1000_IVAR_OTHER(core->mac[IVAR]));
    }
}

static inline void
igb_fix_icr_asserted(IGBCore *core)
{
    core->mac[ICR] &= ~E1000_ICR_ASSERTED;
    if (core->mac[ICR]) {
        core->mac[ICR] |= E1000_ICR_ASSERTED;
    }

    trace_e1000e_irq_fix_icr_asserted(core->mac[ICR]);
}

static void
igb_send_msi(IGBCore *core, bool msix)
{
    uint32_t causes = core->mac[ICR] & core->mac[IMS] & ~E1000_ICR_ASSERTED;

    core->msi_causes_pending &= causes;
    causes ^= core->msi_causes_pending;
    if (causes == 0) {
        return;
    }
    core->msi_causes_pending |= causes;

    if (msix) {
        igb_msix_notify(core, causes);
    } else {
        if (!igb_itr_should_postpone(core)) {
            trace_e1000e_irq_msi_notify(causes);
            msi_notify(core->owner, 0);
        }
    }
}

static void
igb_update_interrupt_state(IGBCore *core)
{
    bool interrupts_pending;
    bool is_msix = msix_enabled(core->owner);

    /* Set ICR[OTHER] for MSI-X */
    if (is_msix) {
        if (core->mac[ICR] & E1000_ICR_OTHER_CAUSES) {
            core->mac[ICR] |= E1000_ICR_OTHER;
            trace_e1000e_irq_add_msi_other(core->mac[ICR]);
        }
    }

    igb_fix_icr_asserted(core);

    /*
     * Make sure ICR and ICS registers have the same value.
     * The spec says that the ICS register is write-only.  However in practice,
     * on real hardware ICS is readable, and for reads it has the same value as
     * ICR (except that ICS does not have the clear on read behaviour of ICR).
     *
     * The VxWorks PRO/1000 driver uses this behaviour.
     */
    core->mac[ICS] = core->mac[ICR];

    interrupts_pending = (core->mac[IMS] & core->mac[ICR]) ? true : false;
    if (!interrupts_pending) {
        core->msi_causes_pending = 0;
    }

    trace_e1000e_irq_pending_interrupts(core->mac[ICR] & core->mac[IMS],
                                        core->mac[ICR], core->mac[IMS]);

    if (is_msix || msi_enabled(core->owner)) {
        if (interrupts_pending) {
            igb_send_msi(core, is_msix);
        }
    } else {
        if (interrupts_pending) {
            if (!igb_itr_should_postpone(core)) {
                igb_raise_legacy_irq(core);
            }
        } else {
            igb_lower_legacy_irq(core);
        }
    }
}

static void
igb_set_interrupt_cause(IGBCore *core, uint32_t val)
{
    trace_e1000e_irq_set_cause_entry(val, core->mac[ICR]);

    val |= igb_intmgr_collect_delayed_causes(core);
    core->mac[ICR] |= val;

    trace_e1000e_irq_set_cause_exit(val, core->mac[ICR]);

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
        igb_set_interrupt_cause(core, E1000_ICR_LSC);
    }
}

static inline uint16_t
igb_get_reg_index_with_offset(const uint16_t *mac_reg_access, hwaddr addr)
{
    uint16_t index = (addr & 0x1ffff) >> 2;
    return index + (mac_reg_access[index] & 0xfffe);
}

static const char igb_phy_regcap[E1000E_PHY_PAGES][0x20] = {
    [0] = {
        [MII_BMCR]              = PHY_ANYPAGE | PHY_RW,
        [MII_BMSR]              = PHY_ANYPAGE | PHY_R,
        [MII_PHYID1]            = PHY_ANYPAGE | PHY_R,
        [MII_PHYID2]            = PHY_ANYPAGE | PHY_R,
        [MII_ANAR]              = PHY_ANYPAGE | PHY_RW,
        [MII_ANLPAR]            = PHY_ANYPAGE | PHY_R,
        [MII_ANER]              = PHY_ANYPAGE | PHY_R,
        [MII_ANNP]              = PHY_ANYPAGE | PHY_RW,
        [MII_ANLPRNP]           = PHY_ANYPAGE | PHY_R,
        [MII_CTRL1000]          = PHY_ANYPAGE | PHY_RW,
        [MII_STAT1000]          = PHY_ANYPAGE | PHY_R,
        [MII_EXTSTAT]           = PHY_ANYPAGE | PHY_R,
        [PHY_PAGE]              = PHY_ANYPAGE | PHY_RW,

        [PHY_COPPER_CTRL1]      = PHY_RW,
        [PHY_COPPER_STAT1]      = PHY_R,
        [PHY_COPPER_CTRL3]      = PHY_RW,
        [PHY_RX_ERR_CNTR]       = PHY_R,
        [PHY_OEM_BITS]          = PHY_RW,
        [PHY_BIAS_1]            = PHY_RW,
        [PHY_BIAS_2]            = PHY_RW,
        [PHY_COPPER_INT_ENABLE] = PHY_RW,
        [PHY_COPPER_STAT2]      = PHY_R,
        [PHY_COPPER_CTRL2]      = PHY_RW
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
    *page = (igb_phy_regcap[0][addr] & PHY_ANYPAGE) ? 0
                                                    : core->phy[0][PHY_PAGE];

    if (*page >= E1000E_PHY_PAGES) {
        return false;
    }

    return igb_phy_regcap[*page][addr] & cap;
}

static void
igb_phy_reg_write(IGBCore *core, uint8_t page, uint32_t addr, uint16_t data)
{
    assert(page < E1000E_PHY_PAGES);
    assert(addr < E1000E_PHY_PAGE_SIZE);

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
            trace_e1000e_core_mdic_read_unhandled(page, addr);
            val |= E1000_MDIC_ERROR;
        } else {
            val = (val ^ data) | core->phy[page][addr];
            trace_e1000e_core_mdic_read(page, addr, val);
        }
    } else if (val & E1000_MDIC_OP_WRITE) {
        if (!igb_phy_reg_check_cap(core, addr, PHY_W, &page)) {
            trace_e1000e_core_mdic_write_unhandled(page, addr);
            val |= E1000_MDIC_ERROR;
        } else {
            trace_e1000e_core_mdic_write(page, addr, data);
            igb_phy_reg_write(core, page, addr, data);
        }
    }
    core->mac[MDIC] = val | E1000_MDIC_READY;

    if (val & E1000_MDIC_INT_EN) {
        igb_set_interrupt_cause(core, E1000_ICR_MDAC);
    }
}

static void
igb_set_rdt(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & 0xffff;
    trace_e1000e_rx_set_rdt(igb_mq_queue_idx(RDT0, index), val);
    igb_start_recv(core);
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
    trace_e1000e_link_set_ext_params(!!(val & E1000_CTRL_EXT_ASDCHK),
                                     !!(val & E1000_CTRL_EXT_SPD_BYPS));

    /* Zero self-clearing bits */
    val &= ~(E1000_CTRL_EXT_ASDCHK | E1000_CTRL_EXT_EE_RST);
    core->mac[CTRL_EXT] = val;
}

static void
igb_set_pbaclr(IGBCore *core, int index, uint32_t val)
{
    int i;

    core->mac[PBACLR] = val & E1000_PBACLR_VALID_MASK;

    if (!msix_enabled(core->owner)) {
        return;
    }

    for (i = 0; i < IGB_MSIX_VEC_NUM; i++) {
        if (core->mac[PBACLR] & BIT(i)) {
            msix_clr_pending(core->owner, i);
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

#define IGB_LOW_BITS_SET_FUNC(num)                             \
    static void                                                \
    igb_set_##num##bit(IGBCore *core, int index, uint32_t val) \
    {                                                          \
        core->mac[index] = val & (BIT(num) - 1);               \
    }

IGB_LOW_BITS_SET_FUNC(4)
IGB_LOW_BITS_SET_FUNC(6)
IGB_LOW_BITS_SET_FUNC(11)
IGB_LOW_BITS_SET_FUNC(12)
IGB_LOW_BITS_SET_FUNC(13)
IGB_LOW_BITS_SET_FUNC(16)

static void
igb_set_vet(IGBCore *core, int index, uint32_t val)
{
    core->mac[VET] = val & 0xffff;
    trace_e1000e_vlan_vet(core->mac[VET]);
}

static void
igb_set_dlen(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & E1000_XDLEN_MASK;
}

static void
igb_set_dbal(IGBCore *core, int index, uint32_t val)
{
    core->mac[index] = val & E1000_XDBAL_MASK;
}

static void
igb_set_tctl(IGBCore *core, int index, uint32_t val)
{
    IGB_TxRing txr;
    core->mac[index] = val;

    if (core->mac[TARC0] & E1000_TARC_ENABLE) {
        igb_tx_ring_init(core, &txr, 0);
        igb_start_xmit(core, &txr);
    }

    if (core->mac[TARC1] & E1000_TARC_ENABLE) {
        igb_tx_ring_init(core, &txr, 1);
        igb_start_xmit(core, &txr);
    }
}

static void
igb_set_tdt(IGBCore *core, int index, uint32_t val)
{
    IGB_TxRing txr;
    int qidx = igb_mq_queue_idx(TDT, index);
    uint32_t tarc_reg = (qidx == 0) ? TARC0 : TARC1;

    core->mac[index] = val & 0xffff;

    if (core->mac[tarc_reg] & E1000_TARC_ENABLE) {
        igb_tx_ring_init(core, &txr, qidx);
        igb_start_xmit(core, &txr);
    }
}

static void
igb_set_ics(IGBCore *core, int index, uint32_t val)
{
    trace_e1000e_irq_write_ics(val);
    igb_set_interrupt_cause(core, val);
}

static void
igb_set_icr(IGBCore *core, int index, uint32_t val)
{
    uint32_t icr = 0;
    if ((core->mac[ICR] & E1000_ICR_ASSERTED) &&
        (core->mac[CTRL_EXT] & E1000_CTRL_EXT_IAME)) {
        trace_e1000e_irq_icr_process_iame();
        igb_clear_ims_bits(core, core->mac[IAM]);
    }

    icr = core->mac[ICR] & ~val;
    /*
     * Windows driver expects that the "receive overrun" bit and other
     * ones to be cleared when the "Other" bit (#24) is cleared.
     */
    icr = (val & E1000_ICR_OTHER) ? (icr & ~E1000_ICR_OTHER_CAUSES) : icr;
    trace_e1000e_irq_icr_write(val, core->mac[ICR], icr);
    core->mac[ICR] = icr;
    igb_update_interrupt_state(core);
}

static void
igb_set_imc(IGBCore *core, int index, uint32_t val)
{
    trace_e1000e_irq_ims_clear_set_imc(val);
    igb_clear_ims_bits(core, val);
    igb_update_interrupt_state(core);
}

static void
igb_set_ims(IGBCore *core, int index, uint32_t val)
{
    static const uint32_t ims_ext_mask =
        E1000_IMS_RXQ0 | E1000_IMS_RXQ1 |
        E1000_IMS_TXQ0 | E1000_IMS_TXQ1 |
        E1000_IMS_OTHER;

    static const uint32_t ims_valid_mask =
        E1000_IMS_TXDW      | E1000_IMS_TXQE    | E1000_IMS_LSC  |
        E1000_IMS_RXDMT0    | E1000_IMS_RXO     | E1000_IMS_RXT0 |
        E1000_IMS_MDAC      | E1000_IMS_TXD_LOW | E1000_IMS_SRPD |
        E1000_IMS_ACK       | E1000_IMS_MNG     | E1000_IMS_RXQ0 |
        E1000_IMS_RXQ1      | E1000_IMS_TXQ0    | E1000_IMS_TXQ1 |
        E1000_IMS_OTHER;

    uint32_t valid_val = val & ims_valid_mask;

    trace_e1000e_irq_set_ims(val, core->mac[IMS], core->mac[IMS] | valid_val);
    core->mac[IMS] |= valid_val;

    if ((valid_val & ims_ext_mask) &&
        (core->mac[CTRL_EXT] & E1000_CTRL_EXT_PBA_CLR) &&
        msix_enabled(core->owner)) {
        igb_msix_clear(core, valid_val);
    }

    if ((valid_val == ims_valid_mask) &&
        (core->mac[CTRL_EXT] & E1000_CTRL_EXT_INT_TIMERS_CLEAR_ENA)) {
        trace_e1000e_irq_fire_all_timers(val);
        igb_intrmgr_fire_all_timers(core);
    }

    igb_update_interrupt_state(core);
}

static void
igb_set_rdtr(IGBCore *core, int index, uint32_t val)
{
    igb_set_16bit(core, index, val);

    if ((val & E1000_RDTR_FPD) && (core->rdtr.running)) {
        trace_e1000e_irq_rdtr_fpd_running();
        igb_intrmgr_fire_delayed_interrupts(core);
    } else {
        trace_e1000e_irq_rdtr_fpd_not_running();
    }
}

static void
igb_set_tidv(IGBCore *core, int index, uint32_t val)
{
    igb_set_16bit(core, index, val);

    if ((val & E1000_TIDV_FPD) && (core->tidv.running)) {
        trace_e1000e_irq_tidv_fpd_running();
        igb_intrmgr_fire_delayed_interrupts(core);
    } else {
        trace_e1000e_irq_tidv_fpd_not_running();
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
    trace_e1000e_irq_read_ics(core->mac[ICS]);
    return core->mac[ICS];
}

static uint32_t
igb_mac_ims_read(IGBCore *core, int index)
{
    trace_e1000e_irq_read_ims(core->mac[IMS]);
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
igb_mac_itr_read(IGBCore *core, int index)
{
    return core->itr_guest_value;
}

static uint32_t
igb_mac_eitr_read(IGBCore *core, int index)
{
    return core->eitr_guest_value[index - EITR];
}

static uint32_t
igb_mac_icr_read(IGBCore *core, int index)
{
    uint32_t ret = core->mac[ICR];
    trace_e1000e_irq_icr_read_entry(ret);

    if (core->mac[IMS] == 0) {
        trace_e1000e_irq_icr_clear_zero_ims();
        core->mac[ICR] = 0;
    }

    if (!msix_enabled(core->owner)) {
        trace_e1000e_irq_icr_clear_nonmsix_icr_read();
        core->mac[ICR] = 0;
    }

    if ((core->mac[ICR] & E1000_ICR_ASSERTED) &&
        (core->mac[CTRL_EXT] & E1000_CTRL_EXT_IAME)) {
        trace_e1000e_irq_icr_clear_iame();
        core->mac[ICR] = 0;
        trace_e1000e_irq_icr_process_iame();
        igb_clear_ims_bits(core, core->mac[IAM]);
    }

    trace_e1000e_irq_icr_read_exit(core->mac[ICR]);
    igb_update_interrupt_state(core);
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

    trace_e1000e_link_read_params(
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
    uint32_t res = core->mac[STATUS];

    if (!(core->mac[CTRL] & E1000_CTRL_GIO_MASTER_DISABLE)) {
        res |= E1000_STATUS_GIO_MASTER_ENABLE;
    }

    if (core->mac[CTRL] & E1000_CTRL_FRCDPX) {
        res |= (core->mac[CTRL] & E1000_CTRL_FD) ? E1000_STATUS_FD : 0;
    } else {
        res |= E1000_STATUS_FD;
    }

    if ((core->mac[CTRL] & E1000_CTRL_FRCSPD) ||
        (core->mac[CTRL_EXT] & E1000_CTRL_EXT_SPD_BYPS)) {
        switch (core->mac[CTRL] & E1000_CTRL_SPD_SEL) {
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

    trace_e1000e_link_status(
        !!(res & E1000_STATUS_LU),
        !!(res & E1000_STATUS_FD),
        (res & E1000_STATUS_SPEED_MASK) >> E1000_STATUS_SPEED_SHIFT,
        (res & E1000_STATUS_ASDV) >> E1000_STATUS_ASDV_SHIFT);

    return res;
}

static uint32_t
igb_get_tarc(IGBCore *core, int index)
{
    return core->mac[index] & ((BIT(11) - 1) |
                                BIT(27)      |
                                BIT(28)      |
                                BIT(29)      |
                                BIT(30));
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

    trace_e1000e_mac_set_sw(MAC_ARG(macaddr));
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
igb_set_rxdctl(IGBCore *core, int index, uint32_t val)
{
    core->mac[RXDCTL] = core->mac[RXDCTL1] = val;
}

static void
igb_set_itr(IGBCore *core, int index, uint32_t val)
{
    uint32_t interval = val & 0xffff;

    trace_e1000e_irq_itr_set(val);

    core->itr_guest_value = interval;
    core->mac[index] = MAX(interval, E1000E_MIN_XITR);
}

static void
igb_set_eitr(IGBCore *core, int index, uint32_t val)
{
    uint32_t interval = val & 0xffff;
    uint32_t eitr_num = index - EITR;

    trace_e1000e_irq_eitr_set(eitr_num, val);

    core->eitr_guest_value[eitr_num] = interval;
    core->mac[index] = MAX(interval, E1000E_MIN_XITR);
}

static void
igb_set_psrctl(IGBCore *core, int index, uint32_t val)
{
    if (core->mac[RCTL] & E1000_RCTL_DTYP_MASK) {

        if ((val & E1000_PSRCTL_BSIZE0_MASK) == 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "igb: PSRCTL.BSIZE0 cannot be zero");
            return;
        }

        if ((val & E1000_PSRCTL_BSIZE1_MASK) == 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "igb: PSRCTL.BSIZE1 cannot be zero");
            return;
        }
    }

    core->mac[PSRCTL] = val;
}

static void
igb_update_rx_offloads(IGBCore *core)
{
    int cso_state = igb_rx_l4_cso_enabled(core);

    trace_e1000e_rx_set_cso(cso_state);

    if (core->has_vnet) {
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

#define igb_getreg(x)    [x] = igb_mac_readreg
typedef uint32_t (*readops)(IGBCore *, int);
static const readops igb_macreg_readops[] = {
    igb_getreg(PBA),
    igb_getreg(WUFC),
    igb_getreg(MANC),
    igb_getreg(TOTL),
    igb_getreg(RDT0),
    igb_getreg(RDBAH0),
    igb_getreg(TDBAL1),
    igb_getreg(RDLEN0),
    igb_getreg(RDH1),
    igb_getreg(LATECOL),
    igb_getreg(SEQEC),
    igb_getreg(XONTXC),
    igb_getreg(AIT),
    igb_getreg(TDFH),
    igb_getreg(TDFT),
    igb_getreg(TDFHS),
    igb_getreg(TDFTS),
    igb_getreg(TDFPC),
    igb_getreg(WUS),
    igb_getreg(PBS),
    igb_getreg(RDFH),
    igb_getreg(RDFT),
    igb_getreg(RDFHS),
    igb_getreg(RDFTS),
    igb_getreg(RDFPC),
    igb_getreg(GORCL),
    igb_getreg(MGTPRC),
    igb_getreg(EERD),
    igb_getreg(EIAC),
    igb_getreg(PSRCTL),
    igb_getreg(MANC2H),
    igb_getreg(RXCSUM),
    igb_getreg(GSCL_3),
    igb_getreg(GSCN_2),
    igb_getreg(RSRPD),
    igb_getreg(RDBAL1),
    igb_getreg(FCAH),
    igb_getreg(FCRTH),
    igb_getreg(FLOP),
    igb_getreg(FLASHT),
    igb_getreg(RXSTMPH),
    igb_getreg(TXSTMPL),
    igb_getreg(TIMADJL),
    igb_getreg(TXDCTL),
    igb_getreg(RDH0),
    igb_getreg(TDT1),
    igb_getreg(TNCRS),
    igb_getreg(RJC),
    igb_getreg(IAM),
    igb_getreg(GSCL_2),
    igb_getreg(RDBAH1),
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
    igb_getreg(TDH),
    igb_getreg(LEDCTL),
    igb_getreg(TCTL),
    igb_getreg(TDBAL),
    igb_getreg(TDLEN),
    igb_getreg(TDH1),
    igb_getreg(RADV),
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
    igb_getreg(PBACLR),
    igb_getreg(FCTTV),
    igb_getreg(EEWR),
    igb_getreg(FLSWCTL),
    igb_getreg(RXDCTL1),
    igb_getreg(RXSATRL),
    igb_getreg(SYSTIML),
    igb_getreg(RXUDP),
    igb_getreg(TORL),
    igb_getreg(TDLEN1),
    igb_getreg(MCC),
    igb_getreg(WUC),
    igb_getreg(EECD),
    igb_getreg(MFUTP23),
    igb_getreg(RAID),
    igb_getreg(FCRTV),
    igb_getreg(TXDCTL1),
    igb_getreg(RCTL),
    igb_getreg(TDT),
    igb_getreg(MDIC),
    igb_getreg(FCRUC),
    igb_getreg(VET),
    igb_getreg(RDBAL0),
    igb_getreg(TDBAH1),
    igb_getreg(RDTR),
    igb_getreg(SCC),
    igb_getreg(COLC),
    igb_getreg(CEXTERR),
    igb_getreg(XOFFRXC),
    igb_getreg(IPAV),
    igb_getreg(GOTCL),
    igb_getreg(MGTPDC),
    igb_getreg(GCR),
    igb_getreg(IVAR),
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
    igb_getreg(RXDCTL),
    igb_getreg(RXSTMPL),
    igb_getreg(TXSTMPH),
    igb_getreg(TIMADJH),
    igb_getreg(FCRTL),
    igb_getreg(TDBAH),
    igb_getreg(TADV),
    igb_getreg(XONRXC),
    igb_getreg(TSCTFC),
    igb_getreg(RFCTL),
    igb_getreg(GSCN_1),
    igb_getreg(FCAL),
    igb_getreg(FLSWCNT),

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
    [STATUS]  = igb_get_status,
    [TARC0]   = igb_get_tarc,
    [ICS]     = igb_mac_ics_read,
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
    [ITR]     = igb_mac_itr_read,
    [CTRL]    = igb_get_ctrl,
    [TARC1]   = igb_get_tarc,
    [SWSM]    = igb_mac_swsm_read,
    [IMS]     = igb_mac_ims_read,

    [CRCERRS ... MPC]      = igb_mac_readreg,
    [IP6AT ... IP6AT + 3]  = igb_mac_readreg,
    [IP4AT ... IP4AT + 6]  = igb_mac_readreg,
    [RA ... RA + 31]       = igb_mac_readreg,
    [WUPM ... WUPM + 31]   = igb_mac_readreg,
    [MTA ... MTA + E1000_MC_TBL_SIZE - 1] = igb_mac_readreg,
    [VFTA ... VFTA + E1000_VLAN_FILTER_TBL_SIZE - 1]  = igb_mac_readreg,
    [FFMT ... FFMT + 254]  = igb_mac_readreg,
    [FFVT ... FFVT + 254]  = igb_mac_readreg,
    [MDEF ... MDEF + 7]    = igb_mac_readreg,
    [FFLT ... FFLT + 10]   = igb_mac_readreg,
    [FTFT ... FTFT + 254]  = igb_mac_readreg,
    [PBM ... PBM + 10239]  = igb_mac_readreg,
    [RETA ... RETA + 31]   = igb_mac_readreg,
    [RSSRK ... RSSRK + 31] = igb_mac_readreg,
    [MAVTV0 ... MAVTV3]    = igb_mac_readreg,
    [EITR...EITR + IGB_MSIX_VEC_NUM - 1] = igb_mac_eitr_read
};
enum { IGB_NREADOPS = ARRAY_SIZE(igb_macreg_readops) };

#define igb_putreg(x)    [x] = igb_mac_writereg
typedef void (*writeops)(IGBCore *, int, uint32_t);
static const writeops igb_macreg_writeops[] = {
    igb_putreg(PBA),
    igb_putreg(SWSM),
    igb_putreg(WUFC),
    igb_putreg(RDBAH1),
    igb_putreg(TDBAH),
    igb_putreg(TXDCTL),
    igb_putreg(RDBAH0),
    igb_putreg(LEDCTL),
    igb_putreg(FCAL),
    igb_putreg(FCRUC),
    igb_putreg(WUC),
    igb_putreg(WUS),
    igb_putreg(IPAV),
    igb_putreg(TDBAH1),
    igb_putreg(TIMINCA),
    igb_putreg(IAM),
    igb_putreg(EIAC),
    igb_putreg(IVAR),
    igb_putreg(TARC0),
    igb_putreg(TARC1),
    igb_putreg(FLSWDATA),
    igb_putreg(POEMB),
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
    igb_putreg(RXDCTL1),
    igb_putreg(TXDCTL1),
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

    [TDH1]     = igb_set_16bit,
    [TDT1]     = igb_set_tdt,
    [TCTL]     = igb_set_tctl,
    [TDT]      = igb_set_tdt,
    [MDIC]     = igb_set_mdic,
    [ICS]      = igb_set_ics,
    [TDH]      = igb_set_16bit,
    [RDH0]     = igb_set_16bit,
    [RDT0]     = igb_set_rdt,
    [IMC]      = igb_set_imc,
    [IMS]      = igb_set_ims,
    [ICR]      = igb_set_icr,
    [EECD]     = igb_set_eecd,
    [RCTL]     = igb_set_rx_control,
    [CTRL]     = igb_set_ctrl,
    [RDTR]     = igb_set_rdtr,
    [RADV]     = igb_set_16bit,
    [TADV]     = igb_set_16bit,
    [ITR]      = igb_set_itr,
    [EERD]     = igb_set_eerd,
    [AIT]      = igb_set_16bit,
    [TDFH]     = igb_set_13bit,
    [TDFT]     = igb_set_13bit,
    [TDFHS]    = igb_set_13bit,
    [TDFTS]    = igb_set_13bit,
    [TDFPC]    = igb_set_13bit,
    [RDFH]     = igb_set_13bit,
    [RDFHS]    = igb_set_13bit,
    [RDFT]     = igb_set_13bit,
    [RDFTS]    = igb_set_13bit,
    [RDFPC]    = igb_set_13bit,
    [PBS]      = igb_set_6bit,
    [GCR]      = igb_set_gcr,
    [PSRCTL]   = igb_set_psrctl,
    [RXCSUM]   = igb_set_rxcsum,
    [RAID]     = igb_set_16bit,
    [RSRPD]    = igb_set_12bit,
    [TIDV]     = igb_set_tidv,
    [TDLEN1]   = igb_set_dlen,
    [TDLEN]    = igb_set_dlen,
    [RDLEN0]   = igb_set_dlen,
    [RDLEN1]   = igb_set_dlen,
    [TDBAL]    = igb_set_dbal,
    [TDBAL1]   = igb_set_dbal,
    [RDBAL0]   = igb_set_dbal,
    [RDBAL1]   = igb_set_dbal,
    [RDH1]     = igb_set_16bit,
    [RDT1]     = igb_set_rdt,
    [STATUS]   = igb_set_status,
    [PBACLR]   = igb_set_pbaclr,
    [CTRL_EXT] = igb_set_ctrlext,
    [FCAH]     = igb_set_16bit,
    [FCT]      = igb_set_16bit,
    [FCTTV]    = igb_set_16bit,
    [FCRTV]    = igb_set_16bit,
    [FCRTH]    = igb_set_fcrth,
    [FCRTL]    = igb_set_fcrtl,
    [VET]      = igb_set_vet,
    [RXDCTL]   = igb_set_rxdctl,
    [FLASHT]   = igb_set_16bit,
    [EEWR]     = igb_set_eewr,
    [CTRL_DUP] = igb_set_ctrl,
    [RFCTL]    = igb_set_rfctl,
    [RA + 1]   = igb_mac_setmacaddr,

    [IP6AT ... IP6AT + 3]    = igb_mac_writereg,
    [IP4AT ... IP4AT + 6]    = igb_mac_writereg,
    [RA + 2 ... RA + 31]     = igb_mac_writereg,
    [WUPM ... WUPM + 31]     = igb_mac_writereg,
    [MTA ... MTA + E1000_MC_TBL_SIZE - 1] = igb_mac_writereg,
    [VFTA ... VFTA + E1000_VLAN_FILTER_TBL_SIZE - 1]    = igb_mac_writereg,
    [FFMT ... FFMT + 254]    = igb_set_4bit,
    [FFVT ... FFVT + 254]    = igb_mac_writereg,
    [PBM ... PBM + 10239]    = igb_mac_writereg,
    [MDEF ... MDEF + 7]      = igb_mac_writereg,
    [FFLT ... FFLT + 10]     = igb_set_11bit,
    [FTFT ... FTFT + 254]    = igb_mac_writereg,
    [RETA ... RETA + 31]     = igb_mac_writereg,
    [RSSRK ... RSSRK + 31]   = igb_mac_writereg,
    [MAVTV0 ... MAVTV3]      = igb_mac_writereg,
    [EITR...EITR + IGB_MSIX_VEC_NUM - 1] = igb_set_eitr
};
enum { IGB_NWRITEOPS = ARRAY_SIZE(igb_macreg_writeops) };

enum { MAC_ACCESS_PARTIAL = 1 };

/*
 * The array below combines alias offsets of the index values for the
 * MAC registers that have aliases, with the indication of not fully
 * implemented registers (lowest bit). This combination is possible
 * because all of the offsets are even.
 */
static const uint16_t mac_reg_access[E1000E_MAC_SIZE] = {
    /* Alias index offsets */
    [FCRTL_A] = 0x07fe, [FCRTH_A] = 0x0802,
    [RDH0_A]  = 0x09bc, [RDT0_A]  = 0x09bc, [RDTR_A] = 0x09c6,
    [RDFH_A]  = 0xe904, [RDFT_A]  = 0xe904,
    [TDH_A]   = 0x0cf8, [TDT_A]   = 0x0cf8, [TIDV_A] = 0x0cf8,
    [TDFH_A]  = 0xed00, [TDFT_A]  = 0xed00,
    [RA_A ... RA_A + 31]      = 0x14f0,
    [VFTA_A ... VFTA_A + E1000_VLAN_FILTER_TBL_SIZE - 1] = 0x1400,
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
    [FCRTH] = MAC_ACCESS_PARTIAL,    [TXDCTL] = MAC_ACCESS_PARTIAL,
    [TXDCTL1] = MAC_ACCESS_PARTIAL,
    [MAVTV0 ... MAVTV3] = MAC_ACCESS_PARTIAL
};

void
igb_core_write(IGBCore *core, hwaddr addr, uint64_t val, unsigned size)
{
    uint16_t index = igb_get_reg_index_with_offset(mac_reg_access, addr);

    if (index < IGB_NWRITEOPS && igb_macreg_writeops[index]) {
        if (mac_reg_access[index] & MAC_ACCESS_PARTIAL) {
            trace_e1000e_wrn_regs_write_trivial(index << 2);
        }
        trace_e1000e_core_write(index << 2, size, val);
        igb_macreg_writeops[index](core, index, val);
    } else if (index < IGB_NREADOPS && igb_macreg_readops[index]) {
        trace_e1000e_wrn_regs_write_ro(index << 2, size, val);
    } else {
        trace_e1000e_wrn_regs_write_unknown(index << 2, size, val);
    }
}

uint64_t
igb_core_read(IGBCore *core, hwaddr addr, unsigned size)
{
    uint64_t val;
    uint16_t index = igb_get_reg_index_with_offset(mac_reg_access, addr);

    if (index < IGB_NREADOPS && igb_macreg_readops[index]) {
        if (mac_reg_access[index] & MAC_ACCESS_PARTIAL) {
            trace_e1000e_wrn_regs_read_trivial(index << 2);
        }
        val = igb_macreg_readops[index](core, index);
        trace_e1000e_core_read(index << 2, size, val);
        return val;
    } else {
        trace_e1000e_wrn_regs_read_unknown(index << 2, size);
    }
    return 0;
}

static inline void
igb_autoneg_pause(IGBCore *core)
{
    timer_del(core->autoneg_timer);
}

static void
igb_autoneg_resume(IGBCore *core)
{
    if (igb_have_autoneg(core) &&
        !(core->phy[0][MII_BMSR] & MII_BMSR_AN_COMP)) {
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
        trace_e1000e_vm_state_running();
        igb_intrmgr_resume(core);
        igb_autoneg_resume(core);
    } else {
        trace_e1000e_vm_state_stopped();
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
    int i;

    core->autoneg_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                       igb_autoneg_timer, core);
    igb_intrmgr_pci_realize(core);

    core->vmstate = qemu_add_vm_change_state_handler(igb_vm_state_change, core);

    for (i = 0; i < IGB_NUM_QUEUES; i++) {
        net_tx_pkt_init(&core->tx[i].tx_pkt, core->owner, E1000E_MAX_TX_FRAGS);
    }

    net_rx_pkt_init(&core->rx_pkt);

    e1000x_core_prepare_eeprom(core->eeprom,
                               eeprom_templ,
                               eeprom_size,
                               PCI_DEVICE_GET_CLASS(core->owner)->device_id,
                               macaddr);
    igb_update_rx_offloads(core);
}

void
igb_core_pci_uninit(IGBCore *core)
{
    int i;

    timer_free(core->autoneg_timer);

    igb_intrmgr_pci_unint(core);

    qemu_del_vm_change_state_handler(core->vmstate);

    for (i = 0; i < IGB_NUM_QUEUES; i++) {
        net_tx_pkt_reset(core->tx[i].tx_pkt);
        net_tx_pkt_uninit(core->tx[i].tx_pkt);
    }

    net_rx_pkt_uninit(core->rx_pkt);
}

static const uint16_t
igb_phy_reg_init[E1000E_PHY_PAGES][E1000E_PHY_PAGE_SIZE] = {
    [0] = {
        [MII_BMCR] = MII_BMCR_SPEED1000 |
                     MII_BMCR_FD        |
                     MII_BMCR_AUTOEN,

        [MII_BMSR] = MII_BMSR_EXTCAP    |
                     MII_BMSR_LINK_ST   |
                     MII_BMSR_AUTONEG   |
                     MII_BMSR_MFPS      |
                     MII_BMSR_EXTSTAT   |
                     MII_BMSR_10T_HD    |
                     MII_BMSR_10T_FD    |
                     MII_BMSR_100TX_HD  |
                     MII_BMSR_100TX_FD,

        [MII_PHYID1]            = 0x141,
        [MII_PHYID2]            = E1000_PHY_ID2_82574x,
        [MII_ANAR]              = MII_ANAR_CSMACD | MII_ANAR_10 |
                                  MII_ANAR_10FD | MII_ANAR_TX |
                                  MII_ANAR_TXFD | MII_ANAR_PAUSE |
                                  MII_ANAR_PAUSE_ASYM,
        [MII_ANLPAR]            = MII_ANLPAR_10 | MII_ANLPAR_10FD |
                                  MII_ANLPAR_TX | MII_ANLPAR_TXFD |
                                  MII_ANLPAR_T4 | MII_ANLPAR_PAUSE,
        [MII_ANER]              = MII_ANER_NP | MII_ANER_NWAY,
        [MII_ANNP]              = 1 | MII_ANNP_MP,
        [MII_CTRL1000]          = MII_CTRL1000_HALF | MII_CTRL1000_FULL |
                                  MII_CTRL1000_PORT | MII_CTRL1000_MASTER,
        [MII_STAT1000]          = MII_STAT1000_HALF | MII_STAT1000_FULL |
                                  MII_STAT1000_ROK | MII_STAT1000_LOK,
        [MII_EXTSTAT]           = MII_EXTSTAT_1000T_HD | MII_EXTSTAT_1000T_FD,

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
    [EEMNGCTL]      = BIT(31),
    [FLASHT]        = 0x2,
    [FLSWCTL]       = BIT(30) | BIT(31),
    [FLOL]          = BIT(0),
    [RXDCTL]        = BIT(16),
    [RXDCTL1]       = BIT(16),
    [TIPG]          = 0x8 | (0x8 << 10) | (0x6 << 20),
    [RXCFGL]        = 0x88F7,
    [RXUDP]         = 0x319,
    [CTRL]          = E1000_CTRL_FD | E1000_CTRL_SWDPIN2 | E1000_CTRL_SWDPIN0 |
                      E1000_CTRL_SPD_1000 | E1000_CTRL_SLU |
                      E1000_CTRL_ADVD3WUC,
    [STATUS]        =  E1000_STATUS_ASDV_1000 | E1000_STATUS_LU,
    [PSRCTL]        = (2 << E1000_PSRCTL_BSIZE0_SHIFT) |
                      (4 << E1000_PSRCTL_BSIZE1_SHIFT) |
                      (4 << E1000_PSRCTL_BSIZE2_SHIFT),
    [TARC0]         = 0x3 | E1000_TARC_ENABLE,
    [TARC1]         = 0x3 | E1000_TARC_ENABLE,
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
    [MANC]          = E1000_MANC_DIS_IP_CHK_ARP,
    [FACTPS]        = E1000_FACTPS_LAN0_ON | 0x20000000,
    [SWSM]          = 1,
    [RXCSUM]        = E1000_RXCSUM_IPOFLD | E1000_RXCSUM_TUOFLD,
    [ITR]           = E1000E_MIN_XITR,
    [EITR...EITR + IGB_MSIX_VEC_NUM - 1] = E1000E_MIN_XITR,
};

static void igb_reset(IGBCore *core, bool sw)
{
    int i;

    timer_del(core->autoneg_timer);

    igb_intrmgr_reset(core);

    memset(core->phy, 0, sizeof core->phy);
    memcpy(core->phy, igb_phy_reg_init, sizeof igb_phy_reg_init);

    for (i = 0; i < E1000E_MAC_SIZE; i++) {
        if (sw && (i == PBA || i == PBS || i == FLA)) {
            continue;
        }

        core->mac[i] = i < ARRAY_SIZE(igb_mac_reg_init) ?
                       igb_mac_reg_init[i] : 0;
    }

    core->rxbuf_min_shift = 1 + E1000_RING_DESC_LEN_SHIFT;

    if (qemu_get_queue(core->owner_nic)->link_down) {
        igb_link_down(core);
    }

    e1000x_reset_mac_addr(core->owner_nic, core->mac, core->permanent_mac);

    for (i = 0; i < ARRAY_SIZE(core->tx); i++) {
        net_tx_pkt_reset(core->tx[i].tx_pkt);
        memset(&core->tx[i].props, 0, sizeof(core->tx[i].props));
        core->tx[i].skip_cp = false;
    }
}

void
igb_core_reset(IGBCore *core)
{
    igb_reset(core, false);
}

void igb_core_pre_save(IGBCore *core)
{
    int i;
    NetClientState *nc = qemu_get_queue(core->owner_nic);

    /*
     * If link is down and auto-negotiation is supported and ongoing,
     * complete auto-negotiation immediately. This allows us to look
     * at MII_BMSR_AN_COMP to infer link status on load.
     */
    if (nc->link_down && igb_have_autoneg(core)) {
        core->phy[0][MII_BMSR] |= MII_BMSR_AN_COMP;
        igb_update_flowctl_status(core);
    }

    for (i = 0; i < ARRAY_SIZE(core->tx); i++) {
        if (net_tx_pkt_has_fragments(core->tx[i].tx_pkt)) {
            core->tx[i].skip_cp = true;
        }
    }
}

int
igb_core_post_load(IGBCore *core)
{
    NetClientState *nc = qemu_get_queue(core->owner_nic);

    /*
     * nc.link_down can't be migrated, so infer link_down according
     * to link status bit in core.mac[STATUS].
     */
    nc->link_down = (core->mac[STATUS] & E1000_STATUS_LU) == 0;

    return 0;
}
