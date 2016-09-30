/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi-visit.h"
#include "qapi/dealloc-visitor.h"


#define TYPE_DUMMY "qemu-dummy"

typedef struct DummyObject DummyObject;
typedef struct DummyObjectClass DummyObjectClass;

typedef struct DummyPerson DummyPerson;
typedef struct DummyAddr DummyAddr;
typedef struct DummyAddrList DummyAddrList;
typedef struct DummySizeList DummySizeList;

#define DUMMY_OBJECT(obj)                               \
    OBJECT_CHECK(DummyObject, (obj), TYPE_DUMMY)

typedef enum DummyAnimal DummyAnimal;

enum DummyAnimal {
    DUMMY_FROG,
    DUMMY_ALLIGATOR,
    DUMMY_PLATYPUS,

    DUMMY_LAST,
};

static const char *const dummy_animal_map[DUMMY_LAST + 1] = {
    [DUMMY_FROG] = "frog",
    [DUMMY_ALLIGATOR] = "alligator",
    [DUMMY_PLATYPUS] = "platypus",
    [DUMMY_LAST] = NULL,
};


struct DummyAddr {
    char *ip;
    int64_t prefix;
    bool ipv6only;
};

struct DummyAddrList {
    DummyAddrList *next;
    struct DummyAddr *value;
};

struct DummyPerson {
    char *name;
    int64_t age;
};

struct DummyObject {
    Object parent_obj;

    bool bv;
    DummyAnimal av;
    char *sv;

    intList *sizes;

    DummyPerson *person;

    DummyAddrList *addrs;
};

struct DummyObjectClass {
    ObjectClass parent_class;
};


static void dummy_set_bv(Object *obj,
                         bool value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    dobj->bv = value;
}

static bool dummy_get_bv(Object *obj,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return dobj->bv;
}


static void dummy_set_av(Object *obj,
                         int value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    dobj->av = value;
}

static int dummy_get_av(Object *obj,
                        Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return dobj->av;
}


static void dummy_set_sv(Object *obj,
                         const char *value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    g_free(dobj->sv);
    dobj->sv = g_strdup(value);
}

static char *dummy_get_sv(Object *obj,
                          Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return g_strdup(dobj->sv);
}

static void visit_type_DummyPerson_fields(Visitor *v, DummyPerson **obj,
                                          Error **errp)
{
    Error *err = NULL;

    visit_type_str(v, "name", &(*obj)->name, &err);
    if (err) {
        goto out;
    }
    visit_type_int(v, "age", &(*obj)->age, &err);
    if (err) {
        goto out;
    }

out:
    error_propagate(errp, err);
}

static void visit_type_DummyPerson(Visitor *v, const char *name,
                                   DummyPerson **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(DummyPerson), &err);
    if (err) {
        goto out;
    }
    if (!*obj) {
        goto out_obj;
    }
    visit_type_DummyPerson_fields(v, obj, &err);
    error_propagate(errp, err);
    err = NULL;
out_obj:
    visit_end_struct(v, (void **)obj);
out:
    error_propagate(errp, err);
}

static void visit_type_DummyAddr_members(Visitor *v, DummyAddr **obj,
                                         Error **errp)
{
    Error *err = NULL;

    visit_type_str(v, "ip", &(*obj)->ip, &err);
    if (err) {
        goto out;
    }
    visit_type_int(v, "prefix", &(*obj)->prefix, &err);
    if (err) {
        goto out;
    }
    visit_type_bool(v, "ipv6only", &(*obj)->ipv6only, &err);
    if (err) {
        goto out;
    }

out:
    error_propagate(errp, err);
}

static void visit_type_DummyAddr(Visitor *v, const char *name,
                                 DummyAddr **obj, Error **errp)
{
    Error *err = NULL;

    visit_start_struct(v, name, (void **)obj, sizeof(DummyAddr), &err);
    if (err) {
        goto out;
    }
    if (!*obj) {
        goto out_obj;
    }
    visit_type_DummyAddr_members(v, obj, &err);
    error_propagate(errp, err);
    err = NULL;
out_obj:
    visit_end_struct(v, (void **)obj);
out:
    error_propagate(errp, err);
}

static void qapi_free_DummyAddrList(DummyAddrList *obj);

static void visit_type_DummyAddrList(Visitor *v, const char *name,
                                     DummyAddrList **obj, Error **errp)
{
    Error *err = NULL;
    DummyAddrList *tail;
    size_t size = sizeof(**obj);

    visit_start_list(v, name, (GenericList **)obj, size, &err);
    if (err) {
        goto out;
    }

    for (tail = *obj; tail;
         tail = (DummyAddrList *)visit_next_list(v,
                                                 (GenericList *)tail,
                                                 size)) {
        visit_type_DummyAddr(v, NULL, &tail->value, &err);
        if (err) {
            break;
        }
    }

    visit_end_list(v, (void **)obj);
    if (err && visit_is_input(v)) {
        qapi_free_DummyAddrList(*obj);
        *obj = NULL;
    }
out:
    error_propagate(errp, err);
}

static void qapi_free_DummyAddrList(DummyAddrList *obj)
{
    Visitor *v;

    if (!obj) {
        return;
    }

    v = qapi_dealloc_visitor_new();
    visit_type_DummyAddrList(v, NULL, &obj, NULL);
    visit_free(v);
}

static void dummy_set_sizes(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    visit_type_intList(v, name, &dobj->sizes, errp);
}

static void dummy_set_person(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    visit_type_DummyPerson(v, name, &dobj->person, errp);
}

static void dummy_set_addrs(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    visit_type_DummyAddrList(v, name, &dobj->addrs, errp);
}

static void dummy_init(Object *obj)
{
    object_property_add_bool(obj, "bv",
                             dummy_get_bv,
                             dummy_set_bv,
                             NULL);
}

static void
dummy_complete(UserCreatable *uc, Error **errp)
{
}

static void dummy_class_init(ObjectClass *cls, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(cls);
    ucc->complete = dummy_complete;

    object_class_property_add_bool(cls, "bv",
                                   dummy_get_bv,
                                   dummy_set_bv,
                                   NULL);
    object_class_property_add_str(cls, "sv",
                                  dummy_get_sv,
                                  dummy_set_sv,
                                  NULL);
    object_class_property_add_enum(cls, "av",
                                   "DummyAnimal",
                                   dummy_animal_map,
                                   dummy_get_av,
                                   dummy_set_av,
                                   NULL);
    object_class_property_add(cls, "sizes",
                              "int[]",
                              NULL,
                              dummy_set_sizes,
                              NULL, NULL, NULL);
    object_class_property_add(cls, "person",
                              "DummyPerson",
                              NULL,
                              dummy_set_person,
                              NULL, NULL, NULL);
    object_class_property_add(cls, "addrs",
                              "DummyAddrList",
                              NULL,
                              dummy_set_addrs,
                              NULL, NULL, NULL);
}


static void dummy_finalize(Object *obj)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);
    Visitor *v;

    v = qapi_dealloc_visitor_new();
    visit_type_intList(v, NULL, &dobj->sizes, NULL);
    visit_type_DummyAddrList(v, NULL, &dobj->addrs, NULL);
    visit_type_DummyPerson(v, NULL, &dobj->person, NULL);
    visit_free(v);

    g_free(dobj->sv);
}


static const TypeInfo dummy_info = {
    .name          = TYPE_DUMMY,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyObject),
    .instance_init = dummy_init,
    .instance_finalize = dummy_finalize,
    .class_size = sizeof(DummyObjectClass),
    .class_init = dummy_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


/*
 * The following 3 object classes are used to
 * simulate the kind of relationships seen in
 * qdev, which result in complex object
 * property destruction ordering.
 *
 * DummyDev has a 'bus' child to a DummyBus
 * DummyBus has a 'backend' child to a DummyBackend
 * DummyDev has a 'backend' link to DummyBackend
 *
 * When DummyDev is finalized, it unparents the
 * DummyBackend, which unparents the DummyDev
 * which deletes the 'backend' link from DummyDev
 * to DummyBackend. This illustrates that the
 * object_property_del_all() method needs to
 * cope with the list of properties being changed
 * while it iterates over them.
 */
typedef struct DummyDev DummyDev;
typedef struct DummyDevClass DummyDevClass;
typedef struct DummyBus DummyBus;
typedef struct DummyBusClass DummyBusClass;
typedef struct DummyBackend DummyBackend;
typedef struct DummyBackendClass DummyBackendClass;

#define TYPE_DUMMY_DEV "qemu-dummy-dev"
#define TYPE_DUMMY_BUS "qemu-dummy-bus"
#define TYPE_DUMMY_BACKEND "qemu-dummy-backend"

#define DUMMY_DEV(obj)                               \
    OBJECT_CHECK(DummyDev, (obj), TYPE_DUMMY_DEV)
#define DUMMY_BUS(obj)                               \
    OBJECT_CHECK(DummyBus, (obj), TYPE_DUMMY_BUS)
#define DUMMY_BACKEND(obj)                               \
    OBJECT_CHECK(DummyBackend, (obj), TYPE_DUMMY_BACKEND)

struct DummyDev {
    Object parent_obj;

    DummyBus *bus;
};

struct DummyDevClass {
    ObjectClass parent_class;
};

struct DummyBus {
    Object parent_obj;

    DummyBackend *backend;
};

struct DummyBusClass {
    ObjectClass parent_class;
};

struct DummyBackend {
    Object parent_obj;
};

struct DummyBackendClass {
    ObjectClass parent_class;
};


static void dummy_dev_finalize(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);

    object_unref(OBJECT(dev->bus));
}

static void dummy_dev_init(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);
    DummyBus *bus = DUMMY_BUS(object_new(TYPE_DUMMY_BUS));
    DummyBackend *backend = DUMMY_BACKEND(object_new(TYPE_DUMMY_BACKEND));

    object_property_add_child(obj, "bus", OBJECT(bus), NULL);
    dev->bus = bus;
    object_property_add_child(OBJECT(bus), "backend", OBJECT(backend), NULL);
    bus->backend = backend;

    object_property_add_link(obj, "backend", TYPE_DUMMY_BACKEND,
                             (Object **)&bus->backend, NULL, 0, NULL);
}

static void dummy_dev_unparent(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);
    object_unparent(OBJECT(dev->bus));
}

static void dummy_dev_class_init(ObjectClass *klass, void *opaque)
{
    klass->unparent = dummy_dev_unparent;
}


static void dummy_bus_finalize(Object *obj)
{
    DummyBus *bus = DUMMY_BUS(obj);

    object_unref(OBJECT(bus->backend));
}

static void dummy_bus_init(Object *obj)
{
}

static void dummy_bus_unparent(Object *obj)
{
    DummyBus *bus = DUMMY_BUS(obj);
    object_property_del(obj->parent, "backend", NULL);
    object_unparent(OBJECT(bus->backend));
}

static void dummy_bus_class_init(ObjectClass *klass, void *opaque)
{
    klass->unparent = dummy_bus_unparent;
}

static void dummy_backend_init(Object *obj)
{
}


static const TypeInfo dummy_dev_info = {
    .name          = TYPE_DUMMY_DEV,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyDev),
    .instance_init = dummy_dev_init,
    .instance_finalize = dummy_dev_finalize,
    .class_size = sizeof(DummyDevClass),
    .class_init = dummy_dev_class_init,
};

static const TypeInfo dummy_bus_info = {
    .name          = TYPE_DUMMY_BUS,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyBus),
    .instance_init = dummy_bus_init,
    .instance_finalize = dummy_bus_finalize,
    .class_size = sizeof(DummyBusClass),
    .class_init = dummy_bus_class_init,
};

static const TypeInfo dummy_backend_info = {
    .name          = TYPE_DUMMY_BACKEND,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyBackend),
    .instance_init = dummy_backend_init,
    .class_size = sizeof(DummyBackendClass),
};



static void test_dummy_createv(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &err,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "platypus",
                              NULL));

    g_assert(err == NULL);
    g_assert_cmpstr(dobj->sv, ==, "Hiss hiss hiss");
    g_assert(dobj->bv == true);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    g_assert(object_resolve_path_component(parent, "dummy0")
             == OBJECT(dobj));

    object_unparent(OBJECT(dobj));
}


static Object *new_helper(Error **errp,
                          Object *parent,
                          ...)
{
    va_list vargs;
    Object *obj;

    va_start(vargs, parent);
    obj = object_new_with_propv(TYPE_DUMMY,
                                parent,
                                "dummy0",
                                errp,
                                vargs);
    va_end(vargs);
    return obj;
}

static void test_dummy_createlist(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        new_helper(&err,
                   parent,
                   "bv", "yes",
                   "sv", "Hiss hiss hiss",
                   "av", "platypus",
                   NULL));

    g_assert(err == NULL);
    g_assert_cmpstr(dobj->sv, ==, "Hiss hiss hiss");
    g_assert(dobj->bv == true);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    g_assert(object_resolve_path_component(parent, "dummy0")
             == OBJECT(dobj));

    object_unparent(OBJECT(dobj));
}


static QemuOptsList dummy_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(dummy_opts.head),
    .desc = {
        { }
    },
};

static void test_dummy_create_complex(DummyObject *dummy)
{
    g_assert(dummy->person != NULL);
    g_assert_cmpstr(dummy->person->name, ==, "fred");
    g_assert_cmpint(dummy->person->age, ==, 52);

    g_assert(dummy->sizes != NULL);
    g_assert_cmpint(dummy->sizes->value, ==, 12);
    g_assert_cmpint(dummy->sizes->next->value, ==, 13);
    g_assert_cmpint(dummy->sizes->next->next->value, ==, 8139);

    g_assert(dummy->addrs != NULL);
    g_assert_cmpstr(dummy->addrs->value->ip, ==, "127.0.0.1");
    g_assert_cmpint(dummy->addrs->value->prefix, ==, 24);
    g_assert(dummy->addrs->value->ipv6only);
    g_assert_cmpstr(dummy->addrs->next->value->ip, ==, "0.0.0.0");
    g_assert_cmpint(dummy->addrs->next->value->prefix, ==, 16);
    g_assert(!dummy->addrs->next->value->ipv6only);
}


static void _test_dummy_createopts(const char *optstr)
{
    QemuOpts *opts;
    DummyObject *dummy;

    opts = qemu_opts_parse_noisily(&dummy_opts,
                                   optstr, true);
    g_assert(opts != NULL);

    dummy = DUMMY_OBJECT(user_creatable_add_opts(opts, &error_abort));

    test_dummy_create_complex(dummy);

    object_unparent(OBJECT(dummy));
    object_unref(OBJECT(dummy));
    qemu_opts_reset(&dummy_opts);
}


static void test_dummy_createopts(void)
{
    const char *optstr = "qemu-dummy,id=dummy0,bv=yes,av=alligator,sv=hiss,"
        "person.name=fred,person.age=52,sizes.0=12,sizes.1=13,sizes.2=8139,"
        "addrs.0.ip=127.0.0.1,addrs.0.prefix=24,addrs.0.ipv6only=yes,"
        "addrs.1.ip=0.0.0.0,addrs.1.prefix=16,addrs.1.ipv6only=no";
    _test_dummy_createopts(optstr);
}

static void test_dummy_createopts_repeat(void)
{
    const char *optstr = "qemu-dummy,id=dummy0,bv=yes,av=alligator,sv=hiss,"
        "person.name=fred,person.age=52,sizes=12,sizes=13,sizes=8139,"
        "addrs.0.ip=127.0.0.1,addrs.0.prefix=24,addrs.0.ipv6only=yes,"
        "addrs.1.ip=0.0.0.0,addrs.1.prefix=16,addrs.1.ipv6only=no";
    _test_dummy_createopts(optstr);
}

static void test_dummy_createopts_range(void)
{
    const char *optstr = "qemu-dummy,id=dummy0,bv=yes,av=alligator,sv=hiss,"
        "person.name=fred,person.age=52,sizes=12-13,sizes=8139,"
        "addrs.0.ip=127.0.0.1,addrs.0.prefix=24,addrs.0.ipv6only=yes,"
        "addrs.1.ip=0.0.0.0,addrs.1.prefix=16,addrs.1.ipv6only=no";
    _test_dummy_createopts(optstr);
}


static void test_dummy_createopts_bad(void)
{
    /* Something that tries to create a QList at the top level
     * should be invalid. */
    const char *optstr = "qemu-dummy,id=dummy0,1=foo,2=bar,3=wizz";
    QemuOpts *opts;
    DummyObject *dummy;
    Error *err = NULL;

    opts = qemu_opts_parse_noisily(&dummy_opts,
                                   optstr, true);
    g_assert(opts != NULL);

    dummy = DUMMY_OBJECT(user_creatable_add_opts(opts, &err));
    error_free_or_abort(&err);
    g_assert(!dummy);
    qemu_opts_reset(&dummy_opts);
}


static void test_dummy_createqmp(void)
{
    const char *jsonstr =
        "{ 'bv': true, 'av': 'alligator', 'sv': 'hiss', "
        "  'person': { 'name': 'fred', 'age': 52 }, "
        "  'sizes': [12, 13, 8139], "
        "  'addrs': [ { 'ip': '127.0.0.1', 'prefix': 24, 'ipv6only': true }, "
        "             { 'ip': '0.0.0.0', 'prefix': 16, 'ipv6only': false } ] }";
    QObject *obj = qobject_from_json(jsonstr);
    Visitor *v = qobject_input_visitor_new(obj, true);
    DummyObject *dummy;
    g_assert(obj);
    dummy = DUMMY_OBJECT(user_creatable_add_type("qemu-dummy", "dummy0",
                                                 qobject_to_qdict(obj), v,
                                                 &error_abort));

    test_dummy_create_complex(dummy);
    visit_free(v);
    object_unparent(OBJECT(dummy));
    object_unref(OBJECT(dummy));
    qobject_decref(obj);
}


static void test_dummy_badenum(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    Object *dobj =
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &err,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "yeti",
                              NULL);

    g_assert(dobj == NULL);
    g_assert(err != NULL);
    g_assert_cmpstr(error_get_pretty(err), ==,
                    "Invalid parameter 'yeti'");

    g_assert(object_resolve_path_component(parent, "dummy0")
             == NULL);

    error_free(err);
}


static void test_dummy_getenum(void)
{
    Error *err = NULL;
    int val;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                         parent,
                         "dummy0",
                         &err,
                         "av", "platypus",
                         NULL));

    g_assert(err == NULL);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    val = object_property_get_enum(OBJECT(dobj),
                                   "av",
                                   "DummyAnimal",
                                   &err);
    g_assert(err == NULL);
    g_assert(val == DUMMY_PLATYPUS);

    /* A bad enum type name */
    val = object_property_get_enum(OBJECT(dobj),
                                   "av",
                                   "BadAnimal",
                                   &err);
    g_assert(err != NULL);
    error_free(err);
    err = NULL;

    /* A non-enum property name */
    val = object_property_get_enum(OBJECT(dobj),
                                   "iv",
                                   "DummyAnimal",
                                   &err);
    g_assert(err != NULL);
    error_free(err);

    object_unparent(OBJECT(dobj));
}


static void test_dummy_iterator(void)
{
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &error_abort,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "platypus",
                              NULL));

    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    bool seenbv = false, seensv = false, seenav = false,
        seentype = false, seenaddrs = false, seenperson = false,
        seensizes = false;

    object_property_iter_init(&iter, OBJECT(dobj));
    while ((prop = object_property_iter_next(&iter))) {
        if (g_str_equal(prop->name, "bv")) {
            seenbv = true;
        } else if (g_str_equal(prop->name, "sv")) {
            seensv = true;
        } else if (g_str_equal(prop->name, "av")) {
            seenav = true;
        } else if (g_str_equal(prop->name, "type")) {
            /* This prop comes from the base Object class */
            seentype = true;
        } else if (g_str_equal(prop->name, "addrs")) {
            seenaddrs = true;
        } else if (g_str_equal(prop->name, "person")) {
            seenperson = true;
        } else if (g_str_equal(prop->name, "sizes")) {
            seensizes = true;
        } else {
            g_printerr("Found prop '%s'\n", prop->name);
            g_assert_not_reached();
        }
    }
    g_assert(seenbv);
    g_assert(seenav);
    g_assert(seensv);
    g_assert(seentype);
    g_assert(seenaddrs);
    g_assert(seenperson);
    g_assert(seensizes);

    object_unparent(OBJECT(dobj));
}


static void test_dummy_delchild(void)
{
    Object *parent = object_get_objects_root();
    DummyDev *dev = DUMMY_DEV(
        object_new_with_props(TYPE_DUMMY_DEV,
                              parent,
                              "dev0",
                              &error_abort,
                              NULL));

    object_unparent(OBJECT(dev));
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    qemu_add_opts(&dummy_opts);

    type_register_static(&dummy_info);
    type_register_static(&dummy_dev_info);
    type_register_static(&dummy_bus_info);
    type_register_static(&dummy_backend_info);

    g_test_add_func("/qom/proplist/createlist", test_dummy_createlist);
    g_test_add_func("/qom/proplist/createv", test_dummy_createv);
    g_test_add_func("/qom/proplist/createopts", test_dummy_createopts);
    g_test_add_func("/qom/proplist/createoptsrepeat",
                    test_dummy_createopts_repeat);
    g_test_add_func("/qom/proplist/createoptsrange",
                    test_dummy_createopts_range);
    g_test_add_func("/qom/proplist/createoptsbad", test_dummy_createopts_bad);
    g_test_add_func("/qom/proplist/createqmp", test_dummy_createqmp);
    g_test_add_func("/qom/proplist/badenum", test_dummy_badenum);
    g_test_add_func("/qom/proplist/getenum", test_dummy_getenum);
    g_test_add_func("/qom/proplist/iterator", test_dummy_iterator);
    g_test_add_func("/qom/proplist/delchild", test_dummy_delchild);

    return g_test_run();
}
