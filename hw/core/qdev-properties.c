#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qdev-prop-internal.h"

void qdev_prop_set_after_realize(DeviceState *dev, const char *name,
                                  Error **errp)
{
    if (dev->id) {
        error_setg(errp, "Attempt to set property '%s' on device '%s' "
                   "(type '%s') after it was realized", name, dev->id,
                   object_get_typename(OBJECT(dev)));
    } else {
        error_setg(errp, "Attempt to set property '%s' on anonymous device "
                   "(type '%s') after it was realized", name,
                   object_get_typename(OBJECT(dev)));
    }
}

/* returns: true if property is allowed to be set, false otherwise */
static bool qdev_prop_allow_set(Object *obj, ObjectProperty *op,
                                Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, op->name, errp);
        return false;
    }
    return true;
}

void qdev_prop_allow_set_link_before_realize(const Object *obj,
                                             const char *name,
                                             Object *val, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized) {
        error_setg(errp, "Attempt to set link property '%s' on device '%s' "
                   "(type '%s') after it was realized",
                   name, dev->id, object_get_typename(obj));
    }
}

void *object_field_prop_ptr(Object *obj, Property *prop)
{
    void *ptr = obj;
    ptr += prop->offset;
    return ptr;
}

static void field_prop_get(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;
    return prop->info->get(obj, v, name, opaque, errp);
}

/**
 * field_prop_getter: Return getter function to be used for property
 *
 * Return value can be NULL if @info has no getter function.
 */
static ObjectPropertyAccessor *field_prop_getter(const PropertyInfo *info)
{
    return info->get ? field_prop_get : NULL;
}

static void field_prop_set(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;

    return prop->info->set(obj, v, name, opaque, errp);
}

/**
 * field_prop_setter: Return setter function to be used for property
 *
 * Return value can be NULL if @info has not setter function.
 */
static ObjectPropertyAccessor *field_prop_setter(const PropertyInfo *info)
{
    return info->set ? field_prop_set : NULL;
}

void field_prop_get_enum(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    int *ptr = object_field_prop_ptr(obj, prop);

    visit_type_enum(v, name, ptr, prop->info->enum_table, errp);
}

void field_prop_set_enum(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    int *ptr = object_field_prop_ptr(obj, prop);

    visit_type_enum(v, name, ptr, prop->info->enum_table, errp);
}

void field_prop_set_default_value_enum(ObjectProperty *op,
                                       const Property *prop)
{
    object_property_set_default_str(op,
        qapi_enum_lookup(prop->info->enum_table, prop->defval.i));
}

const PropertyInfo prop_info_enum = {
    .name  = "enum",
    .get   = field_prop_get_enum,
    .set   = field_prop_set_enum,
    .set_default_value = field_prop_set_default_value_enum,
};

/* Bit */

static uint32_t qdev_get_prop_mask(Property *prop)
{
    assert(prop->info == &prop_info_bit);
    return 0x1 << prop->bitnr;
}

static void bit_prop_set(Object *obj, Property *props, bool val)
{
    uint32_t *p = object_field_prop_ptr(obj, props);
    uint32_t mask = qdev_get_prop_mask(props);
    if (val) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static void prop_get_bit(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *p = object_field_prop_ptr(obj, prop);
    bool value = (*p & qdev_get_prop_mask(prop)) != 0;

    visit_type_bool(v, name, &value, errp);
}

static void prop_set_bit(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }
    bit_prop_set(obj, prop, value);
}

static void set_default_value_bool(ObjectProperty *op, const Property *prop)
{
    object_property_set_default_bool(op, prop->defval.u);
}

const PropertyInfo prop_info_bit = {
    .name  = "bool",
    .description = "on/off",
    .get   = prop_get_bit,
    .set   = prop_set_bit,
    .set_default_value = set_default_value_bool,
};

/* Bit64 */

static uint64_t qdev_get_prop_mask64(Property *prop)
{
    assert(prop->info == &prop_info_bit64);
    return 0x1ull << prop->bitnr;
}

static void bit64_prop_set(Object *obj, Property *props, bool val)
{
    uint64_t *p = object_field_prop_ptr(obj, props);
    uint64_t mask = qdev_get_prop_mask64(props);
    if (val) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static void prop_get_bit64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint64_t *p = object_field_prop_ptr(obj, prop);
    bool value = (*p & qdev_get_prop_mask64(prop)) != 0;

    visit_type_bool(v, name, &value, errp);
}

static void prop_set_bit64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }
    bit64_prop_set(obj, prop, value);
}

const PropertyInfo prop_info_bit64 = {
    .name  = "bool",
    .description = "on/off",
    .get   = prop_get_bit64,
    .set   = prop_set_bit64,
    .set_default_value = set_default_value_bool,
};

/* --- bool --- */

static void get_bool(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    Property *prop = opaque;
    bool *ptr = object_field_prop_ptr(obj, prop);

    visit_type_bool(v, name, ptr, errp);
}

static void set_bool(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    Property *prop = opaque;
    bool *ptr = object_field_prop_ptr(obj, prop);

    visit_type_bool(v, name, ptr, errp);
}

const PropertyInfo prop_info_bool = {
    .name  = "bool",
    .get   = get_bool,
    .set   = set_bool,
    .set_default_value = set_default_value_bool,
};

/* --- 8bit integer --- */

static void get_uint8(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    Property *prop = opaque;
    uint8_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint8(v, name, ptr, errp);
}

static void set_uint8(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    Property *prop = opaque;
    uint8_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint8(v, name, ptr, errp);
}

void field_prop_set_default_value_int(ObjectProperty *op,
                                      const Property *prop)
{
    object_property_set_default_int(op, prop->defval.i);
}

void field_prop_set_default_value_uint(ObjectProperty *op,
                                       const Property *prop)
{
    object_property_set_default_uint(op, prop->defval.u);
}

const PropertyInfo prop_info_uint8 = {
    .name  = "uint8",
    .get   = get_uint8,
    .set   = set_uint8,
    .set_default_value = field_prop_set_default_value_uint,
};

/* --- 16bit integer --- */

static void get_uint16(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint16_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint16(v, name, ptr, errp);
}

static void set_uint16(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint16_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint16(v, name, ptr, errp);
}

const PropertyInfo prop_info_uint16 = {
    .name  = "uint16",
    .get   = get_uint16,
    .set   = set_uint16,
    .set_default_value = field_prop_set_default_value_uint,
};

/* --- 32bit integer --- */

static void get_uint32(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint32(v, name, ptr, errp);
}

static void set_uint32(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint32(v, name, ptr, errp);
}

void field_prop_get_int32(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    Property *prop = opaque;
    int32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int32(v, name, ptr, errp);
}

static void set_int32(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    Property *prop = opaque;
    int32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int32(v, name, ptr, errp);
}

const PropertyInfo prop_info_uint32 = {
    .name  = "uint32",
    .get   = get_uint32,
    .set   = set_uint32,
    .set_default_value = field_prop_set_default_value_uint,
};

const PropertyInfo prop_info_int32 = {
    .name  = "int32",
    .get   = field_prop_get_int32,
    .set   = set_int32,
    .set_default_value = field_prop_set_default_value_int,
};

/* --- 64bit integer --- */

static void get_uint64(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint64(v, name, ptr, errp);
}

static void set_uint64(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint64(v, name, ptr, errp);
}

static void get_int64(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    Property *prop = opaque;
    int64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int64(v, name, ptr, errp);
}

static void set_int64(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    Property *prop = opaque;
    int64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int64(v, name, ptr, errp);
}

const PropertyInfo prop_info_uint64 = {
    .name  = "uint64",
    .get   = get_uint64,
    .set   = set_uint64,
    .set_default_value = field_prop_set_default_value_uint,
};

const PropertyInfo prop_info_int64 = {
    .name  = "int64",
    .get   = get_int64,
    .set   = set_int64,
    .set_default_value = field_prop_set_default_value_int,
};

/* --- string --- */

static void release_string(Object *obj, const char *name, void *opaque)
{
    Property *prop = opaque;
    g_free(*(char **)object_field_prop_ptr(obj, prop));
}

static void get_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    char **ptr = object_field_prop_ptr(obj, prop);

    if (!*ptr) {
        char *str = (char *)"";
        visit_type_str(v, name, &str, errp);
    } else {
        visit_type_str(v, name, ptr, errp);
    }
}

static void set_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    char **ptr = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }
    g_free(*ptr);
    *ptr = str;
}

const PropertyInfo prop_info_string = {
    .name  = "str",
    .release = release_string,
    .get   = get_string,
    .set   = set_string,
};

/* --- on/off/auto --- */

const PropertyInfo prop_info_on_off_auto = {
    .name = "OnOffAuto",
    .description = "on/off/auto",
    .enum_table = &OnOffAuto_lookup,
    .get = field_prop_get_enum,
    .set = field_prop_set_enum,
    .set_default_value = field_prop_set_default_value_enum,
};

/* --- 32bit unsigned int 'size' type --- */

void field_prop_get_size32(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    uint64_t value = *ptr;

    visit_type_size(v, name, &value, errp);
}

static void set_size32(Object *obj, Visitor *v, const char *name, void *opaque,
                       Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    uint64_t value;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    if (value > UINT32_MAX) {
        error_setg(errp,
                   "Property %s.%s doesn't take value %" PRIu64
                   " (maximum: %u)",
                   object_get_typename(obj), name, value, UINT32_MAX);
        return;
    }

    *ptr = value;
}

const PropertyInfo prop_info_size32 = {
    .name  = "size",
    .get = field_prop_get_size32,
    .set = set_size32,
    .set_default_value = field_prop_set_default_value_uint,
};

/* --- support for array properties --- */

/* object property release callback for dynamically-created properties:
 * we call the underlying element's property release hook, and
 * then free the memory we allocated when we added the property.
 */
static void static_prop_release_dynamic_prop(Object *obj, const char *name,
                                             void *opaque)
{
    Property *prop = opaque;
    if (prop->info->release) {
        prop->info->release(obj, name, opaque);
    }
    g_free(prop);
}

static void set_prop_arraylen(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    /* Setter for the property which defines the length of a
     * variable-sized property array. As well as actually setting the
     * array-length field in the device struct, we have to create the
     * array itself and dynamically add the corresponding properties.
     */
    Property *prop = opaque;
    ObjectProperty *op = object_property_find_err(obj, name, &error_abort);
    uint32_t *alenptr = object_field_prop_ptr(obj, prop);
    void **arrayptr = (void *)obj + prop->arrayoffset;
    void *eltptr;
    const char *arrayname;
    int i;

    if (*alenptr) {
        error_setg(errp, "array size property %s may not be set more than once",
                   name);
        return;
    }
    if (!visit_type_uint32(v, name, alenptr, errp)) {
        return;
    }
    if (!*alenptr) {
        return;
    }

    /* DEFINE_PROP_ARRAY guarantees that name should start with this prefix;
     * strip it off so we can get the name of the array itself.
     */
    assert(strncmp(name, PROP_ARRAY_LEN_PREFIX,
                   strlen(PROP_ARRAY_LEN_PREFIX)) == 0);
    arrayname = name + strlen(PROP_ARRAY_LEN_PREFIX);

    /* Note that it is the responsibility of the individual device's deinit
     * to free the array proper.
     */
    *arrayptr = eltptr = g_malloc0(*alenptr * prop->arrayfieldsize);
    for (i = 0; i < *alenptr; i++, eltptr += prop->arrayfieldsize) {
        g_autofree char *propname = g_strdup_printf("%s[%d]", arrayname, i);
        Property arrayprop = { };
        arrayprop.info = prop->arrayinfo;
        /* This ugly piece of pointer arithmetic sets up the offset so
         * that when the underlying get/set hooks call qdev_get_prop_ptr
         * they get the right answer despite the array element not actually
         * being inside the device struct.
         */
        arrayprop.offset = eltptr - (void *)obj;
        assert(object_field_prop_ptr(obj, &arrayprop) == eltptr);
        object_property_add_field(obj, propname, &arrayprop,
                                  op->allow_set);
    }
}

const PropertyInfo prop_info_arraylen = {
    .name = "uint32",
    .get = get_uint32,
    .set = set_prop_arraylen,
    .set_default_value = field_prop_set_default_value_uint,
};

/* --- public helpers --- */

static Property *qdev_prop_walk(Property *props, const char *name)
{
    if (!props) {
        return NULL;
    }
    while (props->name_template) {
        if (strcmp(props->name_template, name) == 0) {
            return props;
        }
        props++;
    }
    return NULL;
}

static Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    ObjectClass *class;
    Property *prop;

    /* device properties */
    class = object_get_class(OBJECT(dev));
    do {
        prop = qdev_prop_walk(DEVICE_CLASS(class)->props_, name);
        if (prop) {
            return prop;
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    return NULL;
}

void error_set_from_qdev_prop_error(Error **errp, int ret, Object *obj,
                                    const char *name, const char *value)
{
    switch (ret) {
    case -EEXIST:
        error_setg(errp, "Property '%s.%s' can't take value '%s', it's in use",
                  object_get_typename(obj), name, value);
        break;
    default:
    case -EINVAL:
        error_setg(errp, QERR_PROPERTY_VALUE_BAD,
                   object_get_typename(obj), name, value);
        break;
    case -ENOENT:
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                  object_get_typename(obj), name, value);
        break;
    case 0:
        break;
    }
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    object_property_set_bool(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value)
{
    object_property_set_str(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_enum(DeviceState *dev, const char *name, int value)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    object_property_set_str(OBJECT(dev), name,
                            qapi_enum_lookup(prop->info->enum_table, value),
                            &error_abort);
}

static GPtrArray *global_props(void)
{
    static GPtrArray *gp;

    if (!gp) {
        gp = g_ptr_array_new();
    }

    return gp;
}

void qdev_prop_register_global(GlobalProperty *prop)
{
    g_ptr_array_add(global_props(), prop);
}

const GlobalProperty *qdev_find_global_prop(Object *obj,
                                            const char *name)
{
    GPtrArray *props = global_props();
    const GlobalProperty *p;
    int i;

    for (i = 0; i < props->len; i++) {
        p = g_ptr_array_index(props, i);
        if (object_dynamic_cast(obj, p->driver)
            && !strcmp(p->property, name)) {
            return p;
        }
    }
    return NULL;
}

int qdev_prop_check_globals(void)
{
    int i, ret = 0;

    for (i = 0; i < global_props()->len; i++) {
        GlobalProperty *prop;
        ObjectClass *oc;
        DeviceClass *dc;

        prop = g_ptr_array_index(global_props(), i);
        if (prop->used) {
            continue;
        }
        oc = object_class_by_name(prop->driver);
        oc = object_class_dynamic_cast(oc, TYPE_DEVICE);
        if (!oc) {
            warn_report("global %s.%s has invalid class name",
                        prop->driver, prop->property);
            ret = 1;
            continue;
        }
        dc = DEVICE_CLASS(oc);
        if (!dc->hotpluggable && !prop->used) {
            warn_report("global %s.%s=%s not used",
                        prop->driver, prop->property, prop->value);
            ret = 1;
            continue;
        }
    }
    return ret;
}

void qdev_prop_set_globals(DeviceState *dev)
{
    object_apply_global_props(OBJECT(dev), global_props(),
                              dev->hotplugged ? NULL : &error_fatal);
}

/* --- 64bit unsigned int 'size' type --- */

static void get_size(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_size(v, name, ptr, errp);
}

static void set_size(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_size(v, name, ptr, errp);
}

const PropertyInfo prop_info_size = {
    .name  = "size",
    .get = get_size,
    .set = set_size,
    .set_default_value = field_prop_set_default_value_uint,
};

/* --- object link property --- */

static ObjectProperty *create_link_property(ObjectClass *oc, const char *name,
                                            Property *prop)
{
    /*
     * NOTE: object_property_allow_set_link is unconditional, but
     *       ObjectProperty.allow_set may be set for the property too.
     */
    return object_class_property_add_link(oc, name, prop->link_type,
                                          prop->offset,
                                          object_property_allow_set_link,
                                          OBJ_PROP_LINK_STRONG);
}

const PropertyInfo prop_info_link = {
    .name = "link",
    .create = create_link_property,
};

ObjectProperty *
object_property_add_field(Object *obj, const char *name,
                          Property *prop,
                          ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;
    Property *newprop = g_new0(Property, 1);

    assert(allow_set);
    assert(!prop->info->create);

    *newprop = *prop;
    op = object_property_add(obj, name, newprop->info->name,
                             field_prop_getter(newprop->info),
                             field_prop_setter(newprop->info),
                             static_prop_release_dynamic_prop,
                             newprop);

    object_property_set_description(obj, name,
                                    newprop->info->description);

    if (newprop->set_default) {
        newprop->info->set_default_value(op, newprop);
        if (op->init) {
            op->init(obj, op);
        }
    }

    op->allow_set = allow_set;
    return op;
}

ObjectProperty *
object_class_property_add_field_static(ObjectClass *oc, const char *name,
                                       Property *prop,
                                       ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;

    assert(allow_set);

    if (prop->info->create) {
        op = prop->info->create(oc, name, prop);
    } else {
        op = object_class_property_add(oc,
                                       name, prop->info->name,
                                       field_prop_getter(prop->info),
                                       field_prop_setter(prop->info),
                                       prop->info->release,
                                       prop);
    }
    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
    }
    if (prop->info->description) {
        object_class_property_set_description(oc, name,
                                              prop->info->description);
    }

    op->allow_set = allow_set;
    return op;
}

void object_class_add_field_properties(ObjectClass *oc, Property *props,
                                       ObjectPropertyAllowSet allow_set)
{
    Property *prop;

    for (prop = props; prop && prop->name_template; prop++) {
        object_class_property_add_field_static(oc, prop->name_template, prop,
                                               allow_set);
    }
}


void qdev_property_add_static(DeviceState *dev, Property *prop)
{
    object_property_add_field(OBJECT(dev), prop->name_template, prop,
                              qdev_prop_allow_set);
}

/**
 * Legacy property handling
 */

static void qdev_get_legacy_property(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    Property *prop = opaque;

    char buffer[1024];
    char *ptr = buffer;

    prop->info->print(obj, prop, buffer, sizeof(buffer));
    visit_type_str(v, name, &ptr, errp);
}

/**
 * qdev_class_add_legacy_property:
 * @dev: Device to add the property to.
 * @prop: The qdev property definition.
 *
 * Add a legacy QOM property to @dev for qdev property @prop.
 *
 * Legacy properties are string versions of QOM properties.  The format of
 * the string depends on the property type.  Legacy properties are only
 * needed for "info qtree".
 *
 * Do not use this in new code!  QOM Properties added through this interface
 * will be given names in the "legacy" namespace.
 */
static void qdev_class_add_legacy_property(DeviceClass *dc, Property *prop)
{
    g_autofree char *name = NULL;

    /* Register pointer properties as legacy properties */
    if (!prop->info->print && prop->info->get) {
        return;
    }

    name = g_strdup_printf("legacy-%s", prop->name_template);
    object_class_property_add(OBJECT_CLASS(dc), name, "str",
        prop->info->print ? qdev_get_legacy_property : prop->info->get,
        NULL, NULL, prop);
}

void device_class_set_props(DeviceClass *dc, Property *props)
{
    ObjectClass *oc = OBJECT_CLASS(dc);
    Property *prop;

    dc->props_ = props;
    for (prop = props; prop && prop->name_template; prop++) {
        qdev_class_add_legacy_property(dc, prop);
    }

    object_class_add_field_properties(oc, props, qdev_prop_allow_set);
}

void qdev_alias_all_properties(DeviceState *target, Object *source)
{
    ObjectClass *class;
    Property *prop;

    class = object_get_class(OBJECT(target));
    do {
        DeviceClass *dc = DEVICE_CLASS(class);

        for (prop = dc->props_; prop && prop->name_template; prop++) {
            object_property_add_alias(source, prop->name_template,
                                      OBJECT(target), prop->name_template);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));
}
