/*
 * QEMU IGVM interface
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@suse.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "sysemu/igvm-cfg.h"
#include "igvm.h"
#include "qom/object_interfaces.h"

static char *get_igvm(Object *obj, Error **errp)
{
    IgvmCfgState *igvm = IGVM_CFG(obj);
    return g_strdup(igvm->filename);
}

static void set_igvm(Object *obj, const char *value, Error **errp)
{
    IgvmCfgState *igvm = IGVM_CFG(obj);
    g_free(igvm->filename);
    igvm->filename = g_strdup(value);
}

static int igvm_process(IgvmCfgState *cfg, ConfidentialGuestSupport *cgs,
                        Error **errp)
{
    if (!cfg->filename) {
        return 0;
    }
    return igvm_process_file(cfg, cgs, errp);
}

static void igvm_cfg_class_init(ObjectClass *oc, void *data)
{
    IgvmCfgClass *igvmc = IGVM_CFG_CLASS(oc);

    object_class_property_add_str(oc, "file", get_igvm, set_igvm);
    object_class_property_set_description(oc, "file",
                                          "Set the IGVM filename to use");

    igvmc->process = igvm_process;
}

static const TypeInfo igvm_cfg_type = {
    .name = TYPE_IGVM_CFG,
    .parent = TYPE_OBJECT,
    .class_init = igvm_cfg_class_init,
    .class_size = sizeof(IgvmCfgClass),
    .instance_size = sizeof(IgvmCfgState),
    .interfaces = (InterfaceInfo[]){ { TYPE_USER_CREATABLE }, {} }
};

static void igvm_cfg_type_init(void)
{
    type_register_static(&igvm_cfg_type);
}

type_init(igvm_cfg_type_init);
