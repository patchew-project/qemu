#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "qapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/opts-visitor.h"
#include "qemu/help_option.h"

void user_creatable_complete(Object *obj, Error **errp)
{

    UserCreatableClass *ucc;
    UserCreatable *uc =
        (UserCreatable *)object_dynamic_cast(obj, TYPE_USER_CREATABLE);

    if (!uc) {
        return;
    }

    ucc = USER_CREATABLE_GET_CLASS(uc);
    if (ucc->complete) {
        ucc->complete(uc, errp);
    }
}

bool user_creatable_can_be_deleted(UserCreatable *uc, Error **errp)
{

    UserCreatableClass *ucc = USER_CREATABLE_GET_CLASS(uc);

    if (ucc->can_be_deleted) {
        return ucc->can_be_deleted(uc, errp);
    } else {
        return true;
    }
}


Object *user_creatable_add(const QDict *qdict,
                           Visitor *v, Error **errp)
{
    char *type = NULL;
    char *id = NULL;
    Object *obj = NULL;
    Error *local_err = NULL;
    QDict *pdict;

    pdict = qdict_clone_shallow(qdict);

    visit_start_struct(v, NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }

    qdict_del(pdict, "qom-type");
    visit_type_str(v, "qom-type", &type, &local_err);
    if (local_err) {
        goto out_visit;
    }

    qdict_del(pdict, "id");
    visit_type_str(v, "id", &id, &local_err);
    if (local_err) {
        goto out_visit;
    }
    visit_check_struct(v, &local_err);
    if (local_err) {
        goto out_visit;
    }

    obj = user_creatable_add_type(type, id, pdict, v, &local_err);

out_visit:
    visit_end_struct(v, NULL);

out:
    QDECREF(pdict);
    g_free(id);
    g_free(type);
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }
    return obj;
}


Object *user_creatable_add_type(const char *type, const char *id,
                                const QDict *qdict,
                                Visitor *v, Error **errp)
{
    Object *obj;
    ObjectClass *klass;
    const QDictEntry *e;
    Error *local_err = NULL;

    klass = object_class_by_name(type);
    if (!klass) {
        error_setg(errp, "invalid object type: %s", type);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_USER_CREATABLE)) {
        error_setg(errp, "object type '%s' isn't supported by object-add",
                   type);
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        error_setg(errp, "object type '%s' is abstract", type);
        return NULL;
    }

    assert(qdict);
    obj = object_new(type);
    visit_start_struct(v, NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }
    for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
        object_property_set(obj, v, e->key, &local_err);
        if (local_err) {
            break;
        }
    }
    if (!local_err) {
        visit_check_struct(v, &local_err);
    }
    visit_end_struct(v, NULL);
    if (local_err) {
        goto out;
    }

    object_property_add_child(object_get_objects_root(),
                              id, obj, &local_err);
    if (local_err) {
        goto out;
    }

    user_creatable_complete(obj, &local_err);
    if (local_err) {
        object_property_del(object_get_objects_root(),
                            id, &error_abort);
        goto out;
    }
out:
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }
    return obj;
}


Object *user_creatable_add_opts(QemuOpts *opts, Error **errp)
{
    Visitor *v;
    QDict *pdict;
    Object *obj = NULL;

    v = opts_visitor_new(opts);
    pdict = qemu_opts_to_qdict(opts, NULL);

    obj = user_creatable_add(pdict, v, errp);
    visit_free(v);
    QDECREF(pdict);
    return obj;
}


int user_creatable_add_opts_foreach(void *opaque, QemuOpts *opts, Error **errp)
{
    bool (*type_predicate)(const char *) = opaque;
    Object *obj = NULL;
    Error *err = NULL;
    const char *type;

    type = qemu_opt_get(opts, "qom-type");
    if (type && type_predicate &&
        !type_predicate(type)) {
        return 0;
    }

    obj = user_creatable_add_opts(opts, &err);
    if (!obj) {
        error_report_err(err);
        return -1;
    }
    object_unref(obj);
    return 0;
}


void user_creatable_del(const char *id, Error **errp)
{
    Object *container;
    Object *obj;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container, id);
    if (!obj) {
        error_setg(errp, "object '%s' not found", id);
        return;
    }

    if (!user_creatable_can_be_deleted(USER_CREATABLE(obj), errp)) {
        error_setg(errp, "object '%s' is in use, can not be deleted", id);
        return;
    }
    object_unparent(obj);
}

int user_creatable_help_func(void *opaque, QemuOpts *opts, Error **errp)
{
    char *type = NULL;
    Object *obj = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;

    type = qemu_opt_get(opts, "qom-type");
    if (type && is_help_option(type)) {
        GSList *list;
        printf("Available object backend types:\n");
        for (list = object_class_get_list(TYPE_USER_CREATABLE, false);  \
                list;                                                   \
                list = list->next) {
            const char *name;
            name = object_class_get_name(OBJECT_CLASS(list->data));
            printf("%s\n", name);
        }
        g_slist_free(list);
        goto out;
    }

    if (!type || !qemu_opt_has_help_opt(opts)) {
        return 0;
    }

    if (!object_class_by_name(type)) {
        printf("invalid object type: %s\n", type);
        goto out;
    }
    obj = object_new(type);
    object_property_iter_init(&iter, obj);

    while ((prop = object_property_iter_next(&iter))) {
        if (prop->description) {
            printf("%s (%s, %s)\n", prop->name, prop->type, prop->description);
        } else {
            printf("%s (%s)\n", prop->name, prop->type);
        }
    }

out:
    g_free(type);
    object_unref(obj);
    return 1;
}

static void register_types(void)
{
    static const TypeInfo uc_interface_info = {
        .name          = TYPE_USER_CREATABLE,
        .parent        = TYPE_INTERFACE,
        .abstract      = true,
        .class_size = sizeof(UserCreatableClass),
    };

    type_register_static(&uc_interface_info);
}

type_init(register_types)
