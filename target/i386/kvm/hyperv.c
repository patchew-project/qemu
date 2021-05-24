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
#include "hyperv.h"
#include "hw/hyperv/hyperv.h"
#include "hyperv-proto.h"
#include "kvm_i386.h"

struct x86_hv_overlay {
    struct hyperv_overlay_page *page;
    uint32_t msr;
    hwaddr gpa;
};

static void async_overlay_update(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    struct x86_hv_overlay *overlay = data.host_ptr;

    qemu_mutex_lock_iothread();
    hyperv_overlay_update(overlay->page, overlay->gpa);
    qemu_mutex_unlock_iothread();

    /**
     * Call KVM so it can keep a copy of the MSR data and do other post-overlay
     * actions such as filling the overlay page contents before returning to
     * guest. This works because MSR filtering is inactive for KVM_SET_MSRS
     */
    kvm_put_one_msr(cpu, overlay->msr, overlay->gpa);

    g_free(overlay);
}

static void do_overlay_update(X86CPU *cpu, struct hyperv_overlay_page *page,
                              uint32_t msr, uint64_t data)
{
    struct x86_hv_overlay *overlay = g_malloc(sizeof(struct x86_hv_overlay));

    *overlay = (struct x86_hv_overlay) {
        .page = page,
        .msr = msr,
        .gpa = data
    };

    /**
     * This will run in this cpu thread before it returns to KVM, but in a
     * safe environment (i.e. when all cpus are quiescent) -- this is
     * necessary because memory hierarchy is being changed
     */
    async_safe_run_on_cpu(CPU(cpu), async_overlay_update,
                          RUN_ON_CPU_HOST_PTR(overlay));
}

static void overlay_update(X86CPU *cpu, uint32_t msr, uint64_t data)
{
    switch (msr) {
    case HV_X64_MSR_GUEST_OS_ID:
        /**
         * When GUEST_OS_ID is cleared, hypercall overlay should be removed;
         * otherwise it is a NOP. We still need to do a SET_MSR here as the
         * kernel need to keep a copy of data.
         */
        if (data != 0) {
            kvm_put_one_msr(cpu, msr, data);
            return;
        }
        /* Fake a zero write to the overlay page hcall to invalidate the mapping */
        do_overlay_update(cpu, &hcall_page, msr, 0);
        break;
    case HV_X64_MSR_HYPERCALL:
        do_overlay_update(cpu, &hcall_page, msr, data);
        break;
    default:
        return;
    }
}

int hyperv_x86_synic_add(X86CPU *cpu)
{
    hyperv_synic_add(CPU(cpu));
    return 0;
}

void hyperv_x86_synic_reset(X86CPU *cpu)
{
    hyperv_synic_reset(CPU(cpu));
}

void hyperv_x86_synic_update(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    hyperv_synic_update(CPU(cpu), env->msr_hv_synic_control & HV_SYNIC_ENABLE,
                        env->msr_hv_synic_msg_page,
                        env->msr_hv_synic_evt_page);
}

static void async_synic_update(CPUState *cs, run_on_cpu_data data)
{
    qemu_mutex_lock_iothread();
    hyperv_x86_synic_update(X86_CPU(cs));
    qemu_mutex_unlock_iothread();
}

int kvm_hv_handle_wrmsr(X86CPU *cpu, uint32_t msr, uint64_t data)
{
    switch (msr) {
    case HV_X64_MSR_GUEST_OS_ID:
    case HV_X64_MSR_HYPERCALL:
        overlay_update(cpu, msr, data);
        break;
    default:
        return -1;
    }

    return 0;
}

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    CPUX86State *env = &cpu->env;

    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!hyperv_feat_enabled(cpu, HYPERV_FEAT_SYNIC)) {
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
         * necessary because memory hierarchy is being changed
         */
        async_safe_run_on_cpu(CPU(cpu), async_synic_update, RUN_ON_CPU_NULL);

        return 0;
    case KVM_EXIT_HYPERV_HCALL: {
        uint16_t code = exit->u.hcall.input & 0xffff;
        bool fast = exit->u.hcall.input & HV_HYPERCALL_FAST;
        uint64_t param = exit->u.hcall.params[0];

        switch (code) {
        case HV_POST_MESSAGE:
            exit->u.hcall.result = hyperv_hcall_post_message(param, fast);
            break;
        case HV_SIGNAL_EVENT:
            exit->u.hcall.result = hyperv_hcall_signal_event(param, fast);
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
