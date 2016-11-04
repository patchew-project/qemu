/*
 * QEMU KVM support, paravirtual clock device
 *
 * Copyright (C) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka        <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_i386.h"
#include "hw/sysbus.h"
#include "hw/kvm/clock.h"
#include "migration/migration.h"

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <time.h>

#define TYPE_KVM_CLOCK "kvmclock"
#define KVM_CLOCK(obj) OBJECT_CHECK(KVMClockState, (obj), TYPE_KVM_CLOCK)

typedef struct KVMClockState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint64_t clock;
    uint64_t ns;
    bool clock_valid;

    uint64_t advance_clock;
    struct timespec t_aftervmstop;

    bool adv_clock_enabled;
} KVMClockState;

struct pvclock_vcpu_time_info {
    uint32_t   version;
    uint32_t   pad0;
    uint64_t   tsc_timestamp;
    uint64_t   system_time;
    uint32_t   tsc_to_system_mul;
    int8_t     tsc_shift;
    uint8_t    flags;
    uint8_t    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

static uint64_t kvmclock_current_nsec(KVMClockState *s)
{
    CPUState *cpu = first_cpu;
    CPUX86State *env = cpu->env_ptr;
    hwaddr kvmclock_struct_pa = env->system_time_msr & ~1ULL;
    uint64_t migration_tsc = env->tsc;
    struct pvclock_vcpu_time_info time;
    uint64_t delta;
    uint64_t nsec_lo;
    uint64_t nsec_hi;
    uint64_t nsec;

    if (!(env->system_time_msr & 1ULL)) {
        /* KVM clock not active */
        return 0;
    }

    cpu_physical_memory_read(kvmclock_struct_pa, &time, sizeof(time));

    assert(time.tsc_timestamp <= migration_tsc);
    delta = migration_tsc - time.tsc_timestamp;
    if (time.tsc_shift < 0) {
        delta >>= -time.tsc_shift;
    } else {
        delta <<= time.tsc_shift;
    }

    mulu64(&nsec_lo, &nsec_hi, delta, time.tsc_to_system_mul);
    nsec = (nsec_lo >> 32) | (nsec_hi << 32);
    return nsec + time.system_time;
}

static void kvmclock_vm_state_change(void *opaque, int running,
                                     RunState state)
{
    KVMClockState *s = opaque;
    CPUState *cpu;
    int cap_clock_ctrl = kvm_check_extension(kvm_state, KVM_CAP_KVMCLOCK_CTRL);
    int ret;

    if (running) {
        struct kvm_clock_data data = {};
        uint64_t time_at_migration = kvmclock_current_nsec(s);

        s->clock_valid = false;

        /* We can't rely on the migrated clock value, just discard it */
        if (time_at_migration) {
            s->clock = time_at_migration;
        }

        if (s->advance_clock && s->clock + s->advance_clock > s->clock) {
            s->clock += s->advance_clock;
            s->advance_clock = 0;
        }

        data.clock = s->clock;
        ret = kvm_vm_ioctl(kvm_state, KVM_SET_CLOCK, &data);
        if (ret < 0) {
            fprintf(stderr, "KVM_SET_CLOCK failed: %s\n", strerror(ret));
            abort();
        }

        if (!cap_clock_ctrl) {
            return;
        }
        CPU_FOREACH(cpu) {
            ret = kvm_vcpu_ioctl(cpu, KVM_KVMCLOCK_CTRL, 0);
            if (ret) {
                if (ret != -EINVAL) {
                    fprintf(stderr, "%s: %s\n", __func__, strerror(-ret));
                }
                return;
            }
        }
    } else {
        struct kvm_clock_data data;
        int ret;

        if (s->clock_valid) {
            return;
        }

        kvm_synchronize_all_tsc();

        ret = kvm_vm_ioctl(kvm_state, KVM_GET_CLOCK, &data);
        if (ret < 0) {
            fprintf(stderr, "KVM_GET_CLOCK failed: %s\n", strerror(ret));
            abort();
        }
        s->clock = data.clock;
        /*
         * Transition from VM-running to VM-stopped via migration?
         * Record when the VM was stopped.
         */

        if (state == RUN_STATE_FINISH_MIGRATE &&
            !migration_in_postcopy(migrate_get_current())) {
            clock_gettime(CLOCK_MONOTONIC, &s->t_aftervmstop);
        } else {
            s->t_aftervmstop.tv_sec = 0;
            s->t_aftervmstop.tv_nsec = 0;
        }

        /*
         * If the VM is stopped, declare the clock state valid to
         * avoid re-reading it on next vmsave (which would return
         * a different value). Will be reset when the VM is continued.
         */
        s->clock_valid = true;
    }
}

static void kvmclock_realize(DeviceState *dev, Error **errp)
{
    KVMClockState *s = KVM_CLOCK(dev);

    qemu_add_vm_change_state_handler(kvmclock_vm_state_change, s);
}

static uint64_t clock_delta(struct timespec *before, struct timespec *after)
{
    if (before->tv_sec > after->tv_sec ||
        (before->tv_sec == after->tv_sec &&
         before->tv_nsec > after->tv_nsec)) {
        fprintf(stderr, "clock_delta failed: before=(%ld sec, %ld nsec),"
                        "after=(%ld sec, %ld nsec)\n", before->tv_sec,
                        before->tv_nsec, after->tv_sec, after->tv_nsec);
        abort();
    }

    return (after->tv_sec - before->tv_sec) * 1000000000ULL +
            after->tv_nsec - before->tv_nsec;
}

static void kvmclock_pre_save(void *opaque)
{
    KVMClockState *s = opaque;
    struct timespec now;
    uint64_t ns;

    if (s->t_aftervmstop.tv_sec == 0) {
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);

    ns = clock_delta(&s->t_aftervmstop, &now);

    /*
     * Linux guests can overflow if time jumps
     * forward in large increments.
     * Cap maximum adjustment to 10 minutes.
     */
    ns = MIN(ns, 600000000000ULL);

    if (s->clock + ns > s->clock) {
        s->ns = ns;
    }
}

static int kvmclock_post_load(void *opaque, int version_id)
{
    KVMClockState *s = opaque;

    /* save the value from incoming migration */
    s->advance_clock = s->ns;

    return 0;
}

static bool kvmclock_ns_needed(void *opaque)
{
    KVMClockState *s = opaque;

    return s->adv_clock_enabled;
}

static const VMStateDescription kvmclock_advance_ns = {
    .name = "kvmclock/advance_ns",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = kvmclock_ns_needed,
    .pre_save = kvmclock_pre_save,
    .post_load = kvmclock_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ns, KVMClockState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription kvmclock_vmsd = {
    .name = "kvmclock",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(clock, KVMClockState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &kvmclock_advance_ns,
        NULL
    }
};

static Property kvmclock_properties[] = {
    DEFINE_PROP_BOOL("advance_clock", KVMClockState, adv_clock_enabled, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvmclock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = kvmclock_realize;
    dc->vmsd = &kvmclock_vmsd;
    dc->props = kvmclock_properties;
}

static const TypeInfo kvmclock_info = {
    .name          = TYPE_KVM_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMClockState),
    .class_init    = kvmclock_class_init,
};

/* Note: Must be called after VCPU initialization. */
void kvmclock_create(void)
{
    X86CPU *cpu = X86_CPU(first_cpu);

    if (kvm_enabled() &&
        cpu->env.features[FEAT_KVM] & ((1ULL << KVM_FEATURE_CLOCKSOURCE) |
                                       (1ULL << KVM_FEATURE_CLOCKSOURCE2))) {
        sysbus_create_simple(TYPE_KVM_CLOCK, -1, NULL);
    }
}

static void kvmclock_register_types(void)
{
    type_register_static(&kvmclock_info);
}

type_init(kvmclock_register_types)
