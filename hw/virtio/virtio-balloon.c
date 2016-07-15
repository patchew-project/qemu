/*
 * Virtio Balloon Device
 *
 * Copyright IBM, Corp. 2008
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Amit Shah <amit.shah@redhat.com>
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "hw/virtio/virtio.h"
#include "hw/i386/pc.h"
#include "sysemu/balloon.h"
#include "hw/virtio/virtio-balloon.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"
#include "qapi/visitor.h"
#include "qapi-event.h"
#include "trace.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

#define BALLOON_PAGE_SIZE  (1 << VIRTIO_BALLOON_PFN_SHIFT)

static void balloon_page(void *addr, int deflate)
{
    if (!qemu_balloon_is_inhibited() && (!kvm_enabled() ||
                                         kvm_has_sync_mmu())) {
        qemu_madvise(addr, BALLOON_PAGE_SIZE,
                deflate ? QEMU_MADV_WILLNEED : QEMU_MADV_DONTNEED);
    }
}

static const char *balloon_stat_names[] = {
   [VIRTIO_BALLOON_S_SWAP_IN] = "stat-swap-in",
   [VIRTIO_BALLOON_S_SWAP_OUT] = "stat-swap-out",
   [VIRTIO_BALLOON_S_MAJFLT] = "stat-major-faults",
   [VIRTIO_BALLOON_S_MINFLT] = "stat-minor-faults",
   [VIRTIO_BALLOON_S_MEMFREE] = "stat-free-memory",
   [VIRTIO_BALLOON_S_MEMTOT] = "stat-total-memory",
   [VIRTIO_BALLOON_S_AVAIL] = "stat-available-memory",
   [VIRTIO_BALLOON_S_NR] = NULL
};

static void do_balloon_bulk_pages(ram_addr_t base_pfn, uint16_t page_shift,
                                  unsigned long len, bool deflate)
{
    ram_addr_t size, processed, chunk, base;
    MemoryRegionSection section = {.mr = NULL};

    size = len << page_shift;
    base = base_pfn << page_shift;

    for (processed = 0; processed < size; processed += chunk) {
        chunk = size - processed;
        while (chunk >= TARGET_PAGE_SIZE) {
            section = memory_region_find(get_system_memory(),
                                         base + processed, chunk);
            if (!section.mr) {
                chunk = QEMU_ALIGN_DOWN(chunk / 2, TARGET_PAGE_SIZE);
            } else {
                break;
            }
        }

        if (section.mr &&
            (int128_nz(section.size) && memory_region_is_ram(section.mr))) {
            void *addr = section.offset_within_region +
                   memory_region_get_ram_ptr(section.mr);
            qemu_madvise(addr, chunk,
                         deflate ? QEMU_MADV_WILLNEED : QEMU_MADV_DONTNEED);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Invalid guest RAM range [0x%lx, 0x%lx]\n",
                          base + processed, chunk);
            chunk = TARGET_PAGE_SIZE;
        }
    }
}

static void balloon_bulk_pages(struct balloon_bmap_hdr *hdr,
                               unsigned long *bitmap, bool deflate)
{
    ram_addr_t base_pfn = hdr->start_pfn;
    uint16_t page_shift = hdr->page_shift;
    unsigned long len = hdr->bmap_len;
    unsigned long current = 0, end = len * BITS_PER_BYTE;

    if (!qemu_balloon_is_inhibited() && (!kvm_enabled() ||
                                         kvm_has_sync_mmu())) {
        while (current < end) {
            unsigned long one = find_next_bit(bitmap, end, current);

            if (one < end) {
                unsigned long pages, zero;

                zero = find_next_zero_bit(bitmap, end, one + 1);
                if (zero >= end) {
                    pages = end - one;
                } else {
                    pages = zero - one;
                }

                if (pages) {
                    do_balloon_bulk_pages(base_pfn + one, page_shift,
                                          pages, deflate);
                }
                current = one + pages;
            } else {
                current = one;
            }
        }
    }
}

/*
 * reset_stats - Mark all items in the stats array as unset
 *
 * This function needs to be called at device initialization and before
 * updating to a set of newly-generated stats.  This will ensure that no
 * stale values stick around in case the guest reports a subset of the supported
 * statistics.
 */
static inline void reset_stats(VirtIOBalloon *dev)
{
    int i;
    for (i = 0; i < VIRTIO_BALLOON_S_NR; dev->stats[i++] = -1);
}

static bool balloon_stats_supported(const VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_STATS_VQ);
}

static bool balloon_page_bitmap_supported(const VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_PAGE_BITMAP);
}

static bool balloon_misc_vq_supported(const VirtIOBalloon *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    return virtio_vdev_has_feature(vdev, VIRTIO_BALLOON_F_MISC_VQ);
}

static bool balloon_stats_enabled(const VirtIOBalloon *s)
{
    return s->stats_poll_interval > 0;
}

static void balloon_stats_destroy_timer(VirtIOBalloon *s)
{
    if (balloon_stats_enabled(s)) {
        timer_del(s->stats_timer);
        timer_free(s->stats_timer);
        s->stats_timer = NULL;
        s->stats_poll_interval = 0;
    }
}

static void balloon_stats_change_timer(VirtIOBalloon *s, int64_t secs)
{
    timer_mod(s->stats_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + secs * 1000);
}

static void balloon_stats_poll_cb(void *opaque)
{
    VirtIOBalloon *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    if (s->stats_vq_elem == NULL || !balloon_stats_supported(s)) {
        /* re-schedule */
        balloon_stats_change_timer(s, s->stats_poll_interval);
        return;
    }

    virtqueue_push(s->svq, s->stats_vq_elem, s->stats_vq_offset);
    virtio_notify(vdev, s->svq);
    g_free(s->stats_vq_elem);
    s->stats_vq_elem = NULL;
}

static void balloon_stats_get_all(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    Error *err = NULL;
    VirtIOBalloon *s = opaque;
    int i;

    visit_start_struct(v, name, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_int(v, "last-update", &s->stats_last_update, &err);
    if (err) {
        goto out_end;
    }

    visit_start_struct(v, "stats", NULL, 0, &err);
    if (err) {
        goto out_end;
    }
    for (i = 0; i < VIRTIO_BALLOON_S_NR; i++) {
        visit_type_uint64(v, balloon_stat_names[i], &s->stats[i], &err);
        if (err) {
            goto out_nested;
        }
    }
    visit_check_struct(v, &err);
out_nested:
    visit_end_struct(v, NULL);

    if (!err) {
        visit_check_struct(v, &err);
    }
out_end:
    visit_end_struct(v, NULL);
out:
    error_propagate(errp, err);
}

static void balloon_stats_get_poll_interval(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    VirtIOBalloon *s = opaque;
    visit_type_int(v, name, &s->stats_poll_interval, errp);
}

static void balloon_stats_set_poll_interval(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    VirtIOBalloon *s = opaque;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (value < 0) {
        error_setg(errp, "timer value must be greater than zero");
        return;
    }

    if (value > UINT32_MAX) {
        error_setg(errp, "timer value is too big");
        return;
    }

    if (value == s->stats_poll_interval) {
        return;
    }

    if (value == 0) {
        /* timer=0 disables the timer */
        balloon_stats_destroy_timer(s);
        return;
    }

    if (balloon_stats_enabled(s)) {
        /* timer interval change */
        s->stats_poll_interval = value;
        balloon_stats_change_timer(s, value);
        return;
    }

    /* create a new timer */
    g_assert(s->stats_timer == NULL);
    s->stats_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, balloon_stats_poll_cb, s);
    s->stats_poll_interval = value;
    balloon_stats_change_timer(s, 0);
}

static void virtio_balloon_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;
    MemoryRegionSection section;

    for (;;) {
        size_t offset = 0;
        uint32_t pfn;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            return;
        }

        if (balloon_page_bitmap_supported(s)) {
            struct balloon_bmap_hdr hdr;
            uint64_t bmap_len;

            iov_to_buf(elem->out_sg, elem->out_num, offset, &hdr, sizeof(hdr));
            offset += sizeof(hdr);

            bmap_len = hdr.bmap_len;
            if (bmap_len > 0) {
                unsigned long *bitmap = bitmap_new(bmap_len * BITS_PER_BYTE);
                iov_to_buf(elem->out_sg, elem->out_num, offset,
                           bitmap, bmap_len);

                balloon_bulk_pages(&hdr, bitmap, !!(vq == s->dvq));
                g_free(bitmap);
            }
        } else {
            while (iov_to_buf(elem->out_sg, elem->out_num, offset,
                              &pfn, 4) == 4) {
                ram_addr_t pa;
                ram_addr_t addr;
                int p = virtio_ldl_p(vdev, &pfn);

                pa = (ram_addr_t) p << VIRTIO_BALLOON_PFN_SHIFT;
                offset += 4;

                /* FIXME: remove get_system_memory(), but how? */
                section = memory_region_find(get_system_memory(), pa, 1);
                if (!int128_nz(section.size) ||
                    !memory_region_is_ram(section.mr)) {
                    continue;
                }

                trace_virtio_balloon_handle_output(memory_region_name(
                                                            section.mr), pa);
                /* Using memory_region_get_ram_ptr is bending the rules a bit,
                 * but should be OK because we only want a single page.  */
                addr = section.offset_within_region;
                balloon_page(memory_region_get_ram_ptr(section.mr) + addr,
                             !!(vq == s->dvq));
                memory_region_unref(section.mr);
            }
        }

        virtqueue_push(vq, elem, offset);
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static void virtio_balloon_receive_stats(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;
    VirtIOBalloonStat stat;
    size_t offset = 0;
    qemu_timeval tv;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        goto out;
    }

    if (s->stats_vq_elem != NULL) {
        /* This should never happen if the driver follows the spec. */
        virtqueue_push(vq, s->stats_vq_elem, 0);
        virtio_notify(vdev, vq);
        g_free(s->stats_vq_elem);
    }

    s->stats_vq_elem = elem;

    /* Initialize the stats to get rid of any stale values.  This is only
     * needed to handle the case where a guest supports fewer stats than it
     * used to (ie. it has booted into an old kernel).
     */
    reset_stats(s);

    while (iov_to_buf(elem->out_sg, elem->out_num, offset, &stat, sizeof(stat))
           == sizeof(stat)) {
        uint16_t tag = virtio_tswap16(vdev, stat.tag);
        uint64_t val = virtio_tswap64(vdev, stat.val);

        offset += sizeof(stat);
        if (tag < VIRTIO_BALLOON_S_NR)
            s->stats[tag] = val;
    }
    s->stats_vq_offset = offset;

    if (qemu_gettimeofday(&tv) < 0) {
        fprintf(stderr, "warning: %s: failed to get time of day\n", __func__);
        goto out;
    }

    s->stats_last_update = tv.tv_sec;

out:
    if (balloon_stats_enabled(s)) {
        balloon_stats_change_timer(s, s->stats_poll_interval);
    }
}

static void virtio_balloon_handle_resp(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    VirtQueueElement *elem;
    size_t offset = 0;
    struct balloon_bmap_hdr hdr;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        s->req_status = REQ_ERROR;
        return;
    }

    s->misc_vq_elem = elem;
    if (!elem->out_num) {
        return;
    }

    iov_to_buf(elem->out_sg, elem->out_num, offset,
               &hdr, sizeof(hdr));
    offset += sizeof(hdr);

    switch (hdr.cmd) {
    case BALLOON_GET_FREE_PAGES:
        if (hdr.req_id == s->misc_req.param) {
            if (s->bmap_len < hdr.start_pfn / BITS_PER_BYTE + hdr.bmap_len) {
                hdr.bmap_len = s->bmap_len - hdr.start_pfn / BITS_PER_BYTE;
            }

            iov_to_buf(elem->out_sg, elem->out_num, offset,
                       s->free_page_bmap + hdr.start_pfn / BITS_PER_LONG,
                       hdr.bmap_len);
            if (hdr.flag == BALLOON_FLAG_DONE) {
                s->req_id = hdr.req_id;
                s->req_status = REQ_DONE;
            } else {
                s->req_status = REQ_ON_GOING;
            }
        }
        break;
    default:
        break;
    }

}

static void virtio_balloon_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    struct virtio_balloon_config config;

    config.num_pages = cpu_to_le32(dev->num_pages);
    config.actual = cpu_to_le32(dev->actual);

    trace_virtio_balloon_get_config(config.num_pages, config.actual);
    memcpy(config_data, &config, sizeof(struct virtio_balloon_config));
}

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
        if (!strcmp(object_get_typename(obj), TYPE_PC_DIMM)) {
            size += object_property_get_int(obj, PC_DIMM_SIZE_PROP,
                                            &error_abort);
        }
    }
    g_slist_free(list);

    return size;
}

static void virtio_balloon_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    struct virtio_balloon_config config;
    uint32_t oldactual = dev->actual;
    ram_addr_t vm_ram_size = get_current_ram_size();

    memcpy(&config, config_data, sizeof(struct virtio_balloon_config));
    dev->actual = le32_to_cpu(config.actual);
    if (dev->actual != oldactual) {
        qapi_event_send_balloon_change(vm_ram_size -
                        ((ram_addr_t) dev->actual << VIRTIO_BALLOON_PFN_SHIFT),
                        &error_abort);
    }
    trace_virtio_balloon_set_config(dev->actual, oldactual);
}

static uint64_t virtio_balloon_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(vdev);
    f |= dev->host_features;
    virtio_add_feature(&f, VIRTIO_BALLOON_F_STATS_VQ);
    return f;
}

static void virtio_balloon_stat(void *opaque, BalloonInfo *info)
{
    VirtIOBalloon *dev = opaque;
    info->actual = get_current_ram_size() - ((uint64_t) dev->actual <<
                                             VIRTIO_BALLOON_PFN_SHIFT);
}

static BalloonReqStatus virtio_balloon_free_pages(void *opaque,
                                                  unsigned long *bitmap,
                                                  unsigned long bmap_len,
                                                  unsigned long req_id)
{
    VirtIOBalloon *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    VirtQueueElement *elem = s->misc_vq_elem;
    int len;

    if (!balloon_misc_vq_supported(s)) {
        return REQ_UNSUPPORT;
    }

    if (s->req_status == REQ_INIT || s->req_status == REQ_DONE) {
        s->free_page_bmap = bitmap;
        if (elem == NULL || !elem->in_num) {
            elem = virtqueue_pop(s->mvq, sizeof(VirtQueueElement));
            if (!elem) {
                return REQ_ERROR;
            }
            s->misc_vq_elem = elem;
        }
        s->misc_req.cmd = BALLOON_GET_FREE_PAGES;
        s->misc_req.param = req_id;
        s->bmap_len = bmap_len;
        len = iov_from_buf(elem->in_sg, elem->in_num, 0, &s->misc_req,
                           sizeof(s->misc_req));
        virtqueue_push(s->mvq, elem, len);
        virtio_notify(vdev, s->mvq);
        g_free(s->misc_vq_elem);
        s->misc_vq_elem = NULL;
        s->req_status = REQ_ON_GOING;
        return REQ_START;
    }

    return REQ_ON_GOING;
}

static BalloonReqStatus virtio_balloon_free_page_ready(void *opaque,
                                                       unsigned long *req_id)
{
    VirtIOBalloon *s = opaque;

    if (!balloon_misc_vq_supported(s)) {
        return REQ_UNSUPPORT;
    }

    if (s->req_status == REQ_DONE) {
        *req_id = s->req_id;
    }

    return s->req_status;
}

static void virtio_balloon_to_target(void *opaque, ram_addr_t target)
{
    VirtIOBalloon *dev = VIRTIO_BALLOON(opaque);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    ram_addr_t vm_ram_size = get_current_ram_size();

    if (target > vm_ram_size) {
        target = vm_ram_size;
    }
    if (target) {
        dev->num_pages = (vm_ram_size - target) >> VIRTIO_BALLOON_PFN_SHIFT;
        virtio_notify_config(vdev);
    }
    trace_virtio_balloon_to_target(target, dev->num_pages);
}

static void virtio_balloon_save(QEMUFile *f, void *opaque)
{
    virtio_save(VIRTIO_DEVICE(opaque), f);
}

static void virtio_balloon_save_device(VirtIODevice *vdev, QEMUFile *f)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);

    qemu_put_be32(f, s->num_pages);
    qemu_put_be32(f, s->actual);
}

static int virtio_balloon_load(QEMUFile *f, void *opaque, int version_id)
{
    if (version_id != 1)
        return -EINVAL;

    return virtio_load(VIRTIO_DEVICE(opaque), f, version_id);
}

static int virtio_balloon_load_device(VirtIODevice *vdev, QEMUFile *f,
                                      int version_id)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);

    s->num_pages = qemu_get_be32(f);
    s->actual = qemu_get_be32(f);

    if (balloon_stats_enabled(s)) {
        balloon_stats_change_timer(s, s->stats_poll_interval);
    }
    return 0;
}

static void virtio_balloon_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBalloon *s = VIRTIO_BALLOON(dev);
    int ret;

    virtio_init(vdev, "virtio-balloon", VIRTIO_ID_BALLOON,
                sizeof(struct virtio_balloon_config));

    ret = qemu_add_balloon_handler(virtio_balloon_to_target,
                                   virtio_balloon_stat,
                                   virtio_balloon_free_pages,
                                   virtio_balloon_free_page_ready, s);

    if (ret < 0) {
        error_setg(errp, "Only one balloon device is supported");
        virtio_cleanup(vdev);
        return;
    }

    s->ivq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->dvq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->svq = virtio_add_queue(vdev, 128, virtio_balloon_receive_stats);
    s->mvq = virtio_add_queue(vdev, 128, virtio_balloon_handle_resp);

    reset_stats(s);
    s->req_status = REQ_INIT;

    register_savevm(dev, "virtio-balloon", -1, 1,
                    virtio_balloon_save, virtio_balloon_load, s);
}

static void virtio_balloon_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOBalloon *s = VIRTIO_BALLOON(dev);

    balloon_stats_destroy_timer(s);
    qemu_remove_balloon_handler(s);
    unregister_savevm(dev, "virtio-balloon", s);
    virtio_cleanup(vdev);
}

static void virtio_balloon_device_reset(VirtIODevice *vdev)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);

    if (s->stats_vq_elem != NULL) {
        g_free(s->stats_vq_elem);
        s->stats_vq_elem = NULL;
    }

    if (s->misc_vq_elem != NULL) {
        g_free(s->misc_vq_elem);
        s->misc_vq_elem = NULL;
    }
    s->req_status = REQ_INIT;
}

static void virtio_balloon_instance_init(Object *obj)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(obj);

    object_property_add(obj, "guest-stats", "guest statistics",
                        balloon_stats_get_all, NULL, NULL, s, NULL);

    object_property_add(obj, "guest-stats-polling-interval", "int",
                        balloon_stats_get_poll_interval,
                        balloon_stats_set_poll_interval,
                        NULL, s, NULL);
}

static Property virtio_balloon_properties[] = {
    DEFINE_PROP_BIT("deflate-on-oom", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_DEFLATE_ON_OOM, false),
    DEFINE_PROP_BIT("page-bitmap", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_PAGE_BITMAP, true),
    DEFINE_PROP_BIT("misc-vq", VirtIOBalloon, host_features,
                    VIRTIO_BALLOON_F_MISC_VQ, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_balloon_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_balloon_device_realize;
    vdc->unrealize = virtio_balloon_device_unrealize;
    vdc->reset = virtio_balloon_device_reset;
    vdc->get_config = virtio_balloon_get_config;
    vdc->set_config = virtio_balloon_set_config;
    vdc->get_features = virtio_balloon_get_features;
    vdc->save = virtio_balloon_save_device;
    vdc->load = virtio_balloon_load_device;
}

static const TypeInfo virtio_balloon_info = {
    .name = TYPE_VIRTIO_BALLOON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOBalloon),
    .instance_init = virtio_balloon_instance_init,
    .class_init = virtio_balloon_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_balloon_info);
}

type_init(virtio_register_types)
