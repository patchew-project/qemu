/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qom/object_interfaces.h"

#include "nvme.h"

int nvme_subsys_register_ctrl(NvmeSubsystem *subsys, NvmeState *n,
                              Error **errp)
{
    int cntlid, nsid;

    for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
        if (!subsys->ctrls[cntlid]) {
            break;
        }
    }

    if (cntlid == ARRAY_SIZE(subsys->ctrls)) {
        error_setg(errp, "no more free controller id");
        return -1;
    }

    subsys->ctrls[cntlid] = n;

    for (nsid = 1; nsid < ARRAY_SIZE(subsys->namespaces); nsid++) {
        NvmeNamespace *ns = subsys->namespaces[nsid];
        if (ns && (ns->flags & NVME_NS_SHARED)) {
            nvme_attach_ns(n, ns);
        }
    }

    return cntlid;
}

void nvme_subsys_unregister_ctrl(NvmeSubsystem *subsys, NvmeState *n)
{
    subsys->ctrls[n->cntlid] = NULL;
    n->cntlid = -1;
}

static void get_controllers(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);
    strList *paths = NULL;
    strList **tail = &paths;
    unsigned cntlid;

    for (cntlid = 0; cntlid < NVME_MAX_CONTROLLERS; cntlid++) {
        NvmeState *n = subsys->ctrls[cntlid];
        if (!n) {
            continue;
        }

        QAPI_LIST_APPEND(tail, object_get_canonical_path(OBJECT(n)));
    }

    visit_type_strList(v, name, &paths, errp);
    qapi_free_strList(paths);
}

static void get_namespaces(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);
    strList *paths = NULL;
    strList **tail = &paths;
    unsigned nsid;

    for (nsid = 1; nsid <= NVME_MAX_NAMESPACES; nsid++) {
        NvmeNamespace *ns = subsys->namespaces[nsid];
        if (!ns) {
            continue;
        }

        QAPI_LIST_APPEND(tail, object_get_canonical_path(OBJECT(ns)));
    }

    visit_type_strList(v, name, &paths, errp);
    qapi_free_strList(paths);
}

static char *get_subnqn(Object *obj, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);
    return g_strdup((char *)subsys->subnqn);
}

static void set_subnqn(Object *obj, const char *str, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);
    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn), "%s", str);
}

static char *get_uuid(Object *obj, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);
    char buf[UUID_FMT_LEN + 1];

    qemu_uuid_unparse(&subsys->uuid, buf);

    return g_strdup(buf);
}

static void set_uuid(Object *obj, const char *str, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(obj);

    if (!strcmp(str, "auto")) {
        qemu_uuid_generate(&subsys->uuid);
    } else if (qemu_uuid_parse(str, &subsys->uuid) < 0) {
        error_setg(errp, "invalid uuid");
        return;
    }
}

static void nvme_subsys_complete(UserCreatable *uc, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYSTEM(uc);

    if (!strcmp((char *)subsys->subnqn, "")) {
        char buf[UUID_FMT_LEN + 1];

        qemu_uuid_unparse(&subsys->uuid, buf);

        snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
                 "nqn.2014-08.org.nvmexpress:uuid:%s", buf);
    }
}

static void nvme_subsys_instance_init(Object *obj)
{
    object_property_add(obj, "controllers", "str", get_controllers, NULL, NULL,
                        NULL);

    object_property_add(obj, "namespaces", "str", get_namespaces, NULL, NULL,
                        NULL);
}

static void nvme_subsys_class_init(ObjectClass *oc, void *data)
{
    ObjectProperty *op;

    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    ucc->complete = nvme_subsys_complete;

    object_class_property_add_str(oc, "subnqn", get_subnqn, set_subnqn);
    object_class_property_set_description(oc, "subnqn", "the NVM Subsystem "
                                          "NVMe Qualified Name; "
                                          "(default: \"nqn.2014-08.org.nvmexpress:uuid:<uuid>\")");

    op = object_class_property_add_str(oc, "uuid", get_uuid, set_uuid);
    object_property_set_default_str(op, "auto");
    object_class_property_set_description(oc, "subnqn", "NVM Subsystem UUID "
                                          "(\"auto\" for random value; "
                                          "default: \"auto\")");
}

static const TypeInfo nvme_subsys_info = {
    .name = TYPE_NVME_SUBSYSTEM,
    .parent = TYPE_OBJECT,
    .class_init = nvme_subsys_class_init,
    .instance_init = nvme_subsys_instance_init,
    .instance_size = sizeof(NvmeSubsystem),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { },
    }
};

static void nvme_subsys_device_setup(NvmeSubsystemDevice *dev)
{
    NvmeSubsystem *subsys = &dev->subsys;
    const char *nqn = dev->params.nqn ?
        dev->params.nqn : dev->parent_obj.id;

    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
             "nqn.2019-08.org.qemu:%s", nqn);
}

static void nvme_subsys_device_realize(DeviceState *dev, Error **errp)
{
    NvmeSubsystemDevice *subsys = NVME_SUBSYSTEM_DEVICE(dev);

    qbus_create_inplace(&subsys->bus, sizeof(NvmeBus), TYPE_NVME_BUS, dev,
                        dev->id);

    nvme_subsys_device_setup(subsys);
}

static Property nvme_subsys_device_props[] = {
    DEFINE_PROP_STRING("nqn", NvmeSubsystemDevice, params.nqn),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_subsys_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->realize = nvme_subsys_device_realize;
    dc->desc = "Virtual NVMe subsystem";
    dc->hotpluggable = false;

    device_class_set_props(dc, nvme_subsys_device_props);
}

static const TypeInfo nvme_subsys_device_info = {
    .name = TYPE_NVME_SUBSYSTEM_DEVICE,
    .parent = TYPE_DEVICE,
    .class_init = nvme_subsys_device_class_init,
    .instance_size = sizeof(NvmeSubsystemDevice),
};

static void register_types(void)
{
    type_register_static(&nvme_subsys_info);
    type_register_static(&nvme_subsys_device_info);
}

type_init(register_types)
