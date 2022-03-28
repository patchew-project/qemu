/*
 * QMP commands related to QOM
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "block/qdict.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"
#include "qom/qom-qobject.h"
#include "hw/boards.h"

ObjectPropertyInfoList *qmp_qom_list(const char *path, Error **errp)
{
    Object *obj;
    bool ambiguous = false;
    ObjectPropertyInfoList *props = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;

    obj = object_resolve_path(path, &ambiguous);
    if (obj == NULL) {
        if (ambiguous) {
            error_setg(errp, "Path '%s' is ambiguous", path);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", path);
        }
        return NULL;
    }

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *value = g_new0(ObjectPropertyInfo, 1);

        QAPI_LIST_PREPEND(props, value);

        value->name = g_strdup(prop->name);
        value->type = g_strdup(prop->type);
    }

    return props;
}

void qmp_qom_set(const char *path, const char *property, QObject *value,
                 Error **errp)
{
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
        return;
    }

    object_property_set_qobject(obj, property, value, errp);
}

QObject *qmp_qom_get(const char *path, const char *property, Error **errp)
{
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
        return NULL;
    }

    return object_property_get_qobject(obj, property, errp);
}

static void qom_list_types_tramp(ObjectClass *klass, void *data)
{
    ObjectTypeInfoList **pret = data;
    ObjectTypeInfo *info;
    ObjectClass *parent = object_class_get_parent(klass);

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(object_class_get_name(klass));
    info->has_abstract = info->abstract = object_class_is_abstract(klass);
    if (parent) {
        info->has_parent = true;
        info->parent = g_strdup(object_class_get_name(parent));
    }

    QAPI_LIST_PREPEND(*pret, info);
}

ObjectTypeInfoList *qmp_qom_list_types(bool has_implements,
                                       const char *implements,
                                       bool has_abstract,
                                       bool abstract,
                                       Error **errp)
{
    ObjectTypeInfoList *ret = NULL;

    module_load_qom_all();
    object_class_foreach(qom_list_types_tramp, implements, abstract, &ret);

    return ret;
}

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                Error **errp)
{
    ObjectClass *klass;
    Object *obj;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyInfoList *prop_list = NULL;

    klass = module_object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", typename);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_DEVICE)
        || object_class_is_abstract(klass)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename",
                   "a non-abstract device type");
        return NULL;
    }

    obj = object_new(typename);

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *info;

        /* Skip Object and DeviceState properties */
        if (strcmp(prop->name, "type") == 0 ||
            strcmp(prop->name, "realized") == 0 ||
            strcmp(prop->name, "hotpluggable") == 0 ||
            strcmp(prop->name, "hotplugged") == 0 ||
            strcmp(prop->name, "parent_bus") == 0) {
            continue;
        }

        /* Skip legacy properties since they are just string versions of
         * properties that we already list.
         */
        if (strstart(prop->name, "legacy-", NULL)) {
            continue;
        }

        info = g_new0(ObjectPropertyInfo, 1);
        info->name = g_strdup(prop->name);
        info->type = g_strdup(prop->type);
        info->has_description = !!prop->description;
        info->description = g_strdup(prop->description);
        info->default_value = qobject_ref(prop->defval);
        info->has_default_value = !!info->default_value;

        QAPI_LIST_PREPEND(prop_list, info);
    }

    object_unref(obj);

    return prop_list;
}

ObjectPropertyInfoList *qmp_qom_list_properties(const char *typename,
                                             Error **errp)
{
    ObjectClass *klass;
    Object *obj = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyInfoList *prop_list = NULL;

    klass = object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Class '%s' not found", typename);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_OBJECT)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename",
                   "a QOM type");
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        object_class_property_iter_init(&iter, klass);
    } else {
        obj = object_new(typename);
        object_property_iter_init(&iter, obj);
    }
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *info;

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(prop->name);
        info->type = g_strdup(prop->type);
        info->has_description = !!prop->description;
        info->description = g_strdup(prop->description);

        QAPI_LIST_PREPEND(prop_list, info);
    }

    object_unref(obj);

    return prop_list;
}

void qmp_object_add(ObjectOptions *options, Error **errp)
{
    user_creatable_add_qapi(options, errp);
}

void qmp_object_del(const char *id, Error **errp)
{
    user_creatable_del(id, errp);
}

static void query_object_prop(InitValueList **props_list, ObjectProperty *prop,
                              Object *obj, Error **errp)
{
    InitValue *prop_info = NULL;

    /* Skip inconsiderable properties */
    if (strcmp(prop->name, "type") == 0 ||
        strcmp(prop->name, "realized") == 0 ||
        strcmp(prop->name, "hotpluggable") == 0 ||
        strcmp(prop->name, "hotplugged") == 0 ||
        strcmp(prop->name, "parent_bus") == 0) {
        return;
    }

    prop_info = g_malloc0(sizeof(*prop_info));
    prop_info->name = g_strdup(prop->name);
    prop_info->value = NULL;
    if (prop->defval) {
        prop_info->value = qobject_ref(prop->defval);
    } else if (prop->get) {
        /*
         * crash-information in x86-cpu uses errp to return current state.
         * So, after requesting this property it returns  GenericError:
         * "No crash occured"
         */
        if (strcmp(prop->name, "crash-information") != 0) {
            prop_info->value = object_property_get_qobject(obj, prop->name,
                                                           errp);
        }
    }
    prop_info->has_value = !!prop_info->value;

    QAPI_LIST_PREPEND(*props_list, prop_info);
}

typedef struct QIPData {
    InitPropsList **dev_list;
    Error **errp;
} QIPData;

static void query_init_properties_tramp(gpointer list_data, gpointer opaque)
{
    ObjectClass *k = list_data;
    Object *obj;
    ObjectClass *parent;
    GHashTableIter iter;

    QIPData *data = opaque;
    ClassPropertiesList *class_props_list = NULL;
    InitProps *dev_info;

    /* Only one machine can be initialized correctly (it's already happened) */
    if (object_class_dynamic_cast(k, TYPE_MACHINE)) {
        return;
    }

    const char *klass_name = object_class_get_name(k);
    /*
     * Uses machine type infrastructure with notifiers. It causes immediate
     * notify and SEGSEGV during remote_object_machine_done
     */
    if (strcmp(klass_name, "x-remote-object") == 0) {
        return;
    }

    dev_info = g_malloc0(sizeof(*dev_info));
    dev_info->name = g_strdup(klass_name);

    obj = object_new_with_class(k);

    /*
     * Part of ObjectPropertyIterator infrastructure, but we need more precise
     * control of current class to dump appropriate features
     * This part was taken out from loop because first initialization differ
     * from other reinitializations
     */
    parent = object_get_class(obj);
    g_hash_table_iter_init(&iter, obj->properties);
    const char *prop_owner_name = object_get_typename(obj);
    do {
        InitValueList *prop_list = NULL;
        ClassProperties *class_data;

        gpointer key, val;
        while (g_hash_table_iter_next(&iter, &key, &val)) {
            query_object_prop(&prop_list, (ObjectProperty *)val, obj,
                              data->errp);
        }
        class_data = g_malloc0(sizeof(*class_data));
        class_data->classname = g_strdup(prop_owner_name);
        class_data->classprops = prop_list;
        class_data->has_classprops = !!prop_list;

        QAPI_LIST_PREPEND(class_props_list, class_data);

        if (!parent) {
            break;
        }
        g_hash_table_iter_init(&iter, parent->properties);
        prop_owner_name = object_class_get_name(parent);
        parent = object_class_get_parent(parent);
    } while (true);
    dev_info->props = class_props_list;
    object_unref(OBJECT(obj));

    QAPI_LIST_PREPEND(*(data->dev_list), dev_info);
}

InitPropsList *qmp_query_init_properties(Error **errp)
{
    GSList *typename_list = object_class_get_list(TYPE_OBJECT, false);

    InitPropsList *dev_list = NULL;
    QIPData data = { &dev_list, errp };
    g_slist_foreach(typename_list, query_init_properties_tramp, &data);
    g_slist_free(typename_list);

    return dev_list;
}
