/*
 * VM Introspection
 *
 * Copyright (C) 2017-2020 Bitdefender S.R.L.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"

typedef struct VMIntrospection {
    Object parent_obj;

    Error *init_error;

    char *chardevid;
    Chardev *chr;

    Notifier machine_ready;
    bool created_from_command_line;
} VMIntrospection;

#define TYPE_VM_INTROSPECTION "introspection"

#define VM_INTROSPECTION(obj) \
    OBJECT_CHECK(VMIntrospection, (obj), TYPE_VM_INTROSPECTION)

static Error *vm_introspection_init(VMIntrospection *i);

static void machine_ready(Notifier *notifier, void *data)
{
    VMIntrospection *i = container_of(notifier, VMIntrospection, machine_ready);

    i->init_error = vm_introspection_init(i);
    if (i->init_error) {
        Error *err = error_copy(i->init_error);

        error_report_err(err);
        if (i->created_from_command_line) {
            exit(1);
        }
    }
}

static void complete(UserCreatable *uc, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(uc);

    if (!i->chardevid) {
        error_setg(errp, "VMI: chardev is not set");
        return;
    }

    i->machine_ready.notify = machine_ready;
    qemu_add_machine_init_done_notifier(&i->machine_ready);

    /*
     * If the introspection object is created while parsing the command line,
     * the machine_ready callback will be called later. At that time,
     * it vm_introspection_init() fails, exit() will be called.
     *
     * If the introspection object is created through QMP, machine_init_done
     * is already set and qemu_add_machine_init_done_notifier() will
     * call our machine_done() callback. If vm_introspection_init() fails,
     * we don't call exit() and report the error back to the user.
     */
    if (i->init_error) {
        *errp = i->init_error;
        i->init_error = NULL;
        return;
    }
}

static void prop_set_chardev(Object *obj, const char *value, Error **errp)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);
    i->chardevid = g_strdup(value);
}

static void class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *uc = USER_CREATABLE_CLASS(oc);

    uc->complete = complete;
}

static void instance_init(Object *obj)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    i->created_from_command_line = (qdev_hotplug == false);

    object_property_add_str(obj, "chardev", NULL, prop_set_chardev, NULL);
}

static void instance_finalize(Object *obj)
{
    VMIntrospection *i = VM_INTROSPECTION(obj);

    g_free(i->chardevid);

    error_free(i->init_error);
}

static const TypeInfo info = {
    .name              = TYPE_VM_INTROSPECTION,
    .parent            = TYPE_OBJECT,
    .class_init        = class_init,
    .instance_size     = sizeof(VMIntrospection),
    .instance_finalize = instance_finalize,
    .instance_init     = instance_init,
    .interfaces        = (InterfaceInfo[]){
        {TYPE_USER_CREATABLE},
        {}
    }
};

static void register_types(void)
{
    type_register_static(&info);
}

type_init(register_types);

static Error *vm_introspection_init(VMIntrospection *i)
{
    Error *err = NULL;
    int kvmi_version;
    Chardev *chr;

    if (!kvm_enabled()) {
        error_setg(&err, "VMI: missing KVM support");
        return err;
    }

    kvmi_version = kvm_check_extension(kvm_state, KVM_CAP_INTROSPECTION);
    if (kvmi_version == 0) {
        error_setg(&err,
                   "VMI: missing kernel built with CONFIG_KVM_INTROSPECTION");
        return err;
    }

    chr = qemu_chr_find(i->chardevid);
    if (!chr) {
        error_setg(&err, "VMI: device '%s' not found", i->chardevid);
        return err;
    }

    if (!object_property_get_bool(OBJECT(chr), "reconnecting", &err)) {
        error_append_hint(&err, "VMI: missing reconnect=N for '%s'",
                          i->chardevid);
        return err;
    }

    i->chr = chr;

    return NULL;
}
