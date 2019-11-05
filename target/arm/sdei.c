/*
 * ARM SDEI emulation for ARM64 virtual machine with KVM
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 *
 * Authors:
 *    Heyi Guo <guoheyi@huawei.com>
 *    Jingyi Wang <wangjingyi11@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "arm-powerctl.h"
#include "qemu/timer.h"
#include "sysemu/kvm.h"
#include "sysemu/kvm_int.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"
#include "sdei.h"
#include "sdei_int.h"
#include "internals.h"
#include "hw/boards.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/intc/arm_gic.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define TYPE_ARM_SDEI "arm_sdei"
#define QEMU_SDEI(obj) OBJECT_CHECK(QemuSDEState, (obj), TYPE_ARM_SDEI)

#define SMCCC_RETURN_REG_COUNT 4
#define PSTATE_M_EL_SHIFT      2

static QemuSDEState *sde_state;

typedef struct QemuSDEIBindNotifyEntry {
    QTAILQ_ENTRY(QemuSDEIBindNotifyEntry) entry;
    QemuSDEIBindNotify *func;
    void *opaque;
    int irq;
} QemuSDEIBindNotifyEntry;

static QTAILQ_HEAD(, QemuSDEIBindNotifyEntry) bind_notifiers =
    QTAILQ_HEAD_INITIALIZER(bind_notifiers);

void qemu_register_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                      void *opaque, int irq)
{
    QemuSDEIBindNotifyEntry *be = g_new0(QemuSDEIBindNotifyEntry, 1);

    be->func = func;
    be->opaque = opaque;
    be->irq = irq;
    QTAILQ_INSERT_TAIL(&bind_notifiers, be, entry);
}

void qemu_unregister_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                        void *opaque, int irq)
{
    QemuSDEIBindNotifyEntry *be;

    QTAILQ_FOREACH(be, &bind_notifiers, entry) {
        if (be->func == func && be->opaque == opaque && be->irq == irq) {
            QTAILQ_REMOVE(&bind_notifiers, be, entry);
            g_free(be);
            return;
        }
    }
}

static void sdei_notify_bind(int irq, int32_t event, bool bind)
{
    QemuSDEIBindNotifyEntry *be, *nbe;

    QTAILQ_FOREACH_SAFE(be, &bind_notifiers, entry, nbe) {
        if (be->irq == irq) {
            be->func(be->opaque, irq, event, bind);
        }
    }
}

static void qemu_sde_prop_init(QemuSDEState *s)
{
    QemuSDEProp *sde_props = s->sde_props_state;
    int i;
    for (i = 0; i < ARRAY_SIZE(s->sde_props_state); i++) {
        sde_props[i].event_id = SDEI_INVALID_EVENT_ID;
        sde_props[i].interrupt = SDEI_INVALID_INTERRUPT;
        sde_props[i].sde_index = i >= PRIVATE_SLOT_COUNT ?
                                 i - PRIVATE_SLOT_COUNT : i;

        qemu_mutex_init(&(sde_props[i].lock));
        sde_props[i].refcount = 0;
    }
    sde_props[0].event_id = SDEI_STD_EVT_SOFTWARE_SIGNAL;
    sde_props[0].interrupt = SDEI_INVALID_INTERRUPT;
    sde_props[0].is_shared = false;
    sde_props[0].is_critical = false;

    for (i = 0; i < ARRAY_SIZE(s->irq_map); i++) {
        s->irq_map[i] = SDEI_INVALID_EVENT_ID;
    }

    qemu_mutex_init(&s->sdei_interrupt_bind_lock);
}

static void qemu_sde_cpu_init(QemuSDEState *s)
{
    int i;
    QemuSDECpu *sde_cpus;

    s->sdei_max_cpus = current_machine->smp.max_cpus;
    s->sde_cpus = g_new0(QemuSDECpu, s->sdei_max_cpus);
    sde_cpus = s->sde_cpus;
    for (i = 0; i < s->sdei_max_cpus; i++) {
        sde_cpus[i].masked = true;
        sde_cpus[i].critical_running_event = SDEI_INVALID_EVENT_ID;
        sde_cpus[i].normal_running_event = SDEI_INVALID_EVENT_ID;
    }
}

static int gic_int_to_irq(int num_irq, int intid, int cpu)
{
    if (intid >= GIC_INTERNAL) {
        return intid - GIC_INTERNAL;
    }
    return num_irq - GIC_INTERNAL + cpu * GIC_INTERNAL + intid;
}

static int irq_to_gic_int(int num_irq, int irq, int *cpu)
{
    if (irq < num_irq - GIC_INTERNAL) {
        return irq + GIC_INTERNAL;
    }
    irq -= num_irq - GIC_INTERNAL;
    *cpu = irq / GIC_INTERNAL;
    return irq % GIC_INTERNAL;
}

static inline QemuSDECpu *get_sde_cpu(QemuSDEState *s, CPUState *cs)
{
    if (cs->cpu_index >= s->sdei_max_cpus) {
        error_report("BUG: cpu index %d >= max_cpus %d",
                     cs->cpu_index, s->sdei_max_cpus);
        return NULL;
    }
    return &s->sde_cpus[cs->cpu_index];
}

static bool is_valid_event_number(int32_t event)
{
    int32_t slot_id;

    if (event < 0 || (event & 0x3F000000)) {
        return false;
    }

    slot_id = SDEI_EVENT_TO_SLOT(event);
    if (slot_id >= PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT) {
        return false;
    }

    return true;
}

static bool is_valid_event(QemuSDEState *s, int32_t event)
{
    if (!is_valid_event_number(event)) {
        return false;
    }

    if (s->sde_props_state[SDEI_EVENT_TO_SLOT(event)].event_id != event) {
        return false;
    }

    return true;
}

static QemuSDEProp *get_sde_prop_no_lock(QemuSDEState *s, int32_t event)
{
    if (!is_valid_event(s, event)) {
        return NULL;
    }

    return &s->sde_props_state[SDEI_EVENT_TO_SLOT(event)];
}

static QemuSDEProp *get_sde_prop(QemuSDEState *s, int32_t event)
{
    QemuSDEProp *sde_props = s->sde_props_state;

    if (!is_valid_event_number(event)) {
        return NULL;
    }

    event = SDEI_EVENT_TO_SLOT(event);

    qemu_mutex_lock(&sde_props[event].lock);
    if (sde_props[event].event_id < 0) {
        qemu_mutex_unlock(&sde_props[event].lock);
        return NULL;
    }
    return &sde_props[event];
}

static void put_sde_prop(QemuSDEProp *prop)
{
    qemu_mutex_unlock(&prop->lock);
}

static void sde_slot_lock(QemuSDE *sde, CPUState *cs)
{
    qemu_mutex_lock(&sde->lock);
}

static void sde_slot_unlock(QemuSDE *sde, CPUState *cs)
{
    qemu_mutex_unlock(&sde->lock);
}

/*
 * It will always return a pointer to a preallocated sde; event number must be
 * validated before calling this function.
 */
static QemuSDE *get_sde_no_check(QemuSDEState *s, int32_t event, CPUState *cs)
{
    QemuSDE **array = s->sde_cpus[cs->cpu_index].private_sde_array;
    int32_t sde_index = SDEI_EVENT_TO_SLOT(event);
    QemuSDE *sde;

    if (SDEI_IS_SHARED_EVENT(event)) {
        array = s->shared_sde_array;
        sde_index -= PRIVATE_SLOT_COUNT;
    }

    sde = array[sde_index];
    sde_slot_lock(sde, cs);
    return sde;
}

static void put_sde(QemuSDE *sde, CPUState *cs)
{
    sde_slot_unlock(sde, cs);
}

static inline bool is_sde_nested(QemuSDECpu *sde_cpu)
{
    return sde_cpu->critical_running_event >= 0 &&
           sde_cpu->normal_running_event >= 0;
}

static int32_t get_running_sde(QemuSDEState *s, CPUState *cs)
{
    QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);

    if (!sde_cpu) {
        return SDEI_INVALID_EVENT_ID;
    }

    if (sde_cpu->critical_running_event >= 0) {
        return sde_cpu->critical_running_event;
    }
    return sde_cpu->normal_running_event;
}

static void override_return_value(CPUState *cs, uint64_t *args)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    int i;

    for (i = 0; i < SMCCC_RETURN_REG_COUNT; i++) {
        args[i] = env->xregs[i];
    }
}

static void sde_save_cpu_ctx(CPUState *cs, QemuSDECpu *sde_cpu, bool critical)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    QemuSDECpuCtx *ctx = &sde_cpu->ctx[critical ? 1 : 0];

    memcpy(ctx->xregs, env->xregs, sizeof(ctx->xregs));
    ctx->pc = env->pc;
    ctx->pstate = pstate_read(env);
}

static void sde_restore_cpu_ctx(QemuSDEState *s, CPUState *cs, bool critical)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);
    QemuSDECpuCtx *ctx;

    if (!sde_cpu) {
        return;
    }

    ctx = &sde_cpu->ctx[critical ? 1 : 0];

    /*
     * TODO: we need to optimize to only restore affected registers by calling
     * ioctl individialy
     */
    kvm_arch_get_registers(cs);

    env->aarch64 = ((ctx->pstate & PSTATE_nRW) == 0);
    memcpy(env->xregs, ctx->xregs, sizeof(ctx->xregs));
    env->pc = ctx->pc;
    pstate_write(env, ctx->pstate);
    aarch64_restore_sp(env, (env->pstate & PSTATE_M) >> PSTATE_M_EL_SHIFT);
}

static void sde_restore_cpu_ctx_for_resume(QemuSDEState *s,
                                           CPUState *cs,
                                           bool critical,
                                           uint64_t resume_addr)
{
    CPUARMState *env = &ARM_CPU(cs)->env;
    QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);
    QemuSDECpuCtx *ctx;

    if (!sde_cpu) {
        return;
    }

    ctx = &sde_cpu->ctx[critical ? 1 : 0];

    /*
     * TODO: we need to optimize to only restore affected registers by calling
     * ioctl individialy
     */
    kvm_arch_get_registers(cs);

    memcpy(env->xregs, ctx->xregs, sizeof(ctx->xregs));
    env->pc = resume_addr;
    env->aarch64 = 1;
    /* Constructe pstate in pstate_read() */
    env->daif = PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F;
    /* Clear nRW/M[4] and M[3:0] */
    env->pstate &= ~(PSTATE_nRW | PSTATE_M);
    /* Set exception mode to EL1h */
    env->pstate |= PSTATE_MODE_EL1h;
    env->elr_el[1] = ctx->pc;
    env->banked_spsr[KVM_SPSR_EL1 + 1] = ctx->pstate;
    aarch64_restore_sp(env, 1);
}

static void sde_build_cpu_ctx(CPUState *cs, QemuSDECpu *sde_cpu, QemuSDE *sde)
{
    CPUARMState *env = &ARM_CPU(cs)->env;

    env->xregs[0] = sde->prop->event_id;
    env->xregs[1] = sde->ep_argument;
    env->xregs[2] = env->pc;
    env->xregs[3] = pstate_read(env);
    env->pc = sde->ep_address;
    env->aarch64 = 1;
    /* Constructe pstate in pstate_read() */
    env->daif = PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F;
    /* Clear nRW/M[4] and M[3:0] */
    env->pstate &= ~(PSTATE_nRW | PSTATE_M);
    /* Set exception mode to EL1h */
    env->pstate |= PSTATE_MODE_EL1h;
    aarch64_restore_sp(env, 1);
}

static void trigger_sde(CPUState *cs, run_on_cpu_data data)
{
    QemuSDEState *s = sde_state;
    QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);
    int32_t event = data.host_int;
    QemuSDE *sde;

    if (!sde_cpu) {
        return;
    }

    if (sde_cpu->masked || sde_cpu->critical_running_event >= 0) {
        return;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        /* Some race condition happens! */
        put_sde(sde, cs);
        return;
    }

    if (sde_cpu->normal_running_event >= 0 && !sde->prop->is_critical) {
        put_sde(sde, cs);
        return;
    }

    if (!sde->enabled || !sde->pending || sde->running) {
        /* Some race condition happens! */
        put_sde(sde, cs);
        return;
    }

    sde->pending = false;
    sde->running = true;

    if (sde->prop->is_critical) {
        sde_cpu->critical_running_event = sde->prop->event_id;
    } else {
        sde_cpu->normal_running_event = sde->prop->event_id;
    }

    kvm_arch_get_registers(cs);
    sde_save_cpu_ctx(cs, sde_cpu, sde->prop->is_critical);
    sde_build_cpu_ctx(cs, sde_cpu, sde);
    kvm_arch_put_registers(cs, 1);
    put_sde(sde, cs);
}

static void dispatch_single(QemuSDEState *s, QemuSDE *sde, CPUState *cs)
{
    int32_t event = sde->prop->event_id;
    bool pending = sde->pending;
    bool enabled = sde->enabled;
    CPUState *target = sde->target_cpu;
    put_sde(sde, cs);

    if (pending && enabled) {
        /*
         * TODO: we need to find a free-unmasked PE to trigger for shared
         * unpinned event
         */
        async_run_on_cpu(target, trigger_sde,
                         RUN_ON_CPU_HOST_INT(event));
    }
}

static bool sde_ready_to_trigger(QemuSDE *sde, CPUState *cs, bool is_critical)
{
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        return false;
    }
    if (sde->prop->is_critical != is_critical) {
        return false;
    }
    if (!sde->enabled || !sde->pending || sde->running ||
        sde->target_cpu != cs) {
        return false;
    }
    return true;
}

static void dispatch_cpu(QemuSDEState *s, CPUState *cs, bool is_critical)
{
    QemuSDE *sde;
    int i;

    for (i = 0; i < PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT; i++) {
        sde = get_sde_no_check(s, i, cs);
        if (!sde_ready_to_trigger(sde, cs, is_critical)) {
            put_sde(sde, cs);
            continue;
        }
        dispatch_single(s, sde, cs);
    }
}

static void qemu_sdei_irq_handler(void *opaque, int irq, int level)
{
    int cpu = 0;

    irq = irq_to_gic_int(sde_state->num_irq, irq, &cpu);
    trigger_sdei_by_irq(cpu, irq);
}

static void override_qemu_irq(QemuSDEState *s, int32_t event, uint32_t intid)
{
    qemu_irq irq;
    QemuSDE *sde;
    CPUState *cs;

    /* SPI */
    if (intid >= GIC_INTERNAL) {
        cs = first_cpu;
        irq = qdev_get_gpio_in(s->gic_dev,
                               gic_int_to_irq(s->num_irq, intid, 0));
        if (irq) {
            qemu_irq_intercept_in(&irq, qemu_sdei_irq_handler, 1);
        }
        sde = get_sde_no_check(s, event, cs);
        sde->irq = irq;
        put_sde(sde, cs);
        return;
    }
    /* PPI */
    CPU_FOREACH(cs) {
        irq = qdev_get_gpio_in(
            s->gic_dev,
            gic_int_to_irq(s->num_irq, intid, cs->cpu_index));
        if (irq) {
            qemu_irq_intercept_in(&irq, qemu_sdei_irq_handler, 1);
        }
        sde = get_sde_no_check(s, event, cs);
        sde->irq = irq;
        put_sde(sde, cs);
    }
}

static void restore_qemu_irq(QemuSDEState *s, int32_t event, uint32_t intid)
{
    QemuSDE *sde;
    CPUState *cs;

    /* SPI */
    if (intid >= GIC_INTERNAL) {
        cs = first_cpu;
        sde = get_sde_no_check(s, event, cs);
        if (sde->irq) {
            qemu_irq_remove_intercept(&sde->irq, 1);
            sde->irq = NULL;
        }
        put_sde(sde, cs);
        return;
    }
    /* PPI */
    CPU_FOREACH(cs) {
        sde = get_sde_no_check(s, event, cs);
        if (sde->irq) {
            qemu_irq_remove_intercept(&sde->irq, 1);
            sde->irq = NULL;
        }
        put_sde(sde, cs);
    }
}

static int32_t sdei_alloc_event_num(QemuSDEState *s, bool is_critical,
                                    bool is_shared, int intid)
{
    int index;
    int start = 0;
    int count = PRIVATE_SLOT_COUNT;
    int32_t event;
    QemuSDEProp *sde_props = s->sde_props_state;

    if (is_shared) {
        start = PRIVATE_SLOT_COUNT;
        count = PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT;
    }

    qemu_mutex_lock(&s->sdei_interrupt_bind_lock);
    for (index = start; index < count; index++) {
        qemu_mutex_lock(&sde_props[index].lock);
        if (sde_props[index].interrupt == intid) {
            event = sde_props[index].event_id;
            qemu_mutex_unlock(&sde_props[index].lock);
            qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);
            return event;
        }
        qemu_mutex_unlock(&sde_props[index].lock);
    }

    for (index = start; index < count; index++) {
        qemu_mutex_lock(&sde_props[index].lock);
        if (sde_props[index].event_id < 0) {
            event = sde_props[index].event_id = 0x40000000 | index;
            sde_props[index].interrupt = intid;
            sde_props[index].is_shared = is_shared;
            sde_props[index].is_critical = is_critical;
            sdei_notify_bind(intid, event, true);
            override_qemu_irq(s, event, intid);
            s->irq_map[intid] = event;
            qemu_mutex_unlock(&sde_props[index].lock);
            qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);
            return event;
        }
        qemu_mutex_unlock(&sde_props[index].lock);
    }
    qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);
    return SDEI_OUT_OF_RESOURCE;
}

static int32_t sdei_free_event_num_locked(QemuSDEState *s, QemuSDEProp *prop)
{
    if (atomic_read(&prop->refcount) > 0) {
        return SDEI_DENIED;
    }

    sdei_notify_bind(prop->interrupt, prop->event_id, false);
    restore_qemu_irq(s, prop->event_id, prop->interrupt);
    s->irq_map[prop->interrupt] = SDEI_INVALID_EVENT_ID;
    prop->event_id = SDEI_INVALID_EVENT_ID;
    prop->interrupt = SDEI_INVALID_INTERRUPT;
    return SDEI_SUCCESS;
}

typedef int64_t (*sdei_single_function)(QemuSDEState *s,
                                        CPUState *cs,
                                        struct kvm_run *run);

static int64_t sdei_version(QemuSDEState *s, CPUState *cs, struct kvm_run *run)
{
    return (1ULL << SDEI_VERSION_MAJOR_SHIFT) |
           (0ULL << SDEI_VERSION_MINOR_SHIFT);
}

static bool inject_event(QemuSDEState *s, CPUState *cs, int32_t event, int irq)
{
    QemuSDE *sde;

    if (event < 0) {
        return false;
    }
    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return false;
    }
    if (irq > 0 && sde->prop->interrupt != irq) {
        /* Someone unbinds the interrupt! */
        put_sde(sde, cs);
        return false;
    }
    sde->pending = true;
    dispatch_single(s, sde, cs);
    return true;
}

static int64_t unregister_single_sde(QemuSDEState *s, int32_t event,
                                     CPUState *cs, bool force)
{
    QemuSDE     *sde;
    QemuSDEProp *prop;
    int         ret = 0;

    prop = get_sde_prop(s, event);
    if (!prop) {
        return SDEI_INVALID_PARAMETERS;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        put_sde_prop(prop);
        return SDEI_DENIED;
    }

    if (sde->running && !force) {
        sde->unregister_pending = true;
        ret = SDEI_PENDING;
    } else {
        atomic_dec(&prop->refcount);
        sde->event_id = SDEI_INVALID_EVENT_ID;
        sde->enabled = false;
        sde->running = false;
        sde->pending = false;
        sde->unregister_pending = false;
    }
    put_sde(sde, cs);
    put_sde_prop(prop);
    return ret;
}

static int64_t sdei_private_reset_common(QemuSDEState *s, CPUState *cs,
                                         bool force)
{
    int64_t ret = SDEI_SUCCESS;
    int i;

    for (i = 0; i < PRIVATE_SLOT_COUNT; i++) {
        int64_t ret1;
        ret1 = unregister_single_sde(s, i, cs, force);
        /* Ignore other return values in reset interface */
        if (ret1 == SDEI_PENDING) {
            ret = SDEI_DENIED;
        }
    }

    return ret;
}

static int64_t sdei_shared_reset_common(QemuSDEState *s, CPUState *cs,
                                        bool force)
{
    int             i;
    QemuSDEProp     *prop;
    int32_t         start_event = PRIVATE_SLOT_COUNT;
    int64_t         ret = SDEI_SUCCESS;

    for (i = start_event; i < PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT; i++) {
        int64_t ret1 = unregister_single_sde(s, i, cs, force);
        /* Ignore other return values in reset interface */
        if (ret1 == SDEI_PENDING) {
            ret = SDEI_DENIED;
        }
    }
    if (ret) {
        return ret;
    }

    qemu_mutex_lock(&s->sdei_interrupt_bind_lock);
    for (i = 0; i < PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT; i++) {
        prop = get_sde_prop(s, i);
        if (!prop || prop->interrupt == SDEI_INVALID_INTERRUPT) {
            if (prop) {
                put_sde_prop(prop);
            }
            continue;
        }
        ret |= sdei_free_event_num_locked(s, prop);
        put_sde_prop(prop);
    }
    qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);

    return ret ? SDEI_DENIED : SDEI_SUCCESS;
}

#define SDEI_EV_REGISTER_RM_MASK 1ULL

static int64_t sdei_event_register(QemuSDEState *s, CPUState *cs,
                                   struct kvm_run *run)
{
    QemuSDE *sde;
    QemuSDEProp *prop;
    CPUState *target = cs;
    uint64_t *args = (uint64_t *)run->hypercall.args;
    int32_t event = args[1];
    uint64_t rm_mode = SDEI_EVENT_REGISTER_RM_PE;

    prop = get_sde_prop(s, event);
    if (!prop) {
        return SDEI_INVALID_PARAMETERS;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id != SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        put_sde_prop(prop);
        return SDEI_DENIED;
    }

    if (prop->is_shared) {
        rm_mode = args[4] & SDEI_EV_REGISTER_RM_MASK;
        if (rm_mode == SDEI_EVENT_REGISTER_RM_PE) {
            target = arm_get_cpu_by_id(args[5]);
            if (!target) {
                put_sde_prop(prop);
                return SDEI_INVALID_PARAMETERS;
            }
        }
    }

    sde->target_cpu = target;
    sde->ep_address = args[2];
    sde->ep_argument = args[3];
    sde->prop = prop;
    sde->routing_mode = rm_mode;
    sde->event_id = prop->event_id;

    put_sde(sde, cs);
    atomic_inc(&prop->refcount);
    put_sde_prop(prop);

    return SDEI_SUCCESS;
}

static int64_t sdei_event_enable(QemuSDEState *s, CPUState *cs,
                                 struct kvm_run *run)
{
    QemuSDE *sde;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    int32_t event = args[1];

    if (!is_valid_event_number(event)) {
        return SDEI_INVALID_PARAMETERS;
    }
    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return SDEI_INVALID_PARAMETERS;
    }

    sde->enabled = true;
    dispatch_single(s, sde, cs);
    return SDEI_SUCCESS;
}

static int64_t sdei_event_disable(QemuSDEState *s, CPUState *cs,
                                  struct kvm_run *run)
{
    QemuSDE *sde;
    uint64_t *args = (uint64_t *)run->hypercall.args;
    int32_t event = args[1];

    if (!is_valid_event_number(event)) {
        return SDEI_INVALID_PARAMETERS;
    }
    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return SDEI_INVALID_PARAMETERS;
    }

    sde->enabled = false;
    put_sde(sde, cs);
    return SDEI_SUCCESS;
}

static int64_t sdei_event_context(QemuSDEState *s, CPUState *cs,
                                  struct kvm_run *run)
{
    QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    uint32_t param_id = args[1];
    int critical;
    QemuSDECpuCtx *ctx;

    if (param_id >= SDEI_PARAM_MAX) {
        return SDEI_INVALID_PARAMETERS;
    }

    if (!sde_cpu) {
        return SDEI_DENIED;
    }

    if (sde_cpu->critical_running_event >= 0) {
        critical = 1;
    } else if (sde_cpu->normal_running_event >= 0) {
        critical = 0;
    } else {
        return SDEI_DENIED;
    }

    ctx = &sde_cpu->ctx[critical];
    return ctx->xregs[param_id];
}

static int64_t sdei_event_complete(QemuSDEState *s, CPUState *cs,
                                   struct kvm_run *run)
{
    QemuSDE *sde;
    QemuSDECpu *cpu = get_sde_cpu(s, cs);
    int32_t event;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    bool is_critical;

    if (!cpu) {
        return SDEI_DENIED;
    }

    event = get_running_sde(s, cs);
    if (event < 0) {
        return SDEI_DENIED;
    }

    if (!is_valid_event_number(event)) {
        error_report("BUG: running event number 0x%x is invalid!",
                     event);
        return SDEI_DENIED;
    }
    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id != event) {
        error_report("BUG: sde event id 0x%x != running event 0x%x!",
                     sde->event_id, event);
        put_sde(sde, cs);
        return SDEI_DENIED;
    }

    sde->running = false;
    is_critical = sde->prop->is_critical;
    if (sde->unregister_pending) {
        atomic_dec(&sde->prop->refcount);
        sde->event_id = SDEI_INVALID_EVENT_ID;
        sde->unregister_pending = false;
    }
    put_sde(sde, cs);

    sde_restore_cpu_ctx(s, cs, is_critical);

    kvm_arch_put_registers(cs, 1);
    override_return_value(cs, args);
    if (cpu->critical_running_event >= 0) {
        cpu->critical_running_event = SDEI_INVALID_EVENT_ID;
    } else {
        cpu->normal_running_event = SDEI_INVALID_EVENT_ID;
    }

    /* TODO: we should not queue more than one sde in work queue */
    dispatch_cpu(s, cs, true);
    if (cpu->critical_running_event < 0 && cpu->normal_running_event < 0) {
        dispatch_cpu(s, cs, false);
    }
    return args[0];
}

static int64_t sdei_event_complete_and_resume(QemuSDEState *s, CPUState *cs,
                                              struct kvm_run *run)
{
    QemuSDE *sde;
    QemuSDECpu *cpu = get_sde_cpu(s, cs);
    int32_t event;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    bool is_critical;
    uint64_t resume_addr = args[1];

    if (!cpu) {
        return SDEI_DENIED;
    }

    event = get_running_sde(s, cs);
    if (event < 0) {
        return SDEI_DENIED;
    }

    if (!is_valid_event_number(event)) {
        error_report("BUG: running event number 0x%x is invalid!",
                     event);
        return SDEI_DENIED;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id != event) {
        error_report("BUG: sde event id 0x%x != running event 0x%x!",
                     sde->event_id, event);
        put_sde(sde, cs);
        return SDEI_DENIED;
    }

    sde->running = false;
    is_critical = sde->prop->is_critical;

    if (sde->unregister_pending) {
        atomic_dec(&sde->prop->refcount);
        sde->event_id = SDEI_INVALID_EVENT_ID;
        sde->unregister_pending = false;
    }
    put_sde(sde, cs);

    sde_restore_cpu_ctx_for_resume(s, cs, is_critical, resume_addr);
    kvm_arch_put_registers(cs, 1);

    override_return_value(cs, args);
    if (cpu->critical_running_event >= 0) {
        cpu->critical_running_event = SDEI_INVALID_EVENT_ID;
    } else {
        cpu->normal_running_event = SDEI_INVALID_EVENT_ID;
    }

    dispatch_cpu(s, cs, true);
    if (cpu->critical_running_event < 0 && cpu->normal_running_event < 0) {
        dispatch_cpu(s, cs, false);
    }
    return args[0];
}

static int64_t sdei_event_unregister(QemuSDEState *s, CPUState *cs,
                                     struct kvm_run *run)
{
    uint64_t        *args = (uint64_t *)(run->hypercall.args);
    int32_t         event = args[1];

    return unregister_single_sde(s, event, cs, false);
}

static int64_t sdei_event_status(QemuSDEState *s, CPUState *cs,
                                 struct kvm_run *run)
{
    QemuSDE *sde;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    int32_t event = args[1];
    int64_t status = 0;

    if (!is_valid_event(s, event)) {
        return SDEI_INVALID_PARAMETERS;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return status;
    }

    status |= SDEI_EVENT_STATUS_REGISTERED;
    if (sde->enabled) {
        status |= SDEI_EVENT_STATUS_ENABLED;
    }
    if (sde->running) {
        status |= SDEI_EVENT_STATUS_RUNNING;
    }
    put_sde(sde, cs);
    return status;
}

static int64_t sdei_event_get_info(QemuSDEState *s, CPUState *cs,
                                   struct kvm_run *run)
{
    QemuSDEProp *prop;
    QemuSDE *sde;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    int32_t event = args[1];
    uint32_t info = args[2];
    int64_t ret = SDEI_INVALID_PARAMETERS;

    if (info > SDEI_EVENT_INFO_EV_ROUTING_AFF) {
        return SDEI_INVALID_PARAMETERS;
    }

    prop = get_sde_prop(s, event);
    if (!prop) {
        return SDEI_INVALID_PARAMETERS;
    }

    switch (info) {
    case SDEI_EVENT_INFO_EV_TYPE:
        ret = prop->is_shared;
        break;
    case SDEI_EVENT_INFO_EV_SIGNALED:
        ret = (event == SDEI_STD_EVT_SOFTWARE_SIGNAL) ? 1 : 0;
        break;
    case SDEI_EVENT_INFO_EV_PRIORITY:
        ret = prop->is_critical;
        break;
    case SDEI_EVENT_INFO_EV_ROUTING_MODE:
    case SDEI_EVENT_INFO_EV_ROUTING_AFF:
        if (!prop->is_shared) {
            break;
        }
        sde = get_sde_no_check(s, event, cs);
        if (sde->event_id == SDEI_INVALID_EVENT_ID) {
            put_sde(sde, cs);
            ret = SDEI_DENIED;
            break;
        }
        if (info == SDEI_EVENT_INFO_EV_ROUTING_MODE) {
            ret = sde->routing_mode;
        } else if (sde->routing_mode == SDEI_EVENT_REGISTER_RM_PE) {
            ret = ARM_CPU(sde->target_cpu)->mp_affinity;
        }
        put_sde(sde, cs);
        break;
    default:
        ret = SDEI_NOT_SUPPORTED;
    }
    put_sde_prop(prop);
    return ret;
}

static int64_t sdei_event_routing_set(QemuSDEState *s, CPUState *cs,
                                      struct kvm_run *run)
{
    QemuSDE *sde;
    CPUState *target = cs;
    uint64_t *args = (uint64_t *)run->hypercall.args;
    int32_t event = args[1];
    uint64_t mode = args[2];
    uint64_t affinity = args[3];

    if (mode & ~1ULL) {
        return SDEI_INVALID_PARAMETERS;
    }
    if (mode == SDEI_EVENT_REGISTER_RM_PE) {
        target = arm_get_cpu_by_id(affinity);
        if (!target) {
            return SDEI_INVALID_PARAMETERS;
        }
    }

    if (!is_valid_event(s, event) || !SDEI_IS_SHARED_EVENT(event)) {
        return SDEI_INVALID_PARAMETERS;
    }

    sde = get_sde_no_check(s, event, cs);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return SDEI_DENIED;
    }
    if (sde->enabled || sde->running ||
        sde->pending || sde->unregister_pending) {
        put_sde(sde, cs);
        return SDEI_DENIED;
    }

    sde->target_cpu = target;
    sde->routing_mode = mode;
    put_sde(sde, cs);

    return SDEI_SUCCESS;
}

static int64_t sdei_event_pe_mask(QemuSDEState *s, CPUState *cs,
                                  struct kvm_run *run)
{
    QemuSDECpu *sde_cpu;

    sde_cpu = get_sde_cpu(s, cs);
    if (!sde_cpu) {
        return SDEI_DENIED;
    }

    if (sde_cpu->masked) {
        return 0;
    }
    sde_cpu->masked = true;
    return 1;
}

static int64_t sdei_event_pe_unmask(QemuSDEState *s, CPUState *cs,
                                    struct kvm_run *run)
{
    QemuSDECpu *sde_cpu;

    sde_cpu = get_sde_cpu(s, cs);
    if (!sde_cpu) {
        return SDEI_DENIED;
    }

    sde_cpu->masked = false;
    dispatch_cpu(s, cs, true);
    dispatch_cpu(s, cs, false);
    return SDEI_SUCCESS;
}

static int dev_walkerfn(DeviceState *dev, void *opaque)
{
    QemuSDEState *s = opaque;

    if (object_dynamic_cast(OBJECT(dev), TYPE_ARM_GICV3_COMMON)) {
        GICv3State *gic = ARM_GICV3_COMMON(dev);
        s->num_irq = gic->num_irq;
        s->gic_dev = dev;
        return -1;
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_ARM_GIC_COMMON)) {
        GICState *gic = ARM_GIC_COMMON(dev);
        s->num_irq = gic->num_irq;
        s->gic_dev = dev;
        return -1;
    }
    return 0;
}

static int64_t sdei_event_interrupt_bind(QemuSDEState *s, CPUState *cs,
                                         struct kvm_run *run)
{
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    uint32_t intid = args[1];

    if (intid < GIC_NR_SGIS || intid >= s->num_irq) {
        return SDEI_INVALID_PARAMETERS;
    }
    return sdei_alloc_event_num(s, false, intid >= GIC_INTERNAL, intid);
}

static int64_t sdei_event_interrupt_release(QemuSDEState *s, CPUState *cs,
                                            struct kvm_run *run)
{
    QemuSDEProp *prop;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    int32_t event = args[1];
    int32_t ret;

    qemu_mutex_lock(&s->sdei_interrupt_bind_lock);
    prop = get_sde_prop(s, event);
    if (!prop) {
        qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);
        return SDEI_INVALID_PARAMETERS;
    }

    ret = sdei_free_event_num_locked(s, prop);
    put_sde_prop(prop);
    qemu_mutex_unlock(&s->sdei_interrupt_bind_lock);
    return ret;
}

static int64_t sdei_event_signal(QemuSDEState *s, CPUState *cs,
                                 struct kvm_run *run)
{
    QemuSDE *sde;
    CPUState *target_cpu;
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    int32_t event = args[1];

    if (event != SDEI_STD_EVT_SOFTWARE_SIGNAL) {
        return SDEI_INVALID_PARAMETERS;
    }

    target_cpu = arm_get_cpu_by_id(args[2]);
    if (!target_cpu) {
        return SDEI_INVALID_PARAMETERS;
    }

    sde = get_sde_no_check(s, event, target_cpu);
    if (sde->event_id == SDEI_INVALID_EVENT_ID) {
        put_sde(sde, cs);
        return SDEI_INVALID_PARAMETERS;
    }

    sde->pending = true;
    dispatch_single(s, sde, target_cpu);
    return SDEI_SUCCESS;
}

#define SDEI_FEATURES_SHARED_SLOTS_SHIFT 16
static int64_t sdei_features(QemuSDEState *s, CPUState *cs, struct kvm_run *run)
{
    uint64_t *args = (uint64_t *)(run->hypercall.args);
    uint32_t feature = args[1];

    switch (feature) {
    case SDEI_FEATURE_BIND_SLOTS:
        return ((SHARED_SLOT_COUNT - PLAT_SHARED_SLOT_COUNT) <<
                 SDEI_FEATURES_SHARED_SLOTS_SHIFT) |
               (PRIVATE_SLOT_COUNT - PLAT_PRIVATE_SLOT_COUNT);
    default:
        return SDEI_INVALID_PARAMETERS;
    }
}

static int64_t sdei_private_reset(QemuSDEState *s, CPUState *cs,
                                  struct kvm_run *run)
{
    return sdei_private_reset_common(s, cs, false);
}

static int64_t sdei_shared_reset(QemuSDEState *s, CPUState *cs,
                                 struct kvm_run *run)
{
    return sdei_shared_reset_common(s, cs, false);
}

static sdei_single_function sdei_functions[] = {
    sdei_version,
    sdei_event_register,
    sdei_event_enable,
    sdei_event_disable,
    sdei_event_context,
    sdei_event_complete,
    sdei_event_complete_and_resume,
    sdei_event_unregister,
    sdei_event_status,
    sdei_event_get_info,
    sdei_event_routing_set,
    sdei_event_pe_mask,
    sdei_event_pe_unmask,
    sdei_event_interrupt_bind,
    sdei_event_interrupt_release,
    sdei_event_signal,
    sdei_features,
    sdei_private_reset,
    sdei_shared_reset,
};

void sdei_handle_request(CPUState *cs, struct kvm_run *run)
{
    uint32_t func_id = run->hypercall.args[0];

    if (!sde_state) {
        run->hypercall.args[0] = SDEI_NOT_SUPPORTED;
        return;
    }

    if (!sde_state->gic_dev) {
        /* Search for ARM GIC device */
        qbus_walk_children(sysbus_get_default(), dev_walkerfn,
                           NULL, NULL, NULL, sde_state);
        if (!sde_state->gic_dev) {
            error_report("Cannot find ARM GIC device!");
            run->hypercall.args[0] = SDEI_NOT_SUPPORTED;
            return;
        }
    }

    if (func_id < SDEI_1_0_FN_BASE || func_id > SDEI_MAX_REQ) {
        error_report("Invalid SDEI function ID: 0x%x", func_id);
        run->hypercall.args[0] = SDEI_INVALID_PARAMETERS;
        return;
    }

    func_id -= SDEI_1_0_FN_BASE;
    if (func_id < ARRAY_SIZE(sdei_functions) && sdei_functions[func_id]) {
        run->hypercall.args[0] = sdei_functions[func_id](sde_state, cs, run);
    } else {
        run->hypercall.args[0] = SDEI_NOT_SUPPORTED;
    }
}

bool trigger_sdei_by_irq(int cpu, int irq)
{
    QemuSDEState *s = sde_state;

    if (!s || irq >= ARRAY_SIZE(s->irq_map)) {
        return false;
    }

    if (s->irq_map[irq] == SDEI_INVALID_EVENT_ID) {
        return false;
    }

    return inject_event(s, qemu_get_cpu(cpu), s->irq_map[irq], irq);
}

static void sde_array_init(QemuSDE **array, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        QemuSDE *sde;
        sde = array[i];
        if (!sde) {
            sde = g_new0(QemuSDE, 1);
        }
        sde->event_id = SDEI_INVALID_EVENT_ID;
        sde->enabled = false;
        sde->running = false;
        sde->pending = false;
        sde->unregister_pending = false;
        qemu_mutex_init(&sde->lock);
        array[i] = sde;
    }
}

static void qemu_shared_sde_init(QemuSDEState *s)
{
    sde_array_init(s->shared_sde_array, SHARED_SLOT_COUNT);
}

static void qemu_private_sde_init(QemuSDEState *s)
{
    int i;

    for (i = 0; i < s->sdei_max_cpus; i++) {
        sde_array_init(s->sde_cpus[i].private_sde_array, PRIVATE_SLOT_COUNT);
    }
}

static void qemu_sde_init(QemuSDEState *s)
{
    qemu_sde_prop_init(s);
    qemu_sde_cpu_init(s);

    qemu_shared_sde_init(s);
    qemu_private_sde_init(s);
}

static void qemu_sde_reset(void *opaque)
{
    int64_t         ret = 0;
    CPUState        *cs;
    QemuSDEState    *s = opaque;

    CPU_FOREACH(cs) {
        QemuSDECpu *sde_cpu = get_sde_cpu(s, cs);
        ret |= sdei_private_reset_common(s, cs, true);
        sde_cpu->masked = true;
        sde_cpu->critical_running_event = SDEI_INVALID_EVENT_ID;
        sde_cpu->normal_running_event = SDEI_INVALID_EVENT_ID;
    }

    ret |= sdei_shared_reset_common(s, first_cpu, true);
    if (ret) {
        error_report("SDEI system reset failed: 0x%lx", ret);
    }
}

static void sde_array_save(QemuSDE **array, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        QemuSDE *sde = array[i];
        if (sde->event_id != SDEI_INVALID_EVENT_ID) {
            sde->event_id = sde->prop->event_id;
            sde->cpu_affinity = ARM_CPU(sde->target_cpu)->mp_affinity;
        }
    }
}

static int qemu_sdei_pre_save(void *opaque)
{
    QemuSDEState *s = opaque;
    int i;

    for (i = 0; i < s->sdei_max_cpus; i++) {
        sde_array_save(s->sde_cpus[i].private_sde_array, PRIVATE_SLOT_COUNT);
    }

    sde_array_save(s->shared_sde_array, SHARED_SLOT_COUNT);

    return 0;
}


static int qemu_sdei_post_load(void *opaque, int version_id)
{
    QemuSDEState *s = opaque;
    QemuSDEProp *sde_props = s->sde_props_state;
    QemuSDE **array;
    int i, j;

    for (i = 0; i < s->sdei_max_cpus; i++) {
        array = s->sde_cpus[i].private_sde_array;
        for (j = 0; j < PRIVATE_SLOT_COUNT; j++) {
            QemuSDE *sde = array[j];
            if (sde->event_id != SDEI_INVALID_EVENT_ID) {
                sde->prop = get_sde_prop_no_lock(s, sde->event_id);
                sde->target_cpu = arm_get_cpu_by_id(sde->cpu_affinity);
            }
        }
    }

    array = s->shared_sde_array;
    for (j = 0; j < SHARED_SLOT_COUNT; j++) {
        QemuSDE *sde = array[j];
        if (sde->event_id != SDEI_INVALID_EVENT_ID) {
            sde->prop = get_sde_prop_no_lock(s, sde->event_id);
            sde->target_cpu = arm_get_cpu_by_id(sde->cpu_affinity);
        }
    }

    /* Search for ARM GIC device */
    qbus_walk_children(sysbus_get_default(), dev_walkerfn,
                       NULL, NULL, NULL, s);
    if (!s->gic_dev) {
        error_report("Cannot find ARM GIC device!");
        return 0;
    }

    for (i = 0; i < PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT; i++) {
        int intid = sde_props[i].interrupt;

        if (intid != SDEI_INVALID_INTERRUPT) {
            s->irq_map[intid] = sde_props[i].event_id;
            override_qemu_irq(s, sde_props[i].event_id, intid);
        }
    }

    return 0;
}

static const VMStateDescription vmstate_sdes = {
    .name = "qemu_sdei/sdes",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(enabled, QemuSDE),
        VMSTATE_BOOL(running, QemuSDE),
        VMSTATE_BOOL(pending, QemuSDE),
        VMSTATE_BOOL(unregister_pending, QemuSDE),
        VMSTATE_UINT64(ep_address, QemuSDE),
        VMSTATE_UINT64(ep_argument, QemuSDE),
        VMSTATE_UINT64(routing_mode, QemuSDE),
        VMSTATE_INT32(event_id, QemuSDE),
        VMSTATE_UINT64(cpu_affinity, QemuSDE),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sde_props = {
    .name = "qemu_sdei/sde_props",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(event_id, QemuSDEProp),
        VMSTATE_INT32(interrupt, QemuSDEProp),
        VMSTATE_BOOL(is_shared, QemuSDEProp),
        VMSTATE_BOOL(is_critical, QemuSDEProp),
        VMSTATE_INT32(sde_index, QemuSDEProp),
        VMSTATE_INT32(refcount, QemuSDEProp),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sde_cpu = {
    .name = "qemu_sdei/sde_cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(private_sde_array, QemuSDECpu,
                                           PRIVATE_SLOT_COUNT, 1,
                                           vmstate_sdes, QemuSDE),
        VMSTATE_UINT64_ARRAY(ctx[0].xregs, QemuSDECpu, SAVED_GP_NUM),
        VMSTATE_UINT64_ARRAY(ctx[1].xregs, QemuSDECpu, SAVED_GP_NUM),
        VMSTATE_UINT64(ctx[0].pc, QemuSDECpu),
        VMSTATE_UINT64(ctx[1].pc, QemuSDECpu),
        VMSTATE_UINT32(ctx[0].pstate, QemuSDECpu),
        VMSTATE_UINT32(ctx[1].pstate, QemuSDECpu),
        VMSTATE_INT32(critical_running_event, QemuSDECpu),
        VMSTATE_INT32(normal_running_event, QemuSDECpu),
        VMSTATE_BOOL(masked, QemuSDECpu),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sde_state = {
    .name = "qemu_sdei",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = qemu_sdei_pre_save,
    .post_load = qemu_sdei_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sde_props_state, QemuSDEState,
                             PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT, 1,
                             vmstate_sde_props, QemuSDEProp),
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(shared_sde_array, QemuSDEState,
                                           SHARED_SLOT_COUNT, 1,
                                           vmstate_sdes, QemuSDE),
        VMSTATE_STRUCT_VARRAY_POINTER_INT32(sde_cpus, QemuSDEState,
                                            sdei_max_cpus,
                                            vmstate_sde_cpu, QemuSDECpu),
        VMSTATE_END_OF_LIST()
    }
};


static void sdei_initfn(Object *obj)
{
    QemuSDEState *s = QEMU_SDEI(obj);

    if (sde_state) {
        error_report("Only one SDEI dispatcher is allowed!");
        abort();
    }
    sde_state = s;

    qemu_sde_init(s);
    qemu_register_reset(qemu_sde_reset, s);
}

static void qemu_sde_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "SDEI_QEMU";
    dc->vmsd = &vmstate_sde_state;
    dc->user_creatable = true;
}

static const TypeInfo sde_qemu_info = {
    .name          = TYPE_ARM_SDEI,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(QemuSDEState),
    .instance_init = sdei_initfn,
    .class_init    = qemu_sde_class_init,
};

static void register_types(void)
{
    type_register_static(&sde_qemu_info);
}

type_init(register_types);
