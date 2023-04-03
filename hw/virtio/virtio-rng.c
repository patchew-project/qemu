/*
 * A virtio device implementing a hardware random number generator.
 *
 * Copyright 2012 Red Hat, Inc.
 * Copyright 2012 Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include <sys/random.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-rng.h"
#include "sysemu/rng.h"
#include "sysemu/runstate.h"
#include "qom/object_interfaces.h"
#include "migration/misc.h"
#include "trace.h"
#include <stdint.h>

#define VIRTIO_RNG_VM_VERSION  1

static bool is_guest_ready(VirtIORNG *vrng)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vrng);
    if (virtio_queue_ready(vrng->vq)
        && (vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return true;
    }
    trace_virtio_rng_guest_not_ready(vrng);
    return false;
}

static size_t get_request_size(VirtQueue *vq, unsigned quota)
{
    unsigned int in, out;

    virtqueue_get_avail_bytes(vq, &in, &out, quota, 0);
    return in;
}

static void virtio_rng_process(VirtIORNG *vrng);

static VirtQueue *get_active_leak_queue(VirtIORNG *vrng)
{
    size_t queue = vrng->active_leak_queue;
    return vrng->leakq[queue];
}

static size_t swap_active_leak_queue(VirtIORNG *vrng)
{
    size_t old_active = vrng->active_leak_queue;
    vrng->active_leak_queue = (old_active + 1) % 2;
    return old_active;
}

static VirtQueue *get_signaled_leak_queue(VirtIORNG *vrng)
{
    int32_t signaled_leak_queue = vrng->signaled_leak_queue;

    if (signaled_leak_queue == -1) {
        return NULL;
    }

    return vrng->leakq[signaled_leak_queue];
}

static size_t handle_fill_on_leak_command(VirtIORNG *vrng, VirtQueue *vq,
                                          VirtQueueElement *elem)
{
    size_t bytes = iov_size(elem->in_sg, elem->in_num);
    uint8_t *buffer = g_new0(uint8_t, bytes);

    /*
     * Probably, the correct thing to do is add a synchronous
     * API call to RngBackend and use it here.
     */
    if (getrandom(buffer, bytes, 0) != bytes) {
        fprintf(stderr, "qemu-virtio-rng: could not get random bytes");
        return 0;
    }

    iov_from_buf(elem->in_sg, elem->in_num, 0, buffer, bytes);

    return bytes;
}

static size_t handle_copy_on_leak_command(VirtIORNG *vrng, VirtQueue *vq,
                                          VirtQueueElement *elem)
{
    size_t out_size, in_size, offset = 0;

    out_size = iov_size(elem->out_sg, elem->out_num);
    in_size = iov_size(elem->in_sg, elem->in_num);

    if (out_size != in_size) {
        return 0;
    }

    for (int i = 0; i < elem->out_num; ++i) {
        struct iovec *iov = &elem->out_sg[i];
        offset += iov_from_buf(elem->in_sg, elem->in_num, offset, iov->iov_base,
                               iov->iov_len);
    }

    return offset;
}

static void virtio_rng_process_leak(VirtIORNG *vrng, VirtQueue *vq)
{
    VirtQueueElement *elem;
    VirtIODevice *vdev = VIRTIO_DEVICE(vrng);
    size_t len;

    if (!runstate_check(RUN_STATE_RUNNING)) {
        return;
    }

    while ((elem = virtqueue_pop(vq, sizeof(VirtQueueElement)))) {
        /*
         * If we have a write buffer this is a copy-on-leak command
         * otherwise a fill-on-leak command
         */
        if (elem->out_num) {
            len = handle_copy_on_leak_command(vrng, vq, elem);
        } else {
            len = handle_fill_on_leak_command(vrng, vq, elem);
        }

        virtqueue_push(vq, elem, len);
        g_free(elem);
    }
    virtio_notify(vdev, vq);
}

static int signal_entropy_leak(VirtIORNG *vrng)
{
    VirtQueue *activeq = get_active_leak_queue(vrng);

    /*
     * Process all the buffers in the active leak queue
     * and then swap active leak queues.
     */
    virtio_rng_process_leak(vrng, activeq);
    vrng->signaled_leak_queue = swap_active_leak_queue(vrng);

    return 0;
}

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const void *buf, size_t size)
{
    VirtIORNG *vrng = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(vrng);
    VirtQueueElement *elem;
    size_t len;
    int offset;

    if (!is_guest_ready(vrng)) {
        return;
    }

    /* we can't modify the virtqueue until
     * our state is fully synced
     */

    if (!runstate_check(RUN_STATE_RUNNING)) {
        trace_virtio_rng_cpu_is_stopped(vrng, size);
        return;
    }

    vrng->quota_remaining -= size;

    offset = 0;
    while (offset < size) {
        elem = virtqueue_pop(vrng->vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        trace_virtio_rng_popped(vrng);
        len = iov_from_buf(elem->in_sg, elem->in_num,
                           0, buf + offset, size - offset);
        offset += len;

        virtqueue_push(vrng->vq, elem, len);
        trace_virtio_rng_pushed(vrng, len);
        g_free(elem);
    }
    virtio_notify(vdev, vrng->vq);

    if (!virtio_queue_empty(vrng->vq)) {
        /* If we didn't drain the queue, call virtio_rng_process
         * to take care of asking for more data as appropriate.
         */
        virtio_rng_process(vrng);
    }
}

static void virtio_rng_process(VirtIORNG *vrng)
{
    size_t size;
    unsigned quota;

    if (!is_guest_ready(vrng)) {
        return;
    }

    if (vrng->activate_timer) {
        timer_mod(vrng->rate_limit_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + vrng->conf.period_ms);
        vrng->activate_timer = false;
    }

    if (vrng->quota_remaining < 0) {
        quota = 0;
    } else {
        quota = MIN((uint64_t)vrng->quota_remaining, (uint64_t)UINT32_MAX);
    }
    size = get_request_size(vrng->vq, quota);

    trace_virtio_rng_request(vrng, size, quota);

    size = MIN(vrng->quota_remaining, size);
    if (size) {
        rng_backend_request_entropy(vrng->rng, size, chr_read, vrng);
    }
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIORNG *vrng = VIRTIO_RNG(vdev);
    virtio_rng_process(vrng);
}

static void handle_leakq(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIORNG *vrng = VIRTIO_RNG(vdev);
    VirtQueue *signaled_queue = get_signaled_leak_queue(vrng);

    if (!is_guest_ready(vrng)) {
        return;
    }

    /*
     * If we received a request on an already signalled leak queue
     * we need to handle it immediately, otherwise we leave the buffer(s)
     * in the virtqueue and we will handle them once an entropy leak event
     * occurs.
     */
    if (vq == signaled_queue) {
        virtio_rng_process_leak(vrng, vq);
    }
}

static uint64_t get_features(VirtIODevice *vdev, uint64_t f, Error **errp)
{
    return f | (1 << VIRTIO_RNG_F_LEAK);
}

static void virtio_rng_vm_state_change(void *opaque, bool running,
                                       RunState state)
{
    VirtIORNG *vrng = opaque;

    trace_virtio_rng_vm_state_change(vrng, running, state);

    /* We may have an element ready but couldn't process it due to a quota
     * limit or because CPU was stopped.  Make sure to try again when the
     * CPU restart.
     */

    if (running && is_guest_ready(vrng)) {
        virtio_rng_process(vrng);
    }
}

static void check_rate_limit(void *opaque)
{
    VirtIORNG *vrng = opaque;

    vrng->quota_remaining = vrng->conf.max_bytes;
    virtio_rng_process(vrng);
    vrng->activate_timer = true;
}

static void virtio_rng_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIORNG *vrng = VIRTIO_RNG(vdev);

    if (!vdev->vm_running) {
        return;
    }
    vdev->status = status;

    /* Something changed, try to process buffers */
    virtio_rng_process(vrng);
}

static void virtio_rng_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORNG *vrng = VIRTIO_RNG(dev);

    if (vrng->conf.period_ms <= 0) {
        error_setg(errp, "'period' parameter expects a positive integer");
        return;
    }

    /* Workaround: Property parsing does not enforce unsigned integers,
     * So this is a hack to reject such numbers. */
    if (vrng->conf.max_bytes > INT64_MAX) {
        error_setg(errp, "'max-bytes' parameter must be non-negative, "
                   "and less than 2^63");
        return;
    }

    if (vrng->conf.rng == NULL) {
        Object *default_backend = object_new(TYPE_RNG_BUILTIN);

        if (!user_creatable_complete(USER_CREATABLE(default_backend),
                                     errp)) {
            object_unref(default_backend);
            return;
        }

        object_property_add_child(OBJECT(dev), "default-backend",
                                  default_backend);

        /* The child property took a reference, we can safely drop ours now */
        object_unref(default_backend);

        object_property_set_link(OBJECT(dev), "rng", default_backend,
                                 &error_abort);
    }

    vrng->rng = vrng->conf.rng;
    if (vrng->rng == NULL) {
        error_setg(errp, "'rng' parameter expects a valid object");
        return;
    }

    virtio_init(vdev, VIRTIO_ID_RNG, 0);

    vrng->vq = virtio_add_queue(vdev, 8, handle_input);
    vrng->leakq[0] = virtio_add_queue(vdev, 8, handle_leakq);
    vrng->leakq[1] = virtio_add_queue(vdev, 8, handle_leakq);
    vrng->active_leak_queue = 0;
    vrng->signaled_leak_queue = -1;
    vrng->quota_remaining = vrng->conf.max_bytes;
    vrng->rate_limit_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                               check_rate_limit, vrng);
    vrng->activate_timer = true;
    vrng->vmstate = qemu_add_vm_change_state_handler(virtio_rng_vm_state_change,
                                                     vrng);
}

static void virtio_rng_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORNG *vrng = VIRTIO_RNG(dev);

    qemu_del_vm_change_state_handler(vrng->vmstate);
    timer_free(vrng->rate_limit_timer);
    virtio_del_queue(vdev, 0);
    virtio_del_queue(vdev, 1);
    virtio_del_queue(vdev, 2);
    virtio_cleanup(vdev);
}

/*
 * After saving the VM state or loading a VM from a snapshot,
 * we need to signal the guest for a leak event
 */
static int virtio_rng_post_save_device(void *opaque)
{
    VirtIORNG *vrng = opaque;
    return signal_entropy_leak(vrng);
}

static int virtio_rng_post_load_device(void *opaque, int version_id)
{
    VirtIORNG *vrng = opaque;
    return signal_entropy_leak(vrng);
}

static const VMStateDescription vmstate_virtio_rng_device = {
    .name = "virtio-rng-device",
    .version_id = VIRTIO_RNG_VM_VERSION,
    .minimum_version_id = VIRTIO_RNG_VM_VERSION,
    .post_save = virtio_rng_post_save_device,
    .post_load = virtio_rng_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(active_leak_queue, VirtIORNG),
        VMSTATE_INT32(signaled_leak_queue, VirtIORNG),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_rng = {
    .name = "virtio-rng",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_rng_properties[] = {
    /* Set a default rate limit of 2^47 bytes per minute or roughly 2TB/s.  If
     * you have an entropy source capable of generating more entropy than this
     * and you can pass it through via virtio-rng, then hats off to you.  Until
     * then, this is unlimited for all practical purposes.
     */
    DEFINE_PROP_UINT64("max-bytes", VirtIORNG, conf.max_bytes, INT64_MAX),
    DEFINE_PROP_UINT32("period", VirtIORNG, conf.period_ms, 1 << 16),
    DEFINE_PROP_LINK("rng", VirtIORNG, conf.rng, TYPE_RNG_BACKEND, RngBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_rng_properties);
    dc->vmsd = &vmstate_virtio_rng;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_rng_device_realize;
    vdc->unrealize = virtio_rng_device_unrealize;
    vdc->get_features = get_features;
    vdc->set_status = virtio_rng_set_status;
    vdc->vmsd = &vmstate_virtio_rng_device;
}

static const TypeInfo virtio_rng_info = {
    .name = TYPE_VIRTIO_RNG,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIORNG),
    .class_init = virtio_rng_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_rng_info);
}

type_init(virtio_register_types)
