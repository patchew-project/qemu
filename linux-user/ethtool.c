/*
 *  Linux ioctl system call SIOCETHTOOL requests
 *
 *  Copyright (c) 2020 Shu-Chun Weng
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include <stdio.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/unistd.h>
#ifdef HAVE_BTRFS_H
/*
 * Presence of the BTRFS macros affects the STRUCT_* macro values because the
 * order in syscall_types.h matters but some entries are dropped when the BTRFS
 * macros are not defined. We must include linux/btrfs.h before any file first
 * transitively include syscall_types.h.
 */
#include <linux/btrfs.h>
#endif
#include "ethtool.h"
#include "qemu.h"

/* Non-standard ethtool structure definitions. */
/*
 * struct ethtool_rxnfc {
 *     __u32 cmd;
 *     __u32 flow_type;
 *     __u64 data;
 *     struct ethtool_rx_flow_spec fs;
 *     union {
 *         __u32 rule_cnt;
 *         __u32 rss_context;
 *     };
 *     __u32 rule_locs[0];
 * };
 *
 * Originally defined for ETHTOOL_{G,S}RXFH with only the cmd, flow_type and
 * data members. For other commands, dedicated standard structure definitions
 * are listed in syscall_types.h.
 */
static void host_to_target_ethtool_rxnfc_get_set_rxfh(void *dst,
                                                      const void *src)
{
    static const argtype ethtool_rx_flow_spec_argtype[] = {
        MK_STRUCT(STRUCT_ethtool_rx_flow_spec), TYPE_NULL };
    struct ethtool_rxnfc *target = dst;
    const struct ethtool_rxnfc *host = src;

    target->cmd = tswap32(host->cmd);
    target->flow_type = tswap32(host->flow_type);
    target->data = tswap64(host->data);

    if (host->cmd == ETHTOOL_SRXFH) {
        /*
         * struct ethtool_rxnfc was originally defined for ETHTOOL_{G,S}RXFH
         * with only the cmd, flow_type and data members. Guest program might
         * still be using that definition.
         */
        return;
    }
    if (host->cmd != ETHTOOL_GRXFH) {
        fprintf(stderr, "host_to_target_ethtool_rxnfc_get_set_rxfh called with "
                "command 0x%x which is not ETHTOOL_SRXFH or ETHTOOL_GRXFH\n",
                host->cmd);
    }
    if ((host->flow_type & FLOW_RSS) == 0) {
        return;
    }
    /*
     * If `FLOW_RSS` was requested then guest program must be using the new
     * definition.
     */
    thunk_convert(&target->fs, &host->fs, ethtool_rx_flow_spec_argtype,
                  THUNK_TARGET);
    target->rule_cnt = tswap32(host->rule_cnt);
}

static void target_to_host_ethtool_rxnfc_get_set_rxfh(void *dst,
                                                      const void *src)
{
    static const argtype ethtool_rx_flow_spec_argtype[] = {
        MK_STRUCT(STRUCT_ethtool_rx_flow_spec), TYPE_NULL };
    struct ethtool_rxnfc *host = dst;
    const struct ethtool_rxnfc *target = src;

    host->cmd = tswap32(target->cmd);
    host->flow_type = tswap32(target->flow_type);
    host->data = tswap64(target->data);

    if (host->cmd == ETHTOOL_SRXFH) {
        /*
         * struct ethtool_rxnfc was originally defined for ETHTOOL_{G,S}RXFH
         * with only the cmd, flow_type and data members. Guest program might
         * still be using that definition.
         */
        return;
    }
    if (host->cmd != ETHTOOL_GRXFH) {
        fprintf(stderr, "target_to_host_ethtool_rxnfc_get_set_rxfh called with "
                "command 0x%x which is not ETHTOOL_SRXFH or ETHTOOL_GRXFH\n",
                host->cmd);
    }
    if ((host->flow_type & FLOW_RSS) == 0) {
        return;
    }
    /*
     * If `FLOW_RSS` was requested then guest program must be using the new
     * definition.
     */
    thunk_convert(&host->fs, &target->fs, ethtool_rx_flow_spec_argtype,
                  THUNK_HOST);
    host->rule_cnt = tswap32(target->rule_cnt);
}

static int target_ethtool_rxnfc_get_set_rxfh_size(const void *src)
{
    const struct ethtool_rxnfc *target = src;
    int cmd = tswap32(target->cmd);
    if (cmd == ETHTOOL_SRXFH ||
        (cmd == ETHTOOL_GRXFH &&
         (tswap32(target->flow_type) & FLOW_RSS) == 0)) {
        return 16;
    }
    return sizeof(struct ethtool_rxnfc);
}

static int host_ethtool_rxnfc_get_set_rxfh_size(const void *src)
{
    const struct ethtool_rxnfc *host = src;
    if (host->cmd == ETHTOOL_SRXFH ||
        (host->cmd == ETHTOOL_GRXFH && (host->flow_type & FLOW_RSS) == 0)) {
        return 16;
    }
    return sizeof(struct ethtool_rxnfc);
}

const StructEntry struct_ethtool_rxnfc_get_set_rxfh_def = {
    .convert = {
        host_to_target_ethtool_rxnfc_get_set_rxfh,
        target_to_host_ethtool_rxnfc_get_set_rxfh },
    .thunk_size = {
        target_ethtool_rxnfc_get_set_rxfh_size,
        host_ethtool_rxnfc_get_set_rxfh_size },
    .size = { 16, 16 },
    .align = {
        __alignof__(struct ethtool_rxnfc),
        __alignof__(struct ethtool_rxnfc) },
};

/*
 * struct ethtool_sset_info {
 *     __u32 cmd;
 *     __u32 reserved;
 *     __u64 sset_mask;
 *     __u32 data[0];
 * };
 *
 * `sset_mask` is a bitmask of string sets. `data` is the buffer for string set
 * sizes, containing number of 1s in `sset_mask`'s binary representation number
 * of 4-byte entries.
 *
 * Since all fields are fixed-width and number of 1s in `sset_mask` does not
 * change between architectures, host-to-target and target-to-host are
 * identical.
 */
static void convert_ethtool_sset_info(void *dst, const void *src)
{
    int i, set_count;
    struct ethtool_sset_info *dst_sset_info = dst;
    const struct ethtool_sset_info *src_sset_info = src;

    dst_sset_info->cmd = tswap32(src_sset_info->cmd);
    dst_sset_info->sset_mask = tswap64(src_sset_info->sset_mask);

    set_count = ctpop64(src_sset_info->sset_mask);
    for (i = 0; i < set_count; ++i) {
        dst_sset_info->data[i] = tswap32(src_sset_info->data[i]);
    }
}

static int ethtool_sset_info_size(const void *src)
{
    const struct ethtool_sset_info *src_sset_info = src;
    return sizeof(struct ethtool_sset_info) +
        ctpop64(src_sset_info->sset_mask) * sizeof(src_sset_info->data[0]);
}

const StructEntry struct_ethtool_sset_info_def = {
    .convert = {
        convert_ethtool_sset_info, convert_ethtool_sset_info },
    .thunk_size = {
        ethtool_sset_info_size, ethtool_sset_info_size },
    .size = {
        sizeof(struct ethtool_sset_info),
        sizeof(struct ethtool_sset_info) },
    .align = {
        __alignof__(struct ethtool_sset_info),
        __alignof__(struct ethtool_sset_info) },
};

/*
 * struct ethtool_rxfh {
 *     __u32 cmd;
 *     __u32 rss_context;
 *     __u32 indir_size;
 *     __u32 key_size;
 *     __u8  hfunc;
 *     __u8  rsvd8[3];
 *     __u32 rsvd32;
 *     __u32 rss_config[0];
 * };
 *
 * `rss_config`: indirection table of `indir_size` __u32 elements, followed by
 * hash key of `key_size` bytes.
 *
 * `indir_size` could be ETH_RXFH_INDIR_NO_CHANGE when `cmd` is ETHTOOL_SRSSH
 * and there would be no indircetion table in `rss_config`.
 */
static void convert_ethtool_rxfh_header(void *dst, const void *src)
{
    struct ethtool_rxfh *dst_rxfh = dst;
    const struct ethtool_rxfh *src_rxfh = src;

    dst_rxfh->cmd = tswap32(src_rxfh->cmd);
    dst_rxfh->rss_context = tswap32(src_rxfh->rss_context);
    dst_rxfh->indir_size = tswap32(src_rxfh->indir_size);
    dst_rxfh->key_size = tswap32(src_rxfh->key_size);
    dst_rxfh->hfunc = src_rxfh->hfunc;
    dst_rxfh->rsvd8[0] = src_rxfh->rsvd8[0];
    dst_rxfh->rsvd8[1] = src_rxfh->rsvd8[1];
    dst_rxfh->rsvd8[2] = src_rxfh->rsvd8[2];
    dst_rxfh->rsvd32 = tswap32(src_rxfh->rsvd32);
}

static void convert_ethtool_rxfh_rss_config(
    void *dst, const void *src, uint32_t indir_size, uint32_t key_size) {
    uint32_t *dst_rss_config = (uint32_t *)dst;
    const uint32_t *src_rss_config = (const uint32_t *)src;
    int i;
    for (i = 0; i < indir_size; ++i) {
        dst_rss_config[i] = tswap32(src_rss_config[i]);
    }
    if (key_size > 0) {
        memcpy(dst_rss_config + indir_size,
               src_rss_config + indir_size,
               key_size);
    }
}

static void host_to_target_ethtool_rxfh(void *dst, const void *src)
{
    struct ethtool_rxfh *target = dst;
    const struct ethtool_rxfh *host = src;

    convert_ethtool_rxfh_header(dst, src);

    const uint32_t indir_size =
        host->cmd == ETHTOOL_SRSSH &&
        host->indir_size == ETH_RXFH_INDIR_NO_CHANGE ?
        0 :
        host->indir_size;
    convert_ethtool_rxfh_rss_config(target->rss_config, host->rss_config,
                                    indir_size, host->key_size);
}

static void target_to_host_ethtool_rxfh(void *dst, const void *src)
{
    struct ethtool_rxfh *host = dst;
    const struct ethtool_rxfh *target = src;

    convert_ethtool_rxfh_header(dst, src);

    const uint32_t indir_size =
        host->cmd == ETHTOOL_SRSSH &&
        host->indir_size == ETH_RXFH_INDIR_NO_CHANGE ?
        0 :
        host->indir_size;
    convert_ethtool_rxfh_rss_config(host->rss_config, target->rss_config,
                                    indir_size, host->key_size);
}

static int target_ethtool_rxfh_size(const void *src)
{
    const struct ethtool_rxfh *target = src;
    if (tswap32(target->cmd) == ETHTOOL_SRSSH &&
        tswap32(target->indir_size) == ETH_RXFH_INDIR_NO_CHANGE) {
        return sizeof(struct ethtool_rxfh) + tswap32(target->key_size);
    }
    return sizeof(struct ethtool_rxfh) +
        tswap32(target->indir_size) * sizeof(target->rss_config[0]) +
        tswap32(target->key_size);
}

static int host_ethtool_rxfh_size(const void *src)
{
    const struct ethtool_rxfh *host = src;
    if (host->cmd == ETHTOOL_SRSSH &&
        host->indir_size == ETH_RXFH_INDIR_NO_CHANGE) {
        return sizeof(struct ethtool_rxfh) + host->key_size;
    }
    return sizeof(struct ethtool_rxfh) +
        host->indir_size * sizeof(host->rss_config[0]) +
        host->key_size;
}

const StructEntry struct_ethtool_rxfh_def = {
    .convert = {
        host_to_target_ethtool_rxfh, target_to_host_ethtool_rxfh },
    .thunk_size = {
        target_ethtool_rxfh_size, host_ethtool_rxfh_size },
    .size = {
        sizeof(struct ethtool_rxfh), sizeof(struct ethtool_rxfh) },
    .align = {
        __alignof__(struct ethtool_rxfh), __alignof__(struct ethtool_rxfh) },
};

/*
 * struct ethtool_link_settings {
 *     __u32 cmd;
 *     __u32 speed;
 *     __u8  duplex;
 *     __u8  port;
 *     __u8  phy_address;
 *     __u8  autoneg;
 *     __u8  mdio_support;
 *     __u8  eth_tp_mdix;
 *     __u8  eth_tp_mdix_ctrl;
 *     __s8  link_mode_masks_nwords;
 *     __u8  transceiver;
 *     __u8  reserved1[3];
 *     __u32 reserved[7];
 *     __u32 link_mode_masks[0];
 * };
 *
 * layout of link_mode_masks fields:
 * __u32 map_supported[link_mode_masks_nwords];
 * __u32 map_advertising[link_mode_masks_nwords];
 * __u32 map_lp_advertising[link_mode_masks_nwords];
 *
 * `link_mode_masks_nwords` can be negative when returning from kernel if the
 * provided request size is not supported.
 */

static void host_to_target_ethtool_link_settings(void *dst, const void *src)
{
    int i;
    struct ethtool_link_settings *target = dst;
    const struct ethtool_link_settings *host = src;

    target->cmd = tswap32(host->cmd);
    target->speed = tswap32(host->speed);
    target->duplex = host->duplex;
    target->port = host->port;
    target->phy_address = host->phy_address;
    target->autoneg = host->autoneg;
    target->mdio_support = host->mdio_support;
    target->eth_tp_mdix = host->eth_tp_mdix;
    target->eth_tp_mdix_ctrl = host->eth_tp_mdix_ctrl;
    target->link_mode_masks_nwords = host->link_mode_masks_nwords;
    target->transceiver = host->transceiver;
    for (i = 0; i < 3; ++i) {
        target->reserved1[i] = host->reserved1[i];
    }
    for (i = 0; i < 7; ++i) {
        target->reserved[i] = tswap32(host->reserved[i]);
    }

    if (host->link_mode_masks_nwords > 0) {
        for (i = 0; i < host->link_mode_masks_nwords * 3; ++i) {
            target->link_mode_masks[i] = tswap32(host->link_mode_masks[i]);
        }
    }
}

static void target_to_host_ethtool_link_settings(void *dst, const void *src)
{
    int i;
    struct ethtool_link_settings *host = dst;
    const struct ethtool_link_settings *target = src;

    host->cmd = tswap32(target->cmd);
    host->speed = tswap32(target->speed);
    host->duplex = target->duplex;
    host->port = target->port;
    host->phy_address = target->phy_address;
    host->autoneg = target->autoneg;
    host->mdio_support = target->mdio_support;
    host->eth_tp_mdix = target->eth_tp_mdix;
    host->eth_tp_mdix_ctrl = target->eth_tp_mdix_ctrl;
    host->link_mode_masks_nwords = target->link_mode_masks_nwords;
    host->transceiver = target->transceiver;
    for (i = 0; i < 3; ++i) {
        host->reserved1[i] = target->reserved1[i];
    }
    for (i = 0; i < 7; ++i) {
        host->reserved[i] = tswap32(target->reserved[i]);
    }

    if (host->link_mode_masks_nwords > 0) {
        for (i = 0; i < host->link_mode_masks_nwords * 3; ++i) {
            host->link_mode_masks[i] = tswap32(target->link_mode_masks[i]);
        }
    }
}

static int target_ethtool_link_settings_size(const void *src)
{
    const struct ethtool_link_settings *target = src;
    if (target->link_mode_masks_nwords > 0) {
        return sizeof(struct ethtool_link_settings) +
            3 * target->link_mode_masks_nwords *
            sizeof(target->link_mode_masks[0]);
    } else {
        return sizeof(struct ethtool_link_settings);
    }
}

static int host_ethtool_link_settings_size(const void *src)
{
    const struct ethtool_link_settings *host = src;
    if (host->link_mode_masks_nwords > 0) {
        return sizeof(struct ethtool_link_settings) +
            3 * host->link_mode_masks_nwords *
            sizeof(host->link_mode_masks[0]);
    } else {
        return sizeof(struct ethtool_link_settings);
    }
}

const StructEntry struct_ethtool_link_settings_def = {
    .convert = {
        host_to_target_ethtool_link_settings,
        target_to_host_ethtool_link_settings
    },
    .thunk_size = {
        target_ethtool_link_settings_size, host_ethtool_link_settings_size },
    .size = {
        sizeof(struct ethtool_link_settings),
        sizeof(struct ethtool_link_settings) },
    .align = {
        __alignof__(struct ethtool_link_settings),
        __alignof__(struct ethtool_link_settings) },
};

/*
 * struct ethtool_per_queue_op {
 *     __u32 cmd;
 *     __u32 sub_command;
 *     __u32 queue_mask[__KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32)];
 *     char  data[];
 * };
 *
 * `queue_mask` are a series of bitmasks of the queues. `data` is a complete
 * command structure for each of the queues addressed.
 *
 * When `cmd` is `ETHTOOL_PERQUEUE` and `sub_command` is `ETHTOOL_GCOALESCE` or
 * `ETHTOOL_SCOALESCE`, the command structure is `struct ethtool_coalesce`.
 */
static void host_to_target_ethtool_per_queue_op(void *dst, const void *src)
{
    static const argtype ethtool_coalesce_argtype[] = {
        MK_STRUCT(STRUCT_ethtool_coalesce), TYPE_NULL };
    int i, queue_count;
    struct ethtool_per_queue_op *target = dst;
    const struct ethtool_per_queue_op *host = src;

    target->cmd = tswap32(host->cmd);
    target->sub_command = tswap32(host->sub_command);

    queue_count = 0;
    for (i = 0; i < __KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32); ++i) {
        target->queue_mask[i] = tswap32(host->queue_mask[i]);
        queue_count += ctpop32(host->queue_mask[i]);
    }

    if (host->cmd != ETHTOOL_PERQUEUE ||
        (host->sub_command != ETHTOOL_GCOALESCE &&
         host->sub_command != ETHTOOL_SCOALESCE)) {
        fprintf(stderr,
                "Unknown command 0x%x sub_command 0x%x for "
                "ethtool_per_queue_op, unable to convert the `data` field "
                "(host-to-target)\n",
                host->cmd, host->sub_command);
        return;
    }

    for (i = 0; i < queue_count; ++i) {
        thunk_convert(target->data + i * sizeof(struct ethtool_coalesce),
                      host->data + i * sizeof(struct ethtool_coalesce),
                      ethtool_coalesce_argtype, THUNK_TARGET);
    }
}

static void target_to_host_ethtool_per_queue_op(void *dst, const void *src)
{
    static const argtype ethtool_coalesce_argtype[] = {
        MK_STRUCT(STRUCT_ethtool_coalesce), TYPE_NULL };
    int i, queue_count;
    struct ethtool_per_queue_op *host = dst;
    const struct ethtool_per_queue_op *target = src;

    host->cmd = tswap32(target->cmd);
    host->sub_command = tswap32(target->sub_command);

    queue_count = 0;
    for (i = 0; i < __KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32); ++i) {
        host->queue_mask[i] = tswap32(target->queue_mask[i]);
        queue_count += ctpop32(host->queue_mask[i]);
    }

    if (host->cmd != ETHTOOL_PERQUEUE ||
        (host->sub_command != ETHTOOL_GCOALESCE &&
         host->sub_command != ETHTOOL_SCOALESCE)) {
        fprintf(stderr,
                "Unknown command 0x%x sub_command 0x%x for "
                "ethtool_per_queue_op, unable to convert the `data` field "
                "(target-to-host)\n",
                host->cmd, host->sub_command);
        return;
    }

    for (i = 0; i < queue_count; ++i) {
        thunk_convert(host->data + i * sizeof(struct ethtool_coalesce),
                      target->data + i * sizeof(struct ethtool_coalesce),
                      ethtool_coalesce_argtype, THUNK_HOST);
    }
}

static int target_ethtool_per_queue_op_size(const void *src)
{
    int i, queue_count;
    const struct ethtool_per_queue_op *target = src;

    if (tswap32(target->cmd) != ETHTOOL_PERQUEUE ||
        (tswap32(target->sub_command) != ETHTOOL_GCOALESCE &&
         tswap32(target->sub_command) != ETHTOOL_SCOALESCE)) {
        fprintf(stderr,
                "Unknown command 0x%x sub_command 0x%x for "
                "ethtool_per_queue_op, unable to compute the size of the "
                "`data` field (target)\n",
                tswap32(target->cmd), tswap32(target->sub_command));
        return sizeof(struct ethtool_per_queue_op);
    }

    queue_count = 0;
    for (i = 0; i < __KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32); ++i) {
        queue_count += ctpop32(target->queue_mask[i]);
    }
    return sizeof(struct ethtool_per_queue_op) +
        queue_count * sizeof(struct ethtool_coalesce);
}

static int host_ethtool_per_queue_op_size(const void *src)
{
    int i, queue_count;
    const struct ethtool_per_queue_op *host = src;

    if (host->cmd != ETHTOOL_PERQUEUE ||
        (host->sub_command != ETHTOOL_GCOALESCE &&
         host->sub_command != ETHTOOL_SCOALESCE)) {
        fprintf(stderr,
                "Unknown command 0x%x sub_command 0x%x for "
                "ethtool_per_queue_op, unable to compute the size of the "
                "`data` field (host)\n",
                host->cmd, host->sub_command);
        return sizeof(struct ethtool_per_queue_op);
    }

    queue_count = 0;
    for (i = 0; i < __KERNEL_DIV_ROUND_UP(MAX_NUM_QUEUE, 32); ++i) {
        queue_count += ctpop32(host->queue_mask[i]);
    }
    return sizeof(struct ethtool_per_queue_op) +
        queue_count * sizeof(struct ethtool_coalesce);
}

const StructEntry struct_ethtool_per_queue_op_def = {
    .convert = {
        host_to_target_ethtool_per_queue_op,
        target_to_host_ethtool_per_queue_op
    },
    .thunk_size = {
        target_ethtool_per_queue_op_size, host_ethtool_per_queue_op_size },
    .size = {
        sizeof(struct ethtool_per_queue_op),
        sizeof(struct ethtool_per_queue_op) },
    .align = {
        __alignof__(struct ethtool_per_queue_op),
        __alignof__(struct ethtool_per_queue_op) },
};

#define safe_dev_ethtool(fd, ...) \
    safe_syscall(__NR_ioctl, (fd), SIOCETHTOOL, __VA_ARGS__)

typedef struct EthtoolEntry EthtoolEntry;

typedef abi_long do_ethtool_fn(const EthtoolEntry *ee, uint8_t *buf_temp,
                               int fd, struct ifreq *host_ifreq);

struct EthtoolEntry {
    uint32_t cmd;
    int access;
    do_ethtool_fn *do_ethtool;
    const argtype arg_type[3];
};

#define ETHT_R 0x0001
#define ETHT_W 0x0002
#define ETHT_RW (ETHT_R | ETHT_W)

static do_ethtool_fn do_ethtool_get_rxfh;

static EthtoolEntry ethtool_entries[] = {
#define ETHTOOL(cmd, access, ...) \
    { cmd, access, 0, { __VA_ARGS__ } },
#define ETHTOOL_SPECIAL(cmd, access, dofn, ...) \
    { cmd, access, dofn, { __VA_ARGS__ } },
#include "ethtool_entries.h"
#undef ETHTOOL
#undef ETHTOOL_SPECIAL
    { 0, 0 },
};

/*
 * ETHTOOL_GRSSH has two modes of operations: querying the sizes of the indir
 * and key as well as actually querying the indir and key. When either
 * `indir_size` or `key_size` is zero, the size of the corresponding entry is
 * retrieved and updated into the `ethtool_rxfh` struct. When either of them is
 * non-zero, the actually indir or key is written to `rss_config`.
 *
 * This causes a problem for the generic framework which converts between host
 * and target structures without the context. When the convertion function sees
 * an `ethtool_rxfh` struct with non-zero `indir_size` or `key_size`, it has to
 * assume that there are entries in `rss_config` and needs to convert them.
 * Unfortunately, when converting the returned `ethtool_rxfh` struct from host
 * to target after an ETHTOOL_GRSSH call with the first mode, the `indir_size`
 * and `key_size` fields are populated but there is no actual data to be
 * converted. More importantly, user programs would not have prepared enough
 * memory for the convertion to take place safely.
 *
 * ETHTOOL_GRSSH thus needs a special implementation which is aware of the two
 * modes of operations and converts the structure accordingly.
 */
abi_long do_ethtool_get_rxfh(const EthtoolEntry *ee, uint8_t *buf_temp,
                             int fd, struct ifreq *host_ifreq)
{
    const argtype *arg_type = ee->arg_type;
    const abi_long ifreq_data = (abi_long)(unsigned long)host_ifreq->ifr_data;
    struct ethtool_rxfh *rxfh = (struct ethtool_rxfh *)buf_temp;
    uint32_t user_indir_size, user_key_size;
    abi_long ret;
    void *argptr;

    assert(arg_type[0] == TYPE_PTR);
    assert(ee->access == IOC_RW);
    arg_type++;

    /*
     * As of Linux kernel v5.8-rc4, ETHTOOL_GRSSH calls never read the
     * `rss_config` part. Converting only the "header" part suffices.
     */
    argptr = lock_user(VERIFY_READ, ifreq_data, sizeof(*rxfh), 1);
    if (!argptr) {
        return -TARGET_EFAULT;
    }
    convert_ethtool_rxfh_header(rxfh, argptr);
    unlock_user(argptr, ifreq_data, sizeof(*rxfh));

    if (rxfh->cmd != ETHTOOL_GRSSH) {
        return -TARGET_EINVAL;
    }
    user_indir_size = rxfh->indir_size;
    user_key_size = rxfh->key_size;

    host_ifreq->ifr_data = (void *)rxfh;
    ret = get_errno(safe_dev_ethtool(fd, host_ifreq));

    /*
     * When a user program supplies `indir_size` or `key_size` but does not
     * match what the kernel has, the syscall returns EINVAL but the structure
     * is already updated. Mimicking it here.
     */
    argptr = lock_user(VERIFY_WRITE, ifreq_data, sizeof(*rxfh), 0);
    if (!argptr) {
        return -TARGET_EFAULT;
    }
    convert_ethtool_rxfh_header(argptr, rxfh);
    unlock_user(argptr, ifreq_data, 0);

    if (is_error(ret)) {
        return ret;
    }

    if (user_indir_size > 0 || user_key_size > 0) {
        const int rss_config_size =
            user_indir_size * sizeof(rxfh->rss_config[0]) + user_key_size;
        argptr = lock_user(VERIFY_WRITE, ifreq_data + sizeof(*rxfh),
                           rss_config_size, 0);
        if (!argptr) {
            return -TARGET_EFAULT;
        }
        convert_ethtool_rxfh_rss_config(argptr, rxfh->rss_config,
                                        user_indir_size, user_key_size);
        unlock_user(argptr, ifreq_data + sizeof(*rxfh), rss_config_size);
    }
    return ret;
}

/*
 * Calculates the size of the data type represented by `type_ptr` with
 * `guest_addr` being the underlying memory. Since `type_ptr` may contain
 * flexible arrays, we need access to the underlying memory to determine their
 * sizes.
 */
static int thunk_size(abi_long guest_addr, const argtype *type_ptr)
{
    /*
     * lock_user based on `thunk_type_size` then call `thunk_type_size_with_src`
     * on it.
     */
    void *src;
    int type_size = thunk_type_size(type_ptr, /*is_host=*/ 0);
    if (!thunk_type_has_flexible_array(type_ptr)) {
        return type_size;
    }

    src = lock_user(VERIFY_READ, guest_addr, type_size, 0);
    type_size = thunk_type_size_with_src(src, type_ptr, /*is_host=*/ 0);
    unlock_user(src, guest_addr, 0);

    return type_size;
}

abi_long dev_ethtool(int fd, uint8_t *buf_temp)
{
    uint32_t *cmd;
    uint32_t host_cmd;
    const EthtoolEntry *ee;
    const argtype *arg_type;
    abi_long ret;
    int target_size;
    void *argptr;

    /*
     * Make a copy of `host_ifreq` because we are going to reuse `buf_temp` and
     * overwrite it. Further, we will overwrite `host_ifreq.ifreq_data`, so
     * keep a copy in `ifreq_data`.
     */
    struct ifreq host_ifreq = *(struct ifreq *)(unsigned long)buf_temp;
    const abi_long ifreq_data = (abi_long)(unsigned long)host_ifreq.ifr_data;

    cmd = (uint32_t *)lock_user(VERIFY_READ, ifreq_data, sizeof(uint32_t), 0);
    host_cmd = tswap32(*cmd);
    unlock_user(cmd, ifreq_data, 0);

    ee = ethtool_entries;
    for (;;) {
        if (ee->cmd == 0) {
            qemu_log_mask(LOG_UNIMP, "Unsupported ethtool cmd=0x%04lx\n",
                          (long)host_cmd);
            return -TARGET_ENOSYS;
        }
        if (ee->cmd == host_cmd) {
            break;
        }
        ee++;
    }
    if (ee->do_ethtool) {
        return ee->do_ethtool(ee, buf_temp, fd, &host_ifreq);
    }

    host_ifreq.ifr_data = buf_temp;
    /* Even for ETHT_R, cmd still needs to be copied. */
    *(uint32_t *)buf_temp = host_cmd;

    arg_type = ee->arg_type;
    switch (arg_type[0]) {
    case TYPE_NULL:
        /* no argument other than cmd */
        ret = get_errno(safe_dev_ethtool(fd, &host_ifreq));
        break;
    case TYPE_PTR:
        arg_type++;
        target_size = thunk_size(ifreq_data, arg_type);
        switch (ee->access) {
        case ETHT_R:
            ret = get_errno(safe_dev_ethtool(fd, &host_ifreq));
            if (!is_error(ret)) {
                argptr = lock_user(VERIFY_WRITE, ifreq_data, target_size, 0);
                if (!argptr) {
                    return -TARGET_EFAULT;
                }
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, ifreq_data, target_size);
            }
            break;
        case ETHT_W:
            argptr = lock_user(VERIFY_READ, ifreq_data, target_size, 1);
            if (!argptr) {
                return -TARGET_EFAULT;
            }
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, ifreq_data, 0);
            ret = get_errno(safe_dev_ethtool(fd, &host_ifreq));
            break;
        default:
        case ETHT_RW:
            argptr = lock_user(VERIFY_READ, ifreq_data, target_size, 1);
            if (!argptr) {
                return -TARGET_EFAULT;
            }
            thunk_convert(buf_temp, argptr, arg_type, THUNK_HOST);
            unlock_user(argptr, ifreq_data, 0);
            ret = get_errno(safe_dev_ethtool(fd, &host_ifreq));
            if (!is_error(ret)) {
                argptr = lock_user(VERIFY_WRITE, ifreq_data, target_size, 0);
                if (!argptr) {
                    return -TARGET_EFAULT;
                }
                thunk_convert(argptr, buf_temp, arg_type, THUNK_TARGET);
                unlock_user(argptr, ifreq_data, target_size);
            }
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "Unsupported ethtool type: cmd=0x%04lx type=%d\n",
                      (long)host_cmd, arg_type[0]);
        ret = -TARGET_ENOSYS;
        break;
    }
    return ret;
}
