/*
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 * Copyright 2011 Intel Corporation
 * Copyright 2016 Veertu, Inc.
 * Copyright 2017 The Android Open Source Project
 *
 * QEMU Hypervisor.framework support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This file contain code under public domain from the hvdos project:
 * https://github.com/mist64/hvdos
 *
 * Parts Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "sysemu/cpus.h"
#include "sysemu/hvf.h"
#include "sysemu/hvf_int.h"
#include "sysemu/runstate.h"
#include "qemu/guest-random.h"

HVFState *hvf_state;

#ifdef __aarch64__
#define HV_VM_DEFAULT NULL
#endif

static void do_hvf_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty) {
        hvf_get_registers(cpu);
        cpu->vcpu_dirty = true;
    }
}

static void hvf_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_hvf_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

static void do_hvf_cpu_synchronize_set_dirty(CPUState *cpu,
                                             run_on_cpu_data arg)
{
    /* QEMU state is the reference, push it to HVF now and on next entry */
    cpu->vcpu_dirty = true;
}

static void hvf_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void hvf_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void hvf_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_hvf_cpu_synchronize_set_dirty, RUN_ON_CPU_NULL);
}

static void dummy_signal(int sig)
{
}

bool hvf_allowed;

static int hvf_accel_init(MachineState *ms)
{
    hv_return_t ret;
    HVFState *s;

    ret = hv_vm_create(HV_VM_DEFAULT);
    assert_hvf_ok(ret);

    s = g_new0(HVFState, 1);

    hvf_state = s;
    hvf_init_memslots();

    return hvf_arch_init();
}

static void hvf_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "HVF";
    ac->init_machine = hvf_accel_init;
    ac->allowed = &hvf_allowed;
}

static const TypeInfo hvf_accel_type = {
    .name = TYPE_HVF_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = hvf_accel_class_init,
};

static void hvf_type_init(void)
{
    type_register_static(&hvf_accel_type);
}

type_init(hvf_type_init);

static void hvf_vcpu_destroy(CPUState *cpu)
{
    hv_return_t ret = hv_vcpu_destroy(cpu->hvf->fd);
    assert_hvf_ok(ret);

    hvf_arch_vcpu_destroy(cpu);
    g_free(cpu->hvf);
    cpu->hvf = NULL;
}

static int hvf_init_vcpu(CPUState *cpu)
{
    int r;

    cpu->hvf = g_malloc0(sizeof(*cpu->hvf));

    /* init cpu signals */
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = dummy_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &cpu->hvf->unblock_ipi_mask);
    sigdelset(&cpu->hvf->unblock_ipi_mask, SIG_IPI);

#ifdef __aarch64__
    r = hv_vcpu_create(&cpu->hvf->fd, (hv_vcpu_exit_t **)&cpu->hvf->exit, NULL);
#else
    r = hv_vcpu_create((hv_vcpuid_t *)&cpu->hvf->fd, HV_VCPU_DEFAULT);
#endif
    cpu->vcpu_dirty = 1;
    assert_hvf_ok(r);

    return hvf_arch_init_vcpu(cpu);
}

/*
 * The HVF-specific vCPU thread function. This one should only run when the host
 * CPU supports the VMX "unrestricted guest" feature.
 */
static void *hvf_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    int r;

    assert(hvf_enabled());

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;

    hvf_init_vcpu(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = hvf_vcpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    hvf_vcpu_destroy(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

static void hvf_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    /*
     * HVF currently does not support TCG, and only runs in
     * unrestricted-guest mode.
     */
    assert(hvf_enabled());

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/HVF",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, hvf_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static void hvf_accel_ops_class_init(ObjectClass *oc, void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = hvf_start_vcpu_thread;
    ops->kick_vcpu_thread = hvf_kick_vcpu_thread;

    ops->synchronize_post_reset = hvf_cpu_synchronize_post_reset;
    ops->synchronize_post_init = hvf_cpu_synchronize_post_init;
    ops->synchronize_state = hvf_cpu_synchronize_state;
    ops->synchronize_pre_loadvm = hvf_cpu_synchronize_pre_loadvm;
};
static const TypeInfo hvf_accel_ops_type = {
    .name = ACCEL_OPS_NAME("hvf"),

    .parent = TYPE_ACCEL_OPS,
    .class_init = hvf_accel_ops_class_init,
    .abstract = true,
};
static void hvf_accel_ops_register_types(void)
{
    type_register_static(&hvf_accel_ops_type);
}
type_init(hvf_accel_ops_register_types);
