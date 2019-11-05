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

static QemuSDEState *sde_state;

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

    for (i = 0; i < PRIVATE_SLOT_COUNT + SHARED_SLOT_COUNT; i++) {
        int intid = sde_props[i].interrupt;

        if (intid != SDEI_INVALID_INTERRUPT) {
            s->irq_map[intid] = sde_props[i].event_id;
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
