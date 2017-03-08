/*
 * QEMU security policy support
 *
 * Copyright (c) 2016 Advanced Micro Devices
 *
 * Author:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/base64.h"

#include "sysemu/security-policy.h"

static SecurityPolicy *
find_security_policy_obj(const char *name)
{
    Object *obj;
    SecurityPolicy *policy;

    if (!name) {
        return NULL;
    }

    obj = object_resolve_path_component(
        object_get_objects_root(), name);
    if (!obj) {
        return NULL;
    }

    policy = (SecurityPolicy *)
        object_dynamic_cast(obj,
                            TYPE_SECURITY_POLICY);
    if (!policy) {
        return NULL;
    }

    return policy;
}

bool
security_policy_debug_allowed(const char *secure_policy_id)
{
    SecurityPolicy *policy = find_security_policy_obj(secure_policy_id);

    /* if id is not a valid security policy then we return true */
    return policy ? policy->debug : true;
}

char *
security_policy_get_memory_encryption_id(const char *secure_policy_id)
{
    SecurityPolicy *policy = find_security_policy_obj(secure_policy_id);

    return policy ? g_strdup(policy->memory_encryption) : NULL;
}

static bool
security_policy_prop_get_debug(Object *obj,
                               Error **errp G_GNUC_UNUSED)
{
    SecurityPolicy *policy = SECURITY_POLICY(obj);

    return policy->debug;
}


static void
security_policy_prop_set_debug(Object *obj,
                               bool value,
                               Error **errp G_GNUC_UNUSED)
{
    SecurityPolicy *policy = SECURITY_POLICY(obj);

    policy->debug = value;
}

static char *
sev_launch_get_memory_encryption(Object *obj, Error **errp)
{
    SecurityPolicy *policy = SECURITY_POLICY(obj);

    return g_strdup(policy->memory_encryption);
}

static void
sev_launch_set_memory_encryption(Object *obj, const char *value,
                                 Error **errp)
{
    SecurityPolicy *policy = SECURITY_POLICY(obj);

    policy->memory_encryption = g_strdup(value);
}

static void
security_policy_init(Object *obj)
{
    SecurityPolicy *policy = SECURITY_POLICY(obj);

    policy->debug = true;
}

static void
security_policy_finalize(Object *obj)
{
}

static void
security_policy_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_bool(oc, "debug",
                                   security_policy_prop_get_debug,
                                   security_policy_prop_set_debug,
                                   NULL);
    object_class_property_set_description(oc, "debug",
            "Set on/off if debugging is allowed on this guest (default on)",
            NULL);
    object_class_property_add_str(oc, "memory-encryption",
                                  sev_launch_get_memory_encryption,
                                  sev_launch_set_memory_encryption,
                                  NULL);
    object_class_property_set_description(oc, "memory-encryption",
            "Set memory encryption object id (if supported by hardware)",
            NULL);
}

static const TypeInfo security_policy_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_SECURITY_POLICY,
    .instance_size = sizeof(SecurityPolicy),
    .instance_init = security_policy_init,
    .instance_finalize = security_policy_finalize,
    .class_size = sizeof(SecurityPolicyClass),
    .class_init = security_policy_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
security_policy_register_types(void)
{
    type_register_static(&security_policy_info);
}


type_init(security_policy_register_types);
