/*
 * QEMU KVM Hyper-V support
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "sysemu/cpus.h"
#include "qemu/bitops.h"
#include "qemu/queue.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "migration/vmstate.h"
#include "hyperv.h"
#include "hyperv-proto.h"

typedef struct SynICState {
    DeviceState parent_obj;

    X86CPU *cpu;

    bool in_kvm_only;

    bool enabled;
    hwaddr msg_page_addr;
    hwaddr evt_page_addr;
    MemoryRegion msg_page_mr;
    MemoryRegion evt_page_mr;
    struct hyperv_message_page *msg_page;
    struct hyperv_event_flags_page *evt_page;
} SynICState;

#define TYPE_SYNIC "hyperv-synic"
#define SYNIC(obj) OBJECT_CHECK(SynICState, (obj), TYPE_SYNIC)

struct HvSintRoute {
    uint32_t sint;
    SynICState *synic;
    int gsi;
    EventNotifier sint_set_notifier;
    EventNotifier sint_ack_notifier;

    HvSintMsgCb msg_cb;
    void *msg_cb_data;
    struct hyperv_message *msg;
    /*
     * the state of the message staged in .msg:
     * 0        - the staging area is not in use (after init or message
     *            successfully delivered to guest)
     * -EBUSY   - the staging area is being used in vcpu thread
     * -EAGAIN  - delivery attempt failed due to slot being busy, retry
     * -EXXXX   - error
     */
    int msg_status;

    unsigned refcount;
};

uint32_t hyperv_vp_index(X86CPU *cpu)
{
    return CPU(cpu)->cpu_index;
}

X86CPU *hyperv_find_vcpu(uint32_t vp_index)
{
    return X86_CPU(qemu_get_cpu(vp_index));
}

static SynICState *get_synic(X86CPU *cpu)
{
    SynICState *synic =
        SYNIC(object_resolve_path_component(OBJECT(cpu), "synic"));
    assert(synic);
    return synic;
}

static void synic_update_msg_page_addr(SynICState *synic)
{
    uint64_t msr = synic->cpu->env.msr_hv_synic_msg_page;
    hwaddr new_addr = (msr & HV_SIMP_ENABLE) ? (msr & TARGET_PAGE_MASK) : 0;

    if (new_addr == synic->msg_page_addr) {
        return;
    }

    if (synic->msg_page_addr) {
        memory_region_del_subregion(get_system_memory(), &synic->msg_page_mr);
    }
    if (new_addr) {
        memory_region_add_subregion(get_system_memory(), new_addr,
                                    &synic->msg_page_mr);
    }
    synic->msg_page_addr = new_addr;
}

static void synic_update_evt_page_addr(SynICState *synic)
{
    uint64_t msr = synic->cpu->env.msr_hv_synic_evt_page;
    hwaddr new_addr = (msr & HV_SIEFP_ENABLE) ? (msr & TARGET_PAGE_MASK) : 0;

    if (new_addr == synic->evt_page_addr) {
        return;
    }

    if (synic->evt_page_addr) {
        memory_region_del_subregion(get_system_memory(), &synic->evt_page_mr);
    }
    if (new_addr) {
        memory_region_add_subregion(get_system_memory(), new_addr,
                                    &synic->evt_page_mr);
    }
    synic->evt_page_addr = new_addr;
}

static void synic_update(SynICState *synic)
{
    if (synic->in_kvm_only) {
        return;
    }

    synic->enabled = synic->cpu->env.msr_hv_synic_control & HV_SYNIC_ENABLE;
    synic_update_msg_page_addr(synic);
    synic_update_evt_page_addr(synic);
}

static void sint_msg_bh(void *opaque)
{
    HvSintRoute *sint_route = opaque;
    int status = sint_route->msg_status;
    sint_route->msg_status = 0;
    sint_route->msg_cb(sint_route->msg_cb_data, status);
    /* drop the reference taken in hyperv_post_msg */
    hyperv_sint_route_unref(sint_route);
}

/*
 * Worker to transfer the message from the staging area into the guest-owned
 * message page in vcpu context, which guarantees serialization with both KVM
 * vcpu and the guest cpu.
 */
static void cpu_post_msg(CPUState *cs, run_on_cpu_data data)
{
    int ret;
    HvSintRoute *sint_route = data.host_ptr;
    SynICState *synic = sint_route->synic;
    struct hyperv_message *dst_msg;

    if (!synic->enabled || !synic->msg_page_addr) {
        ret = -ENXIO;
        goto notify;
    }

    dst_msg = &synic->msg_page->slot[sint_route->sint];

    if (dst_msg->header.message_type != HV_MESSAGE_NONE) {
        dst_msg->header.message_flags |= HV_MESSAGE_FLAG_PENDING;
        ret = -EAGAIN;
    } else {
        memcpy(dst_msg, sint_route->msg, sizeof(*dst_msg));
        ret = kvm_hv_sint_route_set_sint(sint_route);
    }

    memory_region_set_dirty(&synic->msg_page_mr, 0, sizeof(*synic->msg_page));

notify:
    sint_route->msg_status = ret;
    /* notify the msg originator of the progress made; if the slot was busy we
     * set msg_pending flag in it so it will be the guest who will do EOM and
     * trigger the notification from KVM via sint_ack_notifier */
    if (ret != -EAGAIN) {
        aio_bh_schedule_oneshot(qemu_get_aio_context(), sint_msg_bh,
                                sint_route);
    }
}

/*
 * Post a Hyper-V message to the staging area, for delivery to guest in the
 * vcpu thread.
 */
int hyperv_post_msg(HvSintRoute *sint_route, struct hyperv_message *src_msg)
{
    int ret = sint_route->msg_status;

    assert(sint_route->msg_cb);

    if (ret == -EBUSY) {
        return -EAGAIN;
    }
    if (ret) {
        return ret;
    }

    sint_route->msg_status = -EBUSY;
    memcpy(sint_route->msg, src_msg, sizeof(*src_msg));

    /* hold a reference on sint_route until the callback is finished */
    hyperv_sint_route_ref(sint_route);

    async_run_on_cpu(CPU(sint_route->synic->cpu), cpu_post_msg,
                     RUN_ON_CPU_HOST_PTR(sint_route));
    return 0;
}

/*
 * Set given event flag for a given sint on a given vcpu, and signal the sint.
 */
int hyperv_set_evt_flag(HvSintRoute *sint_route, unsigned evtno)
{
    int ret;
    SynICState *synic = sint_route->synic;
    unsigned long *flags, set_mask;
    unsigned set_idx;

    if (evtno > HV_EVENT_FLAGS_COUNT) {
        return -EINVAL;
    }
    if (!synic->enabled || !synic->evt_page_addr) {
        return -ENXIO;
    }

    set_idx = BIT_WORD(evtno);
    set_mask = BIT_MASK(evtno);
    flags = synic->evt_page->slot[sint_route->sint].flags;

    if ((atomic_fetch_or(&flags[set_idx], set_mask) & set_mask) != set_mask) {
        memory_region_set_dirty(&synic->evt_page_mr, 0,
                                sizeof(*synic->evt_page));
        ret = kvm_hv_sint_route_set_sint(sint_route);
    } else {
        ret = 0;
    }
    return ret;
}

static void async_synic_update(CPUState *cs, run_on_cpu_data data)
{
    SynICState *synic = data.host_ptr;
    qemu_mutex_lock_iothread();
    synic_update(synic);
    qemu_mutex_unlock_iothread();
}

typedef struct EvtHandler {
    struct rcu_head rcu;
    QLIST_ENTRY(EvtHandler) le;
    uint32_t conn_id;
    EventNotifier *notifier;
} EvtHandler;

static QLIST_HEAD(, EvtHandler) evt_handlers;
static QemuMutex handlers_mutex;

static void __attribute__((constructor)) hv_init(void)
{
    QLIST_INIT(&evt_handlers);
    qemu_mutex_init(&handlers_mutex);
}

int hyperv_set_evt_notifier(uint32_t conn_id, EventNotifier *notifier)
{
    int ret;
    EvtHandler *eh;

    qemu_mutex_lock(&handlers_mutex);
    QLIST_FOREACH(eh, &evt_handlers, le) {
        if (eh->conn_id == conn_id) {
            if (notifier) {
                ret = -EEXIST;
            } else {
                QLIST_REMOVE_RCU(eh, le);
                g_free_rcu(eh, rcu);
                ret = 0;
            }
            goto unlock;
        }
    }

    if (notifier) {
        eh = g_new(EvtHandler, 1);
        eh->conn_id = conn_id;
        eh->notifier = notifier;
        QLIST_INSERT_HEAD_RCU(&evt_handlers, eh, le);
        ret = 0;
    } else {
        ret = -ENOENT;
    }
unlock:
    qemu_mutex_unlock(&handlers_mutex);
    return ret;
}

static uint64_t sigevent_params(hwaddr addr, uint32_t *conn_id)
{
    uint64_t ret;
    hwaddr len;
    struct hyperv_signal_event_input *msg;

    if (addr & (__alignof__(*msg) - 1)) {
        return HV_STATUS_INVALID_ALIGNMENT;
    }

    len = sizeof(*msg);
    msg = cpu_physical_memory_map(addr, &len, 0);
    if (len < sizeof(*msg)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
    } else {
        *conn_id = (msg->connection_id & HV_CONNECTION_ID_MASK) +
            msg->flag_number;
        ret = 0;
    }
    cpu_physical_memory_unmap(msg, len, 0, 0);
    return ret;
}

static uint64_t hvcall_signal_event(uint64_t param, bool fast)
{
    uint64_t ret;
    uint32_t conn_id;
    EvtHandler *eh;

    if (likely(fast)) {
        conn_id = (param & 0xffffffff) + ((param >> 32) & 0xffff);
    } else {
        ret = sigevent_params(param, &conn_id);
        if (ret) {
            return ret;
        }
    }

    ret = HV_STATUS_INVALID_CONNECTION_ID;
    rcu_read_lock();
    QLIST_FOREACH_RCU(eh, &evt_handlers, le) {
        if (eh->conn_id == conn_id) {
            event_notifier_set(eh->notifier);
            ret = 0;
            break;
        }
    }
    rcu_read_unlock();
    return ret;
}

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    CPUX86State *env = &cpu->env;

    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!cpu->hyperv_synic) {
            return -1;
        }

        switch (exit->u.synic.msr) {
        case HV_X64_MSR_SCONTROL:
            env->msr_hv_synic_control = exit->u.synic.control;
            break;
        case HV_X64_MSR_SIMP:
            env->msr_hv_synic_msg_page = exit->u.synic.msg_page;
            break;
        case HV_X64_MSR_SIEFP:
            env->msr_hv_synic_evt_page = exit->u.synic.evt_page;
            break;
        default:
            return -1;
        }
        /*
         * this will run in this cpu thread before it returns to KVM, but in a
         * safe environment (i.e. when all cpus are quiescent) -- this is
         * necessary because we're changing memory hierarchy
         */
        async_safe_run_on_cpu(CPU(cpu), async_synic_update,
                              RUN_ON_CPU_HOST_PTR(get_synic(cpu)));
        return 0;
    case KVM_EXIT_HYPERV_HCALL: {
        uint16_t code = exit->u.hcall.input & 0xffff;
        bool fast = exit->u.hcall.input & HV_HYPERCALL_FAST;
        uint64_t param = exit->u.hcall.params[0];

        switch (code) {
        case HV_SIGNAL_EVENT:
            exit->u.hcall.result = hvcall_signal_event(param, fast);
            break;
        default:
            exit->u.hcall.result = HV_STATUS_INVALID_HYPERCALL_CODE;
        }
        return 0;
    }
    default:
        return -1;
    }
}

static void sint_ack_handler(EventNotifier *notifier)
{
    HvSintRoute *sint_route = container_of(notifier, HvSintRoute,
                                           sint_ack_notifier);
    event_notifier_test_and_clear(notifier);

    if (sint_route->msg_status == -EAGAIN) {
        aio_bh_schedule_oneshot(qemu_get_aio_context(), sint_msg_bh,
                                sint_route);
    }
}

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data)
{
    SynICState *synic;
    HvSintRoute *sint_route;
    EventNotifier *ack_notifier;
    int r, gsi;
    X86CPU *cpu;

    cpu = hyperv_find_vcpu(vp_index);
    if (!cpu) {
        return NULL;
    }

    synic = get_synic(cpu);
    assert(!synic->in_kvm_only);

    sint_route = g_new0(HvSintRoute, 1);
    r = event_notifier_init(&sint_route->sint_set_notifier, false);
    if (r) {
        goto err;
    }

    ack_notifier = cb ? &sint_route->sint_ack_notifier : NULL;
    if (ack_notifier) {
        sint_route->msg = g_new(struct hyperv_message, 1);

        r = event_notifier_init(ack_notifier, false);
        if (r) {
            goto err_sint_set_notifier;
        }

        event_notifier_set_handler(ack_notifier, sint_ack_handler);
    }

    gsi = kvm_irqchip_add_hv_sint_route(kvm_state, vp_index, sint);
    if (gsi < 0) {
        goto err_gsi;
    }

    r = kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &sint_route->sint_set_notifier,
                                           ack_notifier, gsi);
    if (r) {
        goto err_irqfd;
    }
    sint_route->gsi = gsi;
    sint_route->msg_cb = cb;
    sint_route->msg_cb_data = cb_data;
    sint_route->synic = synic;
    sint_route->sint = sint;
    sint_route->refcount = 1;

    return sint_route;

err_irqfd:
    kvm_irqchip_release_virq(kvm_state, gsi);
err_gsi:
    if (ack_notifier) {
        event_notifier_set_handler(ack_notifier, NULL);
        event_notifier_cleanup(ack_notifier);
        g_free(sint_route->msg);
    }
err_sint_set_notifier:
    event_notifier_cleanup(&sint_route->sint_set_notifier);
err:
    g_free(sint_route);

    return NULL;
}

void hyperv_sint_route_ref(HvSintRoute *sint_route)
{
    sint_route->refcount++;
}

void hyperv_sint_route_unref(HvSintRoute *sint_route)
{
    if (!sint_route) {
        return;
    }

    assert(sint_route->refcount > 0);

    if (--sint_route->refcount) {
        return;
    }

    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state,
                                          &sint_route->sint_set_notifier,
                                          sint_route->gsi);
    kvm_irqchip_release_virq(kvm_state, sint_route->gsi);
    if (sint_route->msg_cb) {
        event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
        event_notifier_cleanup(&sint_route->sint_ack_notifier);
        g_free(sint_route->msg);
    }
    event_notifier_cleanup(&sint_route->sint_set_notifier);
    g_free(sint_route);
}

int kvm_hv_sint_route_set_sint(HvSintRoute *sint_route)
{
    return event_notifier_set(&sint_route->sint_set_notifier);
}

static Property synic_props[] = {
    /* user-invisible, only used for compat handling */
    DEFINE_PROP_BOOL("in-kvm-only", SynICState, in_kvm_only, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void synic_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SynICState *synic = SYNIC(dev);
    char *msgp_name, *evtp_name;
    uint32_t vp_index;

    if (synic->in_kvm_only) {
        return;
    }

    synic->cpu = X86_CPU(obj->parent);

    /* memory region names have to be globally unique */
    vp_index = hyperv_vp_index(synic->cpu);
    msgp_name = g_strdup_printf("synic-%u-msg-page", vp_index);
    evtp_name = g_strdup_printf("synic-%u-evt-page", vp_index);

    memory_region_init_ram(&synic->msg_page_mr, obj, msgp_name,
                           sizeof(*synic->msg_page), &error_abort);
    memory_region_init_ram(&synic->evt_page_mr, obj, evtp_name,
                           sizeof(*synic->evt_page), &error_abort);
    synic->msg_page = memory_region_get_ram_ptr(&synic->msg_page_mr);
    synic->evt_page = memory_region_get_ram_ptr(&synic->evt_page_mr);

    g_free(msgp_name);
    g_free(evtp_name);
}

static void synic_reset(DeviceState *dev)
{
    SynICState *synic = SYNIC(dev);

    if (synic->in_kvm_only) {
        return;
    }

    memset(synic->msg_page, 0, sizeof(*synic->msg_page));
    memset(synic->evt_page, 0, sizeof(*synic->evt_page));
    synic_update(synic);
}

static void synic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = synic_props;
    dc->realize = synic_realize;
    dc->reset = synic_reset;
    dc->user_creatable = false;
}

int hyperv_synic_add(X86CPU *cpu)
{
    Object *obj;
    SynICState *synic;
    uint32_t synic_cap;
    int ret;

    obj = object_new(TYPE_SYNIC);
    object_property_add_child(OBJECT(cpu), "synic", obj, &error_abort);
    object_unref(obj);

    synic = SYNIC(obj);

    if (!synic->in_kvm_only) {
        synic_cap = KVM_CAP_HYPERV_SYNIC2;
        if (!cpu->hyperv_vpindex) {
            error_report("Hyper-V SynIC requires VP_INDEX support");
            return -ENOSYS;
        }
    } else {
        /* compat mode: only in-KVM SynIC timers supported */
        synic_cap = KVM_CAP_HYPERV_SYNIC;
    }

    ret = kvm_vcpu_enable_cap(CPU(cpu), synic_cap, 0);
    if (ret) {
        error_report("failed to enable Hyper-V SynIC in KVM: %s",
                     strerror(-ret));
        return ret;
    }

    object_property_set_bool(obj, true, "realized", &error_abort);
    return 0;
}

void hyperv_synic_reset(X86CPU *cpu)
{
    device_reset(DEVICE(get_synic(cpu)));
}

void hyperv_synic_update(X86CPU *cpu)
{
    synic_update(get_synic(cpu));
}

bool hyperv_synic_usable(void)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);

        if (!cpu->hyperv_synic) {
            return false;
        }

        if (get_synic(cpu)->in_kvm_only) {
            return false;
        }
    }

    return true;
}

static const TypeInfo synic_type_info = {
    .name = TYPE_SYNIC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SynICState),
    .class_init = synic_class_init,
};

static void synic_register_types(void)
{
    type_register_static(&synic_type_info);
}

type_init(synic_register_types)
