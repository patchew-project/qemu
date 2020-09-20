/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020 Oracle and/or its affiliates.
 *
 * Author: Maciej S. Szmigiero <maciej.szmigiero@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "exec/address-spaces.h"
#include "exec/cpu-common.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "hw/hyperv/dynmem-proto.h"
#include "hw/hyperv/vmbus.h"
#include "hw/mem/haprot.h"
#include "hw/mem/pc-dimm.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qapi-events-misc.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/timer.h"
#include "sysemu/balloon.h"
#include "trace.h"

/*
 * temporarily avoid warnings about enhanced GTree API usage requiring a
 * too recent Glib version until GLIB_VERSION_MAX_ALLOWED finally reaches
 * the Glib version with this API
 */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define TYPE_HV_BALLOON "hv-balloon"
#define HV_BALLOON_GUID "525074DC-8985-46e2-8057-A307DC18A502"
#define HV_BALLOON_PFN_SHIFT 12
#define HV_BALLOON_PAGE_SIZE (1 << HV_BALLOON_PFN_SHIFT)

/*
 * Some Windows versions (at least Server 2019) will crash with various
 * error codes when receiving DM protocol requests (at least
 * DM_MEM_HOT_ADD_REQUEST) immediately after boot.
 *
 * It looks like Hyper-V from Server 2016 uses a 50-second after-boot
 * delay, probably to workaround this issue, so we'll use this value, too.
 */
#define HV_BALLOON_POST_INIT_WAIT (50 * 1000)

#define HV_BALLOON_HA_CHUNK_SIZE (2 * GiB)
#define HV_BALLOON_HA_CHUNK_PAGES (HV_BALLOON_HA_CHUNK_SIZE / HV_BALLOON_PAGE_SIZE)

#define HV_BALLOON_HR_CHUNK_PAGES 585728
/*
 *                                ^ that's the maximum number of pages
 * that Windows returns in one hot remove response
 *
 * If the number requested is too high Windows will no longer honor
 * these requests
 */

typedef enum State {
    S_WAIT_RESET,
    S_CLOSED,
    S_VERSION,
    S_CAPS,
    S_POST_INIT_WAIT,
    S_IDLE,
    S_HOT_ADD_RB_WAIT,
    S_HOT_ADD_POSTING,
    S_HOT_ADD_REPLY_WAIT,
    S_HOT_ADD_SKIP_CURRENT,
    S_HOT_ADD_PROCESSED_CLEAR_PENDING,
    S_HOT_ADD_PROCESSED_NEXT,
    S_HOT_REMOVE,
    S_BALLOON_POSTING,
    S_BALLOON_RB_WAIT,
    S_BALLOON_REPLY_WAIT,
    S_UNBALLOON_POSTING,
    S_UNBALLOON_RB_WAIT,
    S_UNBALLOON_REPLY_WAIT,
} State;

typedef struct PageRange {
    uint64_t start;
    uint64_t count;
} PageRange;

/* type safety */
typedef struct PageRangeTree {
    GTree *t;
} PageRangeTree;

typedef struct HAProtRange {
    HAProtDevice *haprot;

    PageRange range;
    uint64_t used;

    /*
     * Pages not currently usable due to guest alignment reqs or
     * not hot added in the first place
     */
    uint64_t unused_head, unused_tail;

    /* Memory removed from the guest backed by this HAProt */
    PageRangeTree removed_guest, removed_both;
} HAProtRange;

/* type safety */
typedef struct HAProtRangeTree {
    GTree *t;
} HAProtRangeTree;

typedef struct HvBalloon {
    VMBusDevice parent;
    State state;
    bool state_changed;
    bool status_reports;

    union dm_version version;
    union dm_caps caps;

    QEMUTimer post_init_timer;

    unsigned int trans_id;

    /* Guest target size */
    uint64_t target;
    bool target_changed;
    uint64_t target_diff;

    HAProtRangeTree haprots;

    /* Ranges disallowed for hot added memory */
    PageRangeTree haprot_disallowed;

    /* Non-HAProt removed memory */
    PageRangeTree removed_guest, removed_both;

    /* Grand totals of removed memory (both HAProt and non-HAProt) */
    uint64_t removed_guest_ctr, removed_both_ctr;

    GSList *ha_todo;
    uint64_t ha_current_count;
} HvBalloon;

#define HV_BALLOON(obj) OBJECT_CHECK(HvBalloon, (obj), TYPE_HV_BALLOON)

#define HV_BALLOON_SET_STATE(hvb, news)             \
    do {                                            \
        _hv_balloon_state_set(hvb, news, # news);   \
    } while (0)

#define SUM_OVERFLOW_U64(in1, in2) ((in1) > UINT64_MAX - (in2))
#define SUM_SATURATE_U64(in1, in2)              \
    ({                                          \
        uint64_t _in1 = (in1), _in2 = (in2);    \
        uint64_t _result;                       \
                                                \
        if (!SUM_OVERFLOW_U64(_in1, _in2)) {    \
            _result = _in1 + _in2;              \
        } else {                                \
            _result = UINT64_MAX;               \
        }                                       \
                                                \
        _result;                                \
    })

typedef struct HvBalloonReq {
    VMBusChanReq vmreq;
} HvBalloonReq;

/* PageRange */
static void page_range_intersect(const PageRange *range,
                                 uint64_t start, uint64_t count,
                                 PageRange *out)
{
    uint64_t end1 = range->start + range->count;
    uint64_t end2 = start + count;
    uint64_t end = MIN(end1, end2);

    out->start = MAX(range->start, start);
    out->count = out->start < end ? end - out->start : 0;
}

static uint64_t page_range_intersection_size(const PageRange *range,
                                             uint64_t start, uint64_t count)
{
    PageRange trange;

    page_range_intersect(range, start, count, &trange);
    return trange.count;
}

/* return just the part of range before (start) */
static void page_range_part_before(const PageRange *range,
                                   uint64_t start, PageRange *out)
{
    uint64_t endr = range->start + range->count;
    uint64_t end = MIN(endr, start);

    out->start = range->start;
    if (end > out->start) {
        out->count = end - out->start;
    } else {
        out->count = 0;
    }
}

/* return just the part of range after (start, count) */
static void page_range_part_after(const PageRange *range,
                                  uint64_t start, uint64_t count,
                                  PageRange *out)
{
    uint64_t end = range->start + range->count;
    uint64_t ends = start + count;

    out->start = MAX(range->start, ends);
    if (end > out->start) {
        out->count = end - out->start;
    } else {
        out->count = 0;
    }
}

static bool page_range_joinable_left(const PageRange *range,
                                     uint64_t start, uint64_t count)
{
    return start + count == range->start;
}

static bool page_range_joinable_right(const PageRange *range,
                                      uint64_t start, uint64_t count)
{
    return range->start + range->count == start;
}

static bool page_range_joinable(const PageRange *range,
                                uint64_t start, uint64_t count)
{
    return page_range_joinable_left(range, start, count) ||
        page_range_joinable_right(range, start, count);
}

/* PageRangeTree */
static gint page_range_tree_key_compare(gconstpointer leftp,
                                        gconstpointer rightp,
                                        gpointer user_data)
{
    const uint64_t *left = leftp, *right = rightp;

    if (*left < *right) {
        return -1;
    } else if (*left > *right) {
        return 1;
    } else { /* *left == *right */
        return 0;
    }
}

static GTreeNode *page_range_tree_insert_new(PageRangeTree tree,
                                             uint64_t start, uint64_t count)
{
    uint64_t *key = g_malloc(sizeof(*key));
    PageRange *range = g_malloc(sizeof(*range));

    assert(count > 0);

    *key = range->start = start;
    range->count = count;

    return g_tree_insert_node(tree.t, key, range);
}

static void page_range_tree_insert(PageRangeTree tree,
                                   uint64_t start, uint64_t count,
                                   uint64_t *dupcount)
{
    GTreeNode *node;
    bool joinable;
    uint64_t intersection;
    PageRange *range;

    assert(!SUM_OVERFLOW_U64(start, count));
    if (count == 0) {
        return;
    }

    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        node = g_tree_node_previous(node);
    } else {
        node = g_tree_node_last(tree.t);
    }

    if (node) {
        range = g_tree_node_value(node);
        assert(range);
        intersection = page_range_intersection_size(range, start, count);
        joinable = page_range_joinable_right(range, start, count);
    }

    if (!node ||
        (!intersection && !joinable)) {
        /*
         * !node case: the tree is empty or the very first node in the tree
         * already has a higher key (the start of its range).
         * the other case: there is a gap in the tree between the new range
         * and the previous one.
         * anyway, let's just insert the new range into the tree.
         */
        node = page_range_tree_insert_new(tree, start, count);
        assert(node);
        range = g_tree_node_value(node);
        assert(range);
    } else {
        /*
         * the previous range in the tree either partially covers the new
         * range or ends just at its beginning - extend it
         */
        if (dupcount) {
            *dupcount += intersection;
        }

        count += start - range->start;
        range->count = MAX(range->count, count);
    }

    /* check next nodes for possible merging */
    for (node = g_tree_node_next(node); node; ) {
        PageRange *rangecur;

        rangecur = g_tree_node_value(node);
        assert(rangecur);

        intersection = page_range_intersection_size(rangecur,
                                                    range->start, range->count);
        joinable = page_range_joinable_left(rangecur,
                                            range->start, range->count);
        if (!intersection && !joinable) {
            /* the current node is disjoint */
            break;
        }

        if (dupcount) {
            *dupcount += intersection;
        }

        count = rangecur->count + (rangecur->start - range->start);
        range->count = MAX(range->count, count);

        /* the current node was merged in, remove it */
        start = rangecur->start;
        node = g_tree_node_next(node);
        /* no hinted removal in GTree... */
        g_tree_remove(tree.t, &start);
    }
}

static bool page_range_tree_pop(PageRangeTree tree, PageRange *out,
                                uint64_t maxcount)
{
    GTreeNode *node;
    PageRange *range;

    node = g_tree_node_last(tree.t);
    if (!node) {
        return false;
    }

    range = g_tree_node_value(node);
    assert(range);

    out->start = range->start;

    /* can't modify range->start as it is the node key */
    if (range->count > maxcount) {
        out->start += range->count - maxcount;
        out->count = maxcount;
        range->count -= maxcount;
    } else {
        out->count = range->count;
        /* no hinted removal in GTree... */
        g_tree_remove(tree.t, &out->start);
    }

    return true;
}

static bool page_range_tree_intree_any(PageRangeTree tree,
                                       uint64_t start, uint64_t count)
{
    GTreeNode *node;

    if (count == 0) {
        return false;
    }

    /* find the first node that can possibly intersect our range */
    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        /*
         * a NULL node below means that the very first node in the tree
         * already has a higher key (the start of its range).
         */
        node = g_tree_node_previous(node);
    } else {
        /* a NULL node below means that the tree is empty */
        node = g_tree_node_last(tree.t);
    }
    /* node range start <= range start */

    if (!node) {
        /* node range start > range start */
        node = g_tree_node_first(tree.t);
    }

    for ( ; node; node = g_tree_node_next(node)) {
        PageRange *range = g_tree_node_value(node);

        assert(range);
        /*
         * if this node starts beyond or at the end of our range so does
         * every next one
         */
        if (range->start >= start + count) {
            break;
        }

        if (page_range_intersection_size(range, start, count) > 0) {
            return true;
        }
    }

    return false;
}

static gboolean page_range_tree_npages_node(gpointer key,
                                            gpointer value,
                                            gpointer data)
{
    PageRange *range = value;
    uint64_t *npages = data;

    *npages += range->count;

    return false;
}

static void page_range_tree_npages(PageRangeTree tree, uint64_t *npages)
{
    g_tree_foreach(tree.t, page_range_tree_npages_node, npages);
}

static PageRangeTree page_range_tree_new(void)
{
    PageRangeTree tree;

    tree.t = g_tree_new_full(page_range_tree_key_compare, NULL,
                             g_free, g_free);
    return tree;
}

static void page_range_tree_destroy(PageRangeTree *tree)
{
    /* g_tree_destroy() is not NULL-safe */
    if (!tree->t) {
        return;
    }

    g_tree_destroy(tree->t);
    tree->t = NULL;
}

/* HAProtDevice */
static uint64_t haprot_get_size(HAProtDevice *haprot)
{
    return object_property_get_uint(OBJECT(haprot), HAPROT_SIZE_PROP,
                                    &error_abort) / HV_BALLOON_PAGE_SIZE;
}

static void haprot_get_range(HAProtDevice *haprot, PageRange *out)
{
    out->start = object_property_get_uint(OBJECT(haprot), HAPROT_ADDR_PROP,
                                          &error_abort) / HV_BALLOON_PAGE_SIZE;
    assert(out->start > 0);

    out->count = haprot_get_size(haprot);
    assert(out->count > 0);
}

static void haprot_mark_inuse(HAProtDevice *haprot)
{
    const DeviceState *dev = DEVICE(haprot);

    haprot->busy = true;
    qapi_event_send_hv_balloon_haprot_inuse(dev->id ? : "");
}

static void haprot_mark_unused(HAProtDevice *haprot)
{
    const DeviceState *dev = DEVICE(haprot);

    haprot->busy = false;
    qapi_event_send_hv_balloon_haprot_unused(dev->id ? : "");
}

/* HAProtRange */
/* the haprot range reduced by unused head and tail */
static void haprot_range_get_effective_range(HAProtRange *hpr, PageRange *out)
{
    out->start = hpr->range.start + hpr->unused_head;
    out->count = hpr->range.count - hpr->unused_head - hpr->unused_tail;
}

/*
 * reset without triggering unref or notify when reaching zero pages used
 * and without decrementing the grand total counters of removed memory
 */
static void haprot_range_reset_nounref(HAProtRange *hpr)
{
    /* mark the whole range as unused */
    hpr->used = 0;
    hpr->unused_head = hpr->range.count;
    hpr->unused_tail = 0;

    page_range_tree_destroy(&hpr->removed_guest);
    page_range_tree_destroy(&hpr->removed_both);
    hpr->removed_guest = page_range_tree_new();
    hpr->removed_both = page_range_tree_new();
}

static void haprot_range_increment(HAProtRange *hpr, uint64_t diff)
{
    if (diff == 0) {
        return;
    }

    if (hpr->used == 0) {
        haprot_mark_inuse(hpr->haprot);
    }

    hpr->used += diff;
}

static void haprot_range_decrement(HAProtRange *hpr, uint64_t diff)
{
    if (diff == 0) {
        return;
    }

    hpr->used -= diff;

    if (hpr->used == 0) {
        haprot_mark_unused(hpr->haprot);
    }
}

static void haprot_range_reset(HAProtRange *hpr)
{
    haprot_range_decrement(hpr, hpr->used);
    haprot_range_reset_nounref(hpr);
}

/* HAProtRangeTree */
static gint haprot_tree_key_compare(gconstpointer leftp, gconstpointer rightp,
                                    gpointer user_data)
{
    /*
     * haprot tree is also keyed on page range start, so we can simply reuse
     * the comparison function from the page range tree
     */
    return page_range_tree_key_compare(leftp, rightp, user_data);
}

static HAProtRange *haprot_tree_insert_new(HvBalloon *balloon,
                                           HAProtDevice *haprot)
{
    uint64_t *key = g_malloc(sizeof(*key));
    HAProtRange *hpr = g_malloc(sizeof(*hpr));

    haprot->busy = true;
    hpr->haprot = haprot;

    haprot_get_range(haprot, &hpr->range);
    *key = hpr->range.start;

    hpr->removed_guest.t = hpr->removed_both.t = NULL;
    haprot_range_reset_nounref(hpr);

    g_tree_insert(balloon->haprots.t, key, hpr);

    return hpr;
}

static void haprot_tree_remove(HvBalloon *balloon, HAProtDevice *haprot)
{
    uint64_t addr;

    addr = object_property_get_uint(OBJECT(haprot), HAPROT_ADDR_PROP,
                                    &error_abort) /
        HV_BALLOON_PAGE_SIZE;
    assert(addr > 0);

    g_tree_remove(balloon->haprots.t, &addr);
}

static HAProtRange *haprot_tree_lookup_maybe(HvBalloon *balloon,
                                             HAProtDevice *haprot)
{
    uint64_t addr;
    GTreeNode *node;
    HAProtRange *hpr;

    addr = object_property_get_uint(OBJECT(haprot), HAPROT_ADDR_PROP,
                                    &error_abort) /
        HV_BALLOON_PAGE_SIZE;
    assert(addr > 0);

    node = g_tree_lookup_node(balloon->haprots.t, &addr);
    if (!node) {
        return NULL;
    }

    hpr = g_tree_node_value(node);
    assert(hpr);
    return hpr;
}

static HAProtRange *haprot_tree_lookup(HvBalloon *balloon,
                                       HAProtDevice *haprot)
{
    HAProtRange *hpr;

    hpr = haprot_tree_lookup_maybe(balloon, haprot);
    assert(hpr);
    return hpr;
}

/* total RAM includes memory currently removed from the guest */
static gboolean haprot_tree_total_ram_node(gpointer key,
                                           gpointer value,
                                           gpointer data)
{
    HAProtRange *hpr = value;
    uint64_t *size = data;
    PageRange rangeeff;

    haprot_range_get_effective_range(hpr, &rangeeff);
    *size += rangeeff.count;

    return false;
}

static uint64_t haprot_tree_total_ram(HvBalloon *balloon)
{
    uint64_t size = 0;

    g_tree_foreach(balloon->haprots.t, haprot_tree_total_ram_node, &size);
    return size;
}

static void haprot_tree_value_free(gpointer data)
{
    HAProtRange *hpr = data;

    page_range_tree_destroy(&hpr->removed_guest);
    page_range_tree_destroy(&hpr->removed_both);
    g_free(hpr);
}

static gboolean haprot_tree_reset_all_node(gpointer key,
                                           gpointer value,
                                           gpointer data)
{
    HAProtRange *hpr = value;

    haprot_range_reset(hpr);

    return false;
}

static void haprot_tree_reset_all(HvBalloon *balloon)
{
    g_tree_foreach(balloon->haprots.t, haprot_tree_reset_all_node, NULL);
}

static HAProtRangeTree haprot_tree_new(void)
{
    HAProtRangeTree tree;

    tree.t = g_tree_new_full(haprot_tree_key_compare, NULL, g_free,
                             haprot_tree_value_free);
    return tree;
}

static void haprot_tree_destroy(HAProtRangeTree *tree)
{
    /* g_tree_destroy() is not NULL-safe */
    if (!tree->t) {
        return;
    }

    g_tree_destroy(tree->t);
    tree->t = NULL;
}

static gboolean ha_todo_add_all_node(gpointer key,
                                     gpointer value,
                                     gpointer data)
{
    HAProtRange *hpr = value;
    HvBalloon *balloon = data;

    /* assume the hpr has been reset */
    assert(hpr->used == 0);
    assert(hpr->unused_head == hpr->range.count);
    assert(hpr->unused_tail == 0);

    hpr->haprot->busy = true;
    haprot_mark_inuse(hpr->haprot);
    balloon->ha_todo = g_slist_append(balloon->ha_todo, hpr);

    return false;
}

static void ha_todo_add_all(HvBalloon *balloon)
{
    assert(balloon->ha_todo == NULL);
    g_tree_foreach(balloon->haprots.t, ha_todo_add_all_node, balloon);
}

static void ha_todo_clear(HvBalloon *balloon)
{
    while (balloon->ha_todo) {
        HAProtRange *hpr = balloon->ha_todo->data;

        haprot_range_reset_nounref(hpr);
        haprot_mark_unused(hpr->haprot);

        balloon->ha_todo = g_slist_remove(balloon->ha_todo, hpr);
    }
}

/* TODO: unify the code below with virtio-balloon and cache the value */
static int build_dimm_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized DIMMs matter */
            *list = g_slist_prepend(*list, dev);
        }
    }

    object_child_foreach(obj, build_dimm_list, opaque);
    return 0;
}

static ram_addr_t get_current_ram_size(void)
{
    GSList *list = NULL, *item;
    ram_addr_t size = ram_size;

    build_dimm_list(qdev_get_machine(), &list);
    for (item = list; item; item = g_slist_next(item)) {
        Object *obj = OBJECT(item->data);
        if (!strcmp(object_get_typename(obj), TYPE_PC_DIMM))
            size += object_property_get_int(obj, PC_DIMM_SIZE_PROP,
                                            &error_abort);
    }
    g_slist_free(list);

    return size;
}

/* total RAM includes memory currently removed from the guest */
static uint64_t hv_balloon_total_ram(HvBalloon *balloon)
{
    ram_addr_t ram_size = get_current_ram_size();
    uint64_t ram_size_pages = ram_size >> HV_BALLOON_PFN_SHIFT;
    uint64_t haprot_size_pages = haprot_tree_total_ram(balloon);

    assert(ram_size_pages > 0);

    return SUM_SATURATE_U64(ram_size_pages, haprot_size_pages);
}

/*
 * calculating the total RAM size is a slow operation,
 * avoid it as much as possible
 */
static uint64_t hv_balloon_total_removed_rs(HvBalloon *balloon,
                                            uint64_t ram_size_pages)
{
    uint64_t total_removed;

    total_removed = SUM_SATURATE_U64(balloon->removed_guest_ctr,
                                     balloon->removed_both_ctr);

    /* possible if guest returns pages outside actual RAM */
    if (total_removed > ram_size_pages) {
        total_removed = ram_size_pages;
    }

    return total_removed;
}

static bool hv_balloon_state_is_init(HvBalloon *balloon)
{
    return balloon->state == S_WAIT_RESET ||
        balloon->state == S_CLOSED ||
        balloon->state == S_VERSION ||
        balloon->state == S_CAPS;
}

static void _hv_balloon_state_set(HvBalloon *balloon,
                                  State newst, const char *newststr)
{
    if (balloon->state == newst) {
        return;
    }

    balloon->state = newst;
    balloon->state_changed = true;
    trace_hv_balloon_state_change(newststr);
}

static VMBusChannel *hv_balloon_get_channel_maybe(HvBalloon *balloon)
{
    return vmbus_device_channel(&balloon->parent, 0);
}

static VMBusChannel *hv_balloon_get_channel(HvBalloon *balloon)
{
    VMBusChannel *chan;

    chan = hv_balloon_get_channel_maybe(balloon);
    assert(chan != NULL);
    return chan;
}

static ssize_t hv_balloon_send_packet(VMBusChannel *chan,
                                      struct dm_message *msg)
{
    int ret;

    ret = vmbus_channel_reserve(chan, 0, msg->hdr.size);
    if (ret < 0) {
        return ret;
    }

    return vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                              NULL, 0, msg, msg->hdr.size, false,
                              msg->hdr.trans_id);
}

static bool hv_balloon_unballoon_get_source(HvBalloon *balloon,
                                            PageRangeTree *dtree,
                                            uint64_t **dctr,
                                            HAProtRange **hpr)
{
    /* Try the boot memory first */
    if (g_tree_nnodes(balloon->removed_guest.t) > 0) {
        *dtree = balloon->removed_guest;
        *dctr = &balloon->removed_guest_ctr;
        *hpr = NULL;
    } else if (g_tree_nnodes(balloon->removed_both.t) > 0) {
        *dtree = balloon->removed_both;
        *dctr = &balloon->removed_both_ctr;
        *hpr = NULL;
    } else {
        GTreeNode *node;

        for (node = g_tree_node_first(balloon->haprots.t); node;
             node = g_tree_node_next(node)) {
            HAProtRange *hprnode = g_tree_node_value(node);

            assert(hprnode);
            if (g_tree_nnodes(hprnode->removed_guest.t) > 0) {
                *dtree = hprnode->removed_guest;
                *dctr = &balloon->removed_guest_ctr;
                *hpr = hprnode;
                break;
            } else if (g_tree_nnodes(hprnode->removed_both.t) > 0) {
                *dtree = hprnode->removed_both;
                *dctr = &balloon->removed_both_ctr;
                *hpr = hprnode;
                break;
            }
        }

        if (!node) {
            return false;
        }
    }

    return true;
}

static void hv_balloon_balloon_unballoon_start(HvBalloon *balloon,
                                               uint64_t ram_size_pages)
{
    uint64_t total_removed = hv_balloon_total_removed_rs(balloon,
                                                         ram_size_pages);

    assert(balloon->state == S_IDLE);
    assert(ram_size_pages > 0);

    /*
     * we need to cache the value when starting the (un)balloon procedure
     * in case somebody changes the balloon target when the procedure is
     * in progress
     */
    if (balloon->target < ram_size_pages - total_removed) {
        balloon->target_diff = ram_size_pages - total_removed - balloon->target;
        HV_BALLOON_SET_STATE(balloon, S_BALLOON_RB_WAIT);
    } else {
        balloon->target_diff = balloon->target -
            (ram_size_pages - total_removed);

        /*
         * careful here, the user might have set the balloon target
         * above the RAM size, so above the total removed count
         */
        balloon->target_diff = MIN(balloon->target_diff, total_removed);
        HV_BALLOON_SET_STATE(balloon, S_UNBALLOON_RB_WAIT);
    }

    balloon->target_changed = false;
}

static void hv_balloon_unballoon_rb_wait(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_unballoon_request *ur;
    size_t ur_size = sizeof(*ur) + sizeof(ur->range_array[0]);

    assert(balloon->state == S_UNBALLOON_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, ur_size) < 0) {
        return;
    }

    HV_BALLOON_SET_STATE(balloon, S_UNBALLOON_POSTING);
}

static void hv_balloon_unballoon_posting(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    PageRangeTree dtree;
    uint64_t *dctr;
    HAProtRange *hpr;
    struct dm_unballoon_request *ur;
    size_t ur_size = sizeof(*ur) + sizeof(ur->range_array[0]);
    PageRange range;
    bool bret;
    ssize_t ret;

    assert(balloon->state == S_UNBALLOON_POSTING);
    assert(balloon->target_diff > 0);

    if (!hv_balloon_unballoon_get_source(balloon, &dtree, &dctr, &hpr)) {
        error_report("trying to unballoon but nothing ballooned");
        /*
         * there is little we can do as we might have already
         * sent the guest a partial request we can't cancel
         */
        return;
    }

    assert(dtree.t);
    assert(dctr);

    ur = alloca(ur_size);
    memset(ur, 0, ur_size);
    ur->hdr.type = DM_UNBALLOON_REQUEST;
    ur->hdr.size = ur_size;
    ur->hdr.trans_id = balloon->trans_id;

    bret = page_range_tree_pop(dtree, &range, MIN(balloon->target_diff,
                                                  HV_BALLOON_HA_CHUNK_PAGES));
    assert(bret);
    /* TODO: madvise? */

    *dctr -= range.count;
    balloon->target_diff -= range.count;
    if (hpr) {
        haprot_range_increment(hpr, range.count);
    }

    ur->range_count = 1;
    ur->range_array[0].finfo.start_page = range.start;
    ur->range_array[0].finfo.page_cnt = range.count;
    ur->more_pages = balloon->target_diff > 0;

    trace_hv_balloon_outgoing_unballoon(ur->hdr.trans_id,
                                        range.count, range.start,
                                        balloon->target_diff);

    if (ur->more_pages) {
        HV_BALLOON_SET_STATE(balloon, S_UNBALLOON_RB_WAIT);
    } else {
        HV_BALLOON_SET_STATE(balloon, S_UNBALLOON_REPLY_WAIT);
    }

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, ur, ur_size, false,
                             ur->hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting unballoon msg, expect problems",
                     ret);
    }
}

static void hv_balloon_hot_add_start(HvBalloon *balloon)
{
    HAProtRange *hpr;
    PageRange range;

    assert(balloon->state == S_IDLE);
    assert(balloon->ha_todo);

    hpr = balloon->ha_todo->data;

    range.start = QEMU_ALIGN_UP(hpr->range.start,
                                (1 << balloon->caps.cap_bits.hot_add_alignment)
                                * (MiB / HV_BALLOON_PAGE_SIZE));
    hpr->unused_head = range.start - hpr->range.start;
    if (hpr->unused_head >= hpr->range.count) {
        HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_SKIP_CURRENT);
        return;
    }

    range.count = hpr->range.count - hpr->unused_head;
    range.count = QEMU_ALIGN_DOWN(range.count,
                                  (1 << balloon->caps.cap_bits.hot_add_alignment)
                                  * (MiB / HV_BALLOON_PAGE_SIZE));
    if (range.count == 0) {
        HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_SKIP_CURRENT);
        return;
    }
    hpr->unused_tail = hpr->range.count - hpr->unused_head - range.count;
    hpr->used = 0;

    HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_RB_WAIT);
}

static void hv_balloon_hot_add_rb_wait(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_hot_add *ha;
    size_t ha_size = sizeof(*ha) + sizeof(ha->range);

    assert(balloon->state == S_HOT_ADD_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, ha_size) < 0) {
        return;
    }

    HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_POSTING);
}

static void hv_balloon_hot_add_posting(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    HAProtRange *hpr;
    struct dm_hot_add *ha;
    size_t ha_size = sizeof(*ha) + sizeof(ha->range);
    union dm_mem_page_range *ha_region;
    PageRange range;
    uint64_t chunk_max_size;
    ssize_t ret;

    assert(balloon->state == S_HOT_ADD_POSTING);
    assert(balloon->ha_todo);

    hpr = balloon->ha_todo->data;

    range.start = hpr->range.start + hpr->unused_head + hpr->used;
    range.count = hpr->range.count;
    range.count -= hpr->unused_head;
    range.count -= hpr->used;
    range.count -= hpr->unused_tail;

    chunk_max_size = MAX((1 << balloon->caps.cap_bits.hot_add_alignment) *
                         (MiB / HV_BALLOON_PAGE_SIZE),
                         HV_BALLOON_HA_CHUNK_PAGES);
    range.count = MIN(range.count, chunk_max_size);
    balloon->ha_current_count = range.count;

    ha = alloca(ha_size);
    ha_region = &(&ha->range)[1];
    memset(ha, 0, ha_size);
    ha->hdr.type = DM_MEM_HOT_ADD_REQUEST;
    ha->hdr.size = ha_size;
    ha->hdr.trans_id = balloon->trans_id;

    ha->range.finfo.start_page = range.start;
    ha->range.finfo.page_cnt = range.count;
    ha_region->finfo.start_page = range.start;
    ha_region->finfo.page_cnt = ha->range.finfo.page_cnt;

    trace_hv_balloon_outgoing_hot_add(ha->hdr.trans_id,
                                      range.count, range.start);

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, ha, ha_size, false,
                             ha->hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting hot add msg, expect problems",
                     ret);
    }

    HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_REPLY_WAIT);
}

static void hv_balloon_hot_add_finish(HvBalloon *balloon)
{
    HAProtRange *hpr;

    assert(balloon->state == S_HOT_ADD_SKIP_CURRENT ||
           balloon->state == S_HOT_ADD_PROCESSED_CLEAR_PENDING ||
           balloon->state == S_HOT_ADD_PROCESSED_NEXT);
    assert(balloon->ha_todo);

    hpr = balloon->ha_todo->data;

    if (balloon->state == S_HOT_ADD_SKIP_CURRENT) {
        haprot_range_reset_nounref(hpr);
        haprot_mark_unused(hpr->haprot);
    }

    balloon->ha_todo = g_slist_remove(balloon->ha_todo, hpr);
    if (balloon->state == S_HOT_ADD_PROCESSED_CLEAR_PENDING) {
        ha_todo_clear(balloon);
    }

    /* let other things happen, too, between hot adds to be done */
    HV_BALLOON_SET_STATE(balloon, S_IDLE);
}

static void hv_balloon_balloon_rb_wait(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    size_t bl_size = sizeof(struct dm_balloon);

    assert(balloon->state == S_BALLOON_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, bl_size) < 0) {
        return;
    }

    HV_BALLOON_SET_STATE(balloon, S_BALLOON_POSTING);
}

static void hv_balloon_balloon_posting(HvBalloon *balloon)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_balloon bl;
    size_t bl_size = sizeof(bl);
    ssize_t ret;

    assert(balloon->state == S_BALLOON_POSTING);
    assert(balloon->target_diff > 0);

    memset(&bl, 0, sizeof(bl));
    bl.hdr.type = DM_BALLOON_REQUEST;
    bl.hdr.size = bl_size;
    bl.hdr.trans_id = balloon->trans_id;
    bl.num_pages = MIN(balloon->target_diff, HV_BALLOON_HR_CHUNK_PAGES);

    trace_hv_balloon_outgoing_balloon(bl.hdr.trans_id, bl.num_pages,
                                      balloon->target_diff);

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, &bl, bl_size, false,
                             bl.hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting balloon msg, expect problems",
                     ret);
    }

    HV_BALLOON_SET_STATE(balloon, S_BALLOON_REPLY_WAIT);
}

static void hv_balloon_idle_state(HvBalloon *balloon)
{
    bool can_balloon = balloon->caps.cap_bits.balloon;
    bool want_unballoon = false;
    bool want_hot_add = balloon->ha_todo != NULL;
    bool want_balloon = false;
    uint64_t ram_size_pages;

    assert(balloon->state == S_IDLE);

    if (can_balloon && balloon->target_changed) {
        uint64_t total_removed;

        ram_size_pages = hv_balloon_total_ram(balloon);
        total_removed = hv_balloon_total_removed_rs(balloon,
                                                    ram_size_pages);

        want_unballoon = total_removed > 0 &&
            balloon->target > ram_size_pages - total_removed;
        want_balloon = balloon->target < ram_size_pages - total_removed;
    }

    /*
     * the order here is important, first we unballoon, then hot add,
     * then balloon (or hot remove)
     */
    if (want_unballoon) {
        hv_balloon_balloon_unballoon_start(balloon, ram_size_pages);
    } else if (want_hot_add) {
        hv_balloon_hot_add_start(balloon);
    } else if (want_balloon) {
        hv_balloon_balloon_unballoon_start(balloon, ram_size_pages);
    }
}

static const struct {
    void (*handler)(HvBalloon *balloon);
} state_handlers[] = {
    [S_IDLE].handler = hv_balloon_idle_state,
    [S_UNBALLOON_RB_WAIT].handler = hv_balloon_unballoon_rb_wait,
    [S_UNBALLOON_POSTING].handler = hv_balloon_unballoon_posting,
    [S_HOT_ADD_RB_WAIT].handler = hv_balloon_hot_add_rb_wait,
    [S_HOT_ADD_POSTING].handler = hv_balloon_hot_add_posting,
    [S_HOT_ADD_SKIP_CURRENT].handler = hv_balloon_hot_add_finish,
    [S_HOT_ADD_PROCESSED_CLEAR_PENDING].handler = hv_balloon_hot_add_finish,
    [S_HOT_ADD_PROCESSED_NEXT].handler = hv_balloon_hot_add_finish,
    [S_BALLOON_RB_WAIT].handler = hv_balloon_balloon_rb_wait,
    [S_BALLOON_POSTING].handler = hv_balloon_balloon_posting,
};

static void hv_balloon_handle_state(HvBalloon *balloon)
{
    if (!state_handlers[balloon->state].handler) {
        return;
    }

    state_handlers[balloon->state].handler(balloon);
}

static void hv_balloon_remove_response_insert_range(PageRangeTree tree,
                                                    const PageRange *range,
                                                    uint64_t *ctr1,
                                                    uint64_t *ctr2,
                                                    uint64_t *ctr3)
{
    uint64_t dupcount, effcount;

    if (range->count == 0) {
        return;
    }

    dupcount = 0;
    page_range_tree_insert(tree, range->start, range->count, &dupcount);

    assert(dupcount <= range->count);
    effcount = range->count - dupcount;

    *ctr1 += effcount;
    *ctr2 += effcount;
    if (ctr3) {
        *ctr3 += effcount;
    }
}

static void hv_balloon_remove_response_handle_range(HvBalloon *balloon,
                                                    PageRange *range,
                                                    bool both,
                                                    uint64_t *removedctr)
{
    GTreeNode *node;
    PageRangeTree globaltree = both ? balloon->removed_both :
        balloon->removed_guest;
    uint64_t *globalctr = both ? &balloon->removed_both_ctr :
        &balloon->removed_guest_ctr;

    if (range->count == 0) {
        return;
    }

    trace_hv_balloon_remove_response(range->count, range->start, both);

    /* find the first node that can possibly intersect our range */
    node = g_tree_upper_bound(balloon->haprots.t, &range->start);
    if (node) {
        /*
         * a NULL node below means that the very first node in the tree
         * already has a higher key (the start of its range).
         */
        node = g_tree_node_previous(node);
    } else {
        /* a NULL node below means that the tree is empty */
        node = g_tree_node_last(balloon->haprots.t);
    }
    /* node range start <= range start */

    if (!node) {
        /* node range start > range start */
        node = g_tree_node_first(balloon->haprots.t);
    }

    for ( ; node && range->count > 0; node = g_tree_node_next(node)) {
        HAProtRange *hpr = g_tree_node_value(node);
        PageRangeTree hprtree;
        PageRange rangeeff, rangehole, rangecommon;
        uint64_t hprremoved = 0;

        assert(hpr);
        hprtree = both ? hpr->removed_both : hpr->removed_guest;
        haprot_range_get_effective_range(hpr, &rangeeff);

        /*
         * if this node starts beyond or at the end of the range so does
         * every next one
         */
        if (rangeeff.start >= range->start + range->count) {
            break;
        }

        /* process the hole before the current hpr, if it exists */
        page_range_part_before(range, rangeeff.start, &rangehole);
        hv_balloon_remove_response_insert_range(globaltree, &rangehole,
                                                globalctr, removedctr, NULL);
        if (rangehole.count > 0) {
            trace_hv_balloon_remove_response_hole(rangehole.count,
                                                  rangehole.start,
                                                  range->count, range->start,
                                                  rangeeff.start, both);
        }

        /*
         * process the hpr part, can be empty for the very first node processed
         * or due to difference between the nominal and effective hpr start
         */
        page_range_intersect(range, rangeeff.start, rangeeff.count,
                             &rangecommon);
        hv_balloon_remove_response_insert_range(hprtree, &rangecommon,
                                                globalctr, removedctr,
                                                &hprremoved);
        haprot_range_decrement(hpr, hprremoved);
        if (rangecommon.count > 0) {
            trace_hv_balloon_remove_response_common(rangecommon.count,
                                                    rangecommon.start,
                                                    range->count, range->start,
                                                    rangeeff.count,
                                                    rangeeff.start, hprremoved,
                                                    both);
        }

        /* calculate what's left after the current hpr */
        rangecommon = *range;
        page_range_part_after(&rangecommon, rangeeff.start, rangeeff.count,
                              range);
    }

    /* process the remainder of the range that lies outside of the hpr tree */
    if (range->count > 0) {
        hv_balloon_remove_response_insert_range(globaltree, range,
                                                globalctr, removedctr, NULL);
        trace_hv_balloon_remove_response_remainder(range->count, range->start,
                                                   both);
        range->count = 0;
    }
}

static void hv_balloon_remove_response_handle_pages(HvBalloon *balloon,
                                                    PageRange *range,
                                                    uint64_t start,
                                                    uint64_t count,
                                                    bool both,
                                                    uint64_t *removedctr)
{
    assert(count > 0);

    /*
     * if there is an existing range that the new range can't be joined to
     * dump it into tree(s)
     */
    if (range->count > 0 && !page_range_joinable(range, start, count)) {
        hv_balloon_remove_response_handle_range(balloon, range, both,
                                                removedctr);
    }

    if (range->count == 0) {
        range->start = start;
        range->count = count;
    } else if (page_range_joinable_left(range, start, count)) {
        range->start = start;
        range->count += count;
    } else { /* page_range_joinable_right() */
        range->count += count;
    }
}

static gboolean hv_balloon_handle_remove_host_addr_node(gpointer key,
                                                        gpointer value,
                                                        gpointer data)
{
    PageRange *range = value;
    uint64_t pageoff;

    for (pageoff = 0; pageoff < range->count; ) {
        void *addr = (void *)((range->start + pageoff) * HV_BALLOON_PAGE_SIZE);
        RAMBlock *rb;
        ram_addr_t rb_offset;
        size_t rb_page_size;
        size_t discard_size;

        rb = qemu_ram_block_from_host(addr, false, &rb_offset);
        rb_page_size = qemu_ram_pagesize(rb);

        if (rb_page_size != HV_BALLOON_PAGE_SIZE) {
            /* TODO: these should end in "removed_guest" */
            warn_report("guest reported removed page backed by unsupported page size %zu",
                        rb_page_size);
            pageoff++;
            continue;
        }

        discard_size = MIN(range->count - pageoff,
                           (rb->max_length - rb_offset) /
                           HV_BALLOON_PAGE_SIZE);
        discard_size = MAX(discard_size, 1);

        if (ram_block_discard_range(rb, rb_offset, discard_size *
                                    HV_BALLOON_PAGE_SIZE) != 0) {
            warn_report("guest reported removed page failed discard");
        }

        pageoff += discard_size;
    }

    return false;
}

static void hv_balloon_handle_remove_host_addr_tree(PageRangeTree tree)
{
    g_tree_foreach(tree.t, hv_balloon_handle_remove_host_addr_node, NULL);
}

static int hv_balloon_handle_remove_section(PageRangeTree tree,
                                            const MemoryRegionSection *section,
                                            uint64_t count)
{
    void *addr = memory_region_get_ram_ptr(section->mr) +
        section->offset_within_region;
    uint64_t addr_page;

    assert(count > 0);

    if ((uintptr_t)addr % HV_BALLOON_PAGE_SIZE) {
        warn_report("guest reported removed pages at an unaligned host addr %p",
                    addr);
        return -EINVAL;
    }

    addr_page = (uintptr_t)addr / HV_BALLOON_PAGE_SIZE;
    page_range_tree_insert(tree, addr_page, count, NULL);

    return 0;
}

static void hv_balloon_handle_remove_ranges(HvBalloon *balloon,
                                            union dm_mem_page_range ranges[],
                                            uint32_t count)
{
    uint64_t removedcnt;
    PageRangeTree removed_host_addr;
    PageRange range_guest, range_both;

    removed_host_addr = page_range_tree_new();
    range_guest.count = range_both.count = removedcnt = 0;
    for (unsigned int ctr = 0; ctr < count; ctr++) {
        union dm_mem_page_range *mr = &ranges[ctr];
        hwaddr pa;
        MemoryRegionSection section;

        for (unsigned int offset = 0; offset < mr->finfo.page_cnt; ) {
            int ret;
            uint64_t pageno = mr->finfo.start_page + offset;
            uint64_t pagecnt = 1;

            pa = (hwaddr)pageno << HV_BALLOON_PFN_SHIFT;
            section = memory_region_find(get_system_memory(), pa,
                                         (mr->finfo.page_cnt - offset) *
                                         HV_BALLOON_PAGE_SIZE);
            if (!section.mr) {
                warn_report("guest reported removed page %"PRIu64" not found in RAM",
                            pageno);
                ret = -EINVAL;
                goto finish_page;
            }

            pagecnt = section.size / HV_BALLOON_PAGE_SIZE;
            if (pagecnt <= 0) {
                warn_report("guest reported removed page %"PRIu64" in a section smaller than page size",
                            pageno);
                pagecnt = 1; /* skip the whole page */
                ret = -EINVAL;
                goto finish_page;
            }

            if (!memory_region_is_ram(section.mr) ||
                memory_region_is_rom(section.mr) ||
                memory_region_is_romd(section.mr)) {
                warn_report("guest reported removed page %"PRIu64" in a section that is not an ordinary RAM",
                            pageno);
                ret = -EINVAL;
                goto finish_page;
            }

            ret = hv_balloon_handle_remove_section(removed_host_addr, &section,
                                                   pagecnt);

        finish_page:
            if (ret == 0) {
                hv_balloon_remove_response_handle_pages(balloon,
                                                        &range_both,
                                                        pageno, pagecnt,
                                                        true, &removedcnt);
            } else {
                hv_balloon_remove_response_handle_pages(balloon,
                                                        &range_guest,
                                                        pageno, pagecnt,
                                                        false, &removedcnt);
            }

            if (section.mr) {
                memory_region_unref(section.mr);
            }

            offset += pagecnt;
        }
    }

    hv_balloon_remove_response_handle_range(balloon, &range_both, true,
                                            &removedcnt);
    hv_balloon_remove_response_handle_range(balloon, &range_guest, false,
                                            &removedcnt);

    hv_balloon_handle_remove_host_addr_tree(removed_host_addr);
    page_range_tree_destroy(&removed_host_addr);

    if (removedcnt > balloon->target_diff) {
        warn_report("guest reported more pages removed than currently pending (%"PRIu64" vs %"PRIu64")",
                    removedcnt, balloon->target_diff);
        balloon->target_diff = 0;
    } else {
        balloon->target_diff -= removedcnt;
    }
}

static bool hv_balloon_handle_msg_size(HvBalloonReq *req, size_t minsize,
                                       const char *msgname)
{
    VMBusChanReq *vmreq = &req->vmreq;
    uint32_t msglen = vmreq->msglen;

    if (msglen >= minsize) {
        return true;
    }

    warn_report("%s message too short (%u vs %zu), ignoring", msgname,
                (unsigned int)msglen, minsize);
    return false;
}

static void hv_balloon_handle_version_request(HvBalloon *balloon,
                                              HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_version_request *msgVr = vmreq->msg;
    struct dm_version_response respVr;

    if (balloon->state != S_VERSION) {
        warn_report("unexpected DM_VERSION_REQUEST in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgVr),
                                    "DM_VERSION_REQUEST")) {
        return;
    }

    trace_hv_balloon_incoming_version(msgVr->version.major_version,
                                      msgVr->version.minor_version);

    memset(&respVr, 0, sizeof(respVr));
    respVr.hdr.type = DM_VERSION_RESPONSE;
    respVr.hdr.size = sizeof(respVr);
    respVr.hdr.trans_id = msgVr->hdr.trans_id;
    respVr.is_accepted = msgVr->version.version >= DYNMEM_PROTOCOL_VERSION_1 &&
        msgVr->version.version <= DYNMEM_PROTOCOL_VERSION_3;

    hv_balloon_send_packet(vmreq->chan, (struct dm_message *)&respVr);

    if (respVr.is_accepted) {
        HV_BALLOON_SET_STATE(balloon, S_CAPS);
    }
}

static void hv_balloon_handle_caps_report(HvBalloon *balloon,
                                          HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_capabilities *msgCap = vmreq->msg;
    struct dm_capabilities_resp_msg respCap;

    if (balloon->state != S_CAPS) {
        warn_report("unexpected DM_CAPABILITIES_REPORT in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgCap),
                                    "DM_CAPABILITIES_REPORT")) {
        return;
    }

    trace_hv_balloon_incoming_caps(msgCap->caps.caps);
    balloon->caps = msgCap->caps;

    memset(&respCap, 0, sizeof(respCap));
    respCap.hdr.type = DM_CAPABILITIES_RESPONSE;
    respCap.hdr.size = sizeof(respCap);
    respCap.hdr.trans_id = msgCap->hdr.trans_id;
    respCap.is_accepted = 1;
    respCap.hot_remove = 1;
    respCap.suppress_pressure_reports = !balloon->status_reports;
    hv_balloon_send_packet(vmreq->chan, (struct dm_message *)&respCap);

    if (balloon->caps.cap_bits.hot_add) {
        ha_todo_add_all(balloon);
    }

    timer_mod(&balloon->post_init_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              HV_BALLOON_POST_INIT_WAIT);

    HV_BALLOON_SET_STATE(balloon, S_POST_INIT_WAIT);
}

static void hv_balloon_handle_status_report(HvBalloon *balloon,
                                            HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_status *msgStatus = vmreq->msg;

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgStatus),
                                    "DM_STATUS_REPORT")) {
        return;
    }

    if (!balloon->status_reports) {
        return;
    }

    qapi_event_send_hv_balloon_status_report((uint64_t)msgStatus->num_committed *
                                             HV_BALLOON_PAGE_SIZE,
                                             (uint64_t)msgStatus->num_avail *
                                             HV_BALLOON_PAGE_SIZE);
}

static void hv_balloon_handle_unballoon_response(HvBalloon *balloon,
                                                 HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_unballoon_response *msgUrR = vmreq->msg;

    if (balloon->state != S_UNBALLOON_REPLY_WAIT) {
        warn_report("unexpected DM_UNBALLOON_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgUrR),
                                    "DM_UNBALLOON_RESPONSE"))
        return;

    trace_hv_balloon_incoming_unballoon(msgUrR->hdr.trans_id);

    balloon->trans_id++;
    HV_BALLOON_SET_STATE(balloon, S_IDLE);
}

static void hv_balloon_handle_hot_add_response(HvBalloon *balloon,
                                               HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_hot_add_response *msgHaR = vmreq->msg;
    HAProtRange *hpr;

    if (balloon->state != S_HOT_ADD_REPLY_WAIT) {
        warn_report("unexpected DM_HOT_ADD_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgHaR),
                                    "DM_HOT_ADD_RESPONSE"))
        return;

    trace_hv_balloon_incoming_hot_add(msgHaR->hdr.trans_id, msgHaR->result,
                                      msgHaR->page_count);

    balloon->trans_id++;

    assert(balloon->ha_todo);
    hpr = balloon->ha_todo->data;

    if (msgHaR->result) {
        if (msgHaR->page_count > balloon->ha_current_count) {
            warn_report("DM_HOT_ADD_RESPONSE page count higher than requested (%"PRIu32" vs %"PRIu64")",
                        msgHaR->page_count, balloon->ha_current_count);
            msgHaR->page_count = balloon->ha_current_count;
        }

        hpr->used += msgHaR->page_count;
    }

    if (!msgHaR->result || msgHaR->page_count < balloon->ha_current_count) {
        if (hpr->used == 0) {
            /*
             * apparently the guest didn't like the current range at all,
             * let's try the next one
             */
            HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_SKIP_CURRENT);
            return;
        }

        /*
         * the current planned range was only partially hot-added, take note
         * how much of it remains and don't attempt any further hot adds
         */
        hpr->unused_tail = hpr->range.count - hpr->unused_head - hpr->used;

        HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_PROCESSED_CLEAR_PENDING);
        return;
    }

    /* any pages remaining in this hpr? */
    if (hpr->range.count - hpr->unused_head - hpr->used -
        hpr->unused_tail > 0) {
        HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_RB_WAIT);
    } else {
        HV_BALLOON_SET_STATE(balloon, S_HOT_ADD_PROCESSED_NEXT);
    }
}

static void hv_balloon_handle_balloon_response(HvBalloon *balloon,
                                               HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_balloon_response *msgBR = vmreq->msg;

    if (balloon->state != S_BALLOON_REPLY_WAIT) {
        warn_report("unexpected DM_BALLOON_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgBR),
                                    "DM_BALLOON_RESPONSE"))
        return;

    trace_hv_balloon_incoming_balloon(msgBR->hdr.trans_id, msgBR->range_count,
                                      msgBR->more_pages);

    if (vmreq->msglen < sizeof(*msgBR) +
        (uint64_t)sizeof(msgBR->range_array[0]) * msgBR->range_count) {
        warn_report("DM_BALLOON_RESPONSE too short for the range count");
        return;
    }

    if (msgBR->range_count == 0) {
        /* The guest is already at its minimum size */
        msgBR->more_pages = 0;
        balloon->target_diff = 0;
    } else {
        hv_balloon_handle_remove_ranges(balloon,
                                        msgBR->range_array,
                                        msgBR->range_count);
    }

    if (!msgBR->more_pages) {
        balloon->trans_id++;

        if (balloon->target_diff > 0) {
            HV_BALLOON_SET_STATE(balloon, S_BALLOON_RB_WAIT);
        } else {
            HV_BALLOON_SET_STATE(balloon, S_IDLE);
        }
    }
}

static void hv_balloon_handle_packet(HvBalloon *balloon, HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_message *msg = vmreq->msg;

    if (vmreq->msglen < sizeof(msg->hdr)) {
        return;
    }

    switch (msg->hdr.type) {
    case DM_VERSION_REQUEST:
        hv_balloon_handle_version_request(balloon, req);
        break;

    case DM_CAPABILITIES_REPORT:
        hv_balloon_handle_caps_report(balloon, req);
        break;

    case DM_STATUS_REPORT:
        hv_balloon_handle_status_report(balloon, req);
        break;

    case DM_MEM_HOT_ADD_RESPONSE:
        hv_balloon_handle_hot_add_response(balloon, req);
        break;

    case DM_UNBALLOON_RESPONSE:
        hv_balloon_handle_unballoon_response(balloon, req);
        break;

    case DM_BALLOON_RESPONSE:
        hv_balloon_handle_balloon_response(balloon, req);
        break;

    default:
        warn_report("unknown DM message %u", msg->hdr.type);
        break;
    }
}

static bool hv_balloon_recv_channel(HvBalloon *balloon)
{
    VMBusChannel *chan;
    HvBalloonReq *req;

    if (balloon->state == S_WAIT_RESET ||
        balloon->state == S_CLOSED) {
        return false;
    }

    chan = hv_balloon_get_channel(balloon);
    if (vmbus_channel_recv_start(chan)) {
        return false;
    }

    while ((req = vmbus_channel_recv_peek(chan, sizeof(*req)))) {
        hv_balloon_handle_packet(balloon, req);
        vmbus_free_req(req);
        vmbus_channel_recv_pop(chan);
    }

    return vmbus_channel_recv_done(chan) > 0;
}

static void hv_balloon_event_loop(HvBalloon *balloon)
{
    bool any_recv;

    do {
        balloon->state_changed = false;
        hv_balloon_handle_state(balloon);

        any_recv = hv_balloon_recv_channel(balloon);
    } while (balloon->state_changed || any_recv);
}

static uint64_t hv_balloon_haprot_get_align(void *ctx, HAProtDevice *haprot)
{
    HvBalloon *balloon = ctx;

    if (hv_balloon_state_is_init(balloon)) {
        return 0;
    }

    return (1 << balloon->caps.cap_bits.hot_add_alignment) * MiB;
}

static void hv_balloon_haprot_plug_notify(void *ctx, HAProtDevice *haprot,
                                          Error **errp)
{
    HvBalloon *balloon = ctx;
    PageRange range;
    HAProtRange *hpr;

    if (hv_balloon_state_is_init(balloon)) {
        error_setg(errp, "no guest attached to the DM protocol yet");
        return;
    }

    if (!balloon->caps.cap_bits.hot_add) {
        error_setg(errp,
                   "the current DM protocol guest has no support for memory hot add");
        return;
    }

    haprot_get_range(haprot, &range);
    if (page_range_tree_intree_any(balloon->haprot_disallowed,
                                   range.start, range.count)) {
        error_setg(errp,
                   "some of the device pages used to be a part of the guest. this is not supported yet, please reboot the guest and try again");
        return;
    }
    if (page_range_tree_intree_any(balloon->removed_guest,
                                   range.start, range.count) ||
        page_range_tree_intree_any(balloon->removed_both,
                                   range.start, range.count)) {
        error_setg(errp,
                   "some of the device new pages were already returned by the guest. this should not happen, please reboot the guest and try again");
        return;
    }

    trace_hv_balloon_haprot_range_add(range.count, range.start);

    hpr = haprot_tree_insert_new(balloon, haprot);

    balloon->ha_todo = g_slist_append(balloon->ha_todo, hpr);

    hv_balloon_event_loop(balloon);
}

static void hv_balloon_haprot_range_remove_process(HvBalloon *balloon,
                                                   HAProtRange *hpr)
{
    PageRange rangeeff;
    uint64_t dupcount;
    uint64_t removed_guest, removed_both;

    haprot_range_get_effective_range(hpr, &rangeeff);
    if (rangeeff.count == 0) {
        /* not strictly necessary but saves a bit of time */
        return;
    }

    dupcount = 0;
    page_range_tree_insert(balloon->haprot_disallowed,
                           rangeeff.start, rangeeff.count, &dupcount);
    assert(dupcount == 0);

    removed_guest = 0;
    page_range_tree_npages(hpr->removed_guest, &removed_guest);
    removed_both = 0;
    page_range_tree_npages(hpr->removed_both, &removed_both);

    trace_hv_balloon_haprot_range_remove(rangeeff.count, rangeeff.start,
                                         removed_guest, removed_both,
                                         balloon->removed_guest_ctr,
                                         balloon->removed_both_ctr);

    assert(removed_guest + removed_both == rangeeff.count);
    assert(balloon->removed_guest_ctr >= removed_guest);
    assert(balloon->removed_both_ctr >= removed_both);

    balloon->removed_guest_ctr -= removed_guest;
    balloon->removed_both_ctr -= removed_both;
}

static void hv_balloon_haprot_unplug_notify(void *ctx, HAProtDevice *haprot)
{
    HvBalloon *balloon = ctx;
    HAProtRange *hpr;

    hpr = haprot_tree_lookup(balloon, haprot);
    hv_balloon_haprot_range_remove_process(balloon, hpr);
    haprot_tree_remove(balloon, haprot);

    hv_balloon_event_loop(balloon);
}

static void hv_balloon_notify_cb(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    hv_balloon_event_loop(balloon);
}

static void hv_balloon_stat(void *opaque, BalloonInfo *info)
{
    HvBalloon *balloon = opaque;
    info->actual = (hv_balloon_total_ram(balloon) - balloon->removed_both_ctr)
        << HV_BALLOON_PFN_SHIFT;
}

static void hv_balloon_to_target(void *opaque, ram_addr_t target)
{
    HvBalloon *balloon = opaque;
    uint64_t target_pages = target >> HV_BALLOON_PFN_SHIFT;

    if (!target_pages) {
        return;
    }

    /*
     * always set target_changed, even with unchanged target, as the user
     * might be asking us to try again reaching it
     */
    balloon->target = target_pages;
    balloon->target_changed = true;

    hv_balloon_event_loop(balloon);
}

static int hv_balloon_open_channel(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    if (balloon->state != S_CLOSED) {
        warn_report("guest trying to open a DM channel in invalid %d state",
                    balloon->state);
        return -EINVAL;
    }

    HV_BALLOON_SET_STATE(balloon, S_VERSION);
    hv_balloon_event_loop(balloon);

    return 0;
}

static void hv_balloon_close_channel(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    timer_del(&balloon->post_init_timer);

    HV_BALLOON_SET_STATE(balloon, S_WAIT_RESET);
    hv_balloon_event_loop(balloon);
}

static void hv_balloon_post_init_timer(void *opaque)
{
    HvBalloon *balloon = opaque;

    if (balloon->state != S_POST_INIT_WAIT) {
        return;
    }

    HV_BALLOON_SET_STATE(balloon, S_IDLE);
    hv_balloon_event_loop(balloon);
}

static void hv_balloon_dev_realize(VMBusDevice *vdev, Error **errp)
{
    HvBalloon *balloon = HV_BALLOON(vdev);
    int ret;
    Error *local_err = NULL;

    balloon->haprots = haprot_tree_new();
    balloon->state = S_WAIT_RESET;

    ret = qemu_add_balloon_handler(hv_balloon_to_target, hv_balloon_stat,
                                   balloon);
    if (ret < 0) {
        error_setg(errp, "Only one balloon device is supported");
        goto ret_tree;
    }

    haprot_register_protocol(hv_balloon_haprot_get_align,
                             hv_balloon_haprot_plug_notify,
                             hv_balloon_haprot_unplug_notify,
                             balloon, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto ret_handler;
    }

    timer_init_ms(&balloon->post_init_timer, QEMU_CLOCK_VIRTUAL,
                  hv_balloon_post_init_timer, balloon);

    return;

ret_handler:
    qemu_remove_balloon_handler(balloon);

ret_tree:
    haprot_tree_destroy(&balloon->haprots);
}

static void hv_balloon_reset_destroy_common(HvBalloon *balloon)
{
    ha_todo_clear(balloon);

    haprot_tree_reset_all(balloon);
}

static void hv_balloon_dev_reset(VMBusDevice *vdev)
{
    HvBalloon *balloon = HV_BALLOON(vdev);

    page_range_tree_destroy(&balloon->haprot_disallowed);
    page_range_tree_destroy(&balloon->removed_guest);
    page_range_tree_destroy(&balloon->removed_both);
    balloon->haprot_disallowed = page_range_tree_new();
    balloon->removed_guest = page_range_tree_new();
    balloon->removed_both = page_range_tree_new();

    hv_balloon_reset_destroy_common(balloon);

    balloon->trans_id = 0;
    balloon->removed_guest_ctr = 0;
    balloon->removed_both_ctr = 0;

    HV_BALLOON_SET_STATE(balloon, S_CLOSED);
    hv_balloon_event_loop(balloon);
}

static void hv_balloon_dev_unrealize(VMBusDevice *vdev)
{
    HvBalloon *balloon = HV_BALLOON(vdev);

    hv_balloon_reset_destroy_common(balloon);

    haprot_unregister_protocol(hv_balloon_haprot_plug_notify, NULL);
    qemu_remove_balloon_handler(balloon);

    page_range_tree_destroy(&balloon->removed_guest);
    page_range_tree_destroy(&balloon->removed_both);
    page_range_tree_destroy(&balloon->haprot_disallowed);
    haprot_tree_destroy(&balloon->haprots);
}

static Property hv_balloon_properties[] = {
    DEFINE_PROP_BOOL("status-report", HvBalloon,
                     status_reports, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void hv_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_CLASS(klass);

    device_class_set_props(dc, hv_balloon_properties);
    qemu_uuid_parse(HV_BALLOON_GUID, &vdc->classid);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->vmdev_realize = hv_balloon_dev_realize;
    vdc->vmdev_unrealize = hv_balloon_dev_unrealize;
    vdc->vmdev_reset = hv_balloon_dev_reset;
    vdc->open_channel = hv_balloon_open_channel;
    vdc->close_channel = hv_balloon_close_channel;
    vdc->chan_notify_cb = hv_balloon_notify_cb;
}

static const TypeInfo hv_balloon_type_info = {
    .name = TYPE_HV_BALLOON,
    .parent = TYPE_VMBUS_DEVICE,
    .instance_size = sizeof(HvBalloon),
    .class_init = hv_balloon_class_init,
};

static void hv_balloon_register_types(void)
{
    type_register_static(&hv_balloon_type_info);
}

type_init(hv_balloon_register_types)
