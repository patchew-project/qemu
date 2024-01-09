/*
 * QEMU Gunyah hypervisor support
 *
 * (based on KVM accelerator code structure)
 *
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "sysemu/accel-ops.h"
#include "sysemu/cpus.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "qapi/visitor.h"
#include "qapi/error.h"

bool gunyah_allowed;

static int gunyah_init(MachineState *ms)
{
    return gunyah_create_vm();
}

static void gunyah_accel_instance_init(Object *obj)
{
    GUNYAHState *s = GUNYAH_STATE(obj);

    s->fd = -1;
    s->vmfd = -1;
}

static void gunyah_get_preshmem_size(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    GUNYAHState *s = GUNYAH_STATE(obj);
    uint32_t value = s->preshmem_size;

    visit_type_uint32(v, name, &value, errp);
}

static void gunyah_set_preshmem_size(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    GUNYAHState *s = GUNYAH_STATE(obj);
    uint32_t value;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after VM is created");
        return;
    }

    if (!visit_type_uint32(v, name, &value, errp)) {
        error_setg(errp, "preshmem-size must be an unsigned integer");
        return;
    }

    if (value & (value - 1)) {
        error_setg(errp, "preshmem-size must be a power of two");
        return;
    }

    if (!s->is_protected_vm) {
        error_setg(errp, "preshmem-size is applicable only for protected VMs");
        return;
    }

    s->preshmem_size = value;
}

static bool gunyah_get_protected_vm(Object *obj, Error **errp)
{
    GUNYAHState *s = GUNYAH_STATE(obj);

    return s->is_protected_vm;
}

static void gunyah_set_protected_vm(Object *obj, bool value, Error **errp)
{
    GUNYAHState *s = GUNYAH_STATE(obj);

    s->is_protected_vm = value;
}

static void gunyah_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);

    ac->name = "GUNYAH";
    ac->init_machine = gunyah_init;
    ac->allowed = &gunyah_allowed;

    object_class_property_add_bool(oc, "protected-vm",
                    gunyah_get_protected_vm, gunyah_set_protected_vm);
    object_class_property_set_description(oc, "protected-vm",
            "Launch a VM of protected type");

    object_class_property_add(oc, "preshmem-size", "uint32",
                gunyah_get_preshmem_size, gunyah_set_preshmem_size, NULL, NULL);
    object_class_property_set_description(oc, "preshmem-size",
        "This property is applicable for protected VMs and indicates "
        "the portion of VM's memory that should be shared with its host");
}

static const TypeInfo gunyah_accel_type = {
    .name = TYPE_GUNYAH_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = gunyah_accel_instance_init,
    .class_init = gunyah_accel_class_init,
    .instance_size = sizeof(GUNYAHState),
};

static void gunyah_type_init(void)
{
    type_register_static(&gunyah_accel_type);
}
type_init(gunyah_type_init);

static void gunyah_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);

    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/Gunyah",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, gunyah_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

static void gunyah_kick_vcpu_thread(CPUState *cpu)
{
    cpus_kick_thread(cpu);
}

static bool gunyah_vcpu_thread_is_idle(CPUState *cpu)
{
    return false;
}

static void gunyah_accel_ops_class_init(ObjectClass *oc, void *data)
{
    AccelOpsClass *ops = ACCEL_OPS_CLASS(oc);

    ops->create_vcpu_thread = gunyah_start_vcpu_thread;
    ops->kick_vcpu_thread = gunyah_kick_vcpu_thread;
    ops->cpu_thread_is_idle = gunyah_vcpu_thread_is_idle;
};

static const TypeInfo gunyah_accel_ops_type = {
    .name = ACCEL_OPS_NAME("gunyah"),
    .parent = TYPE_ACCEL_OPS,
    .class_init = gunyah_accel_ops_class_init,
    .abstract = true,
};

static void gunyah_accel_ops_register_types(void)
{
    type_register_static(&gunyah_accel_ops_type);
}

type_init(gunyah_accel_ops_register_types);
