#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"


#define TYPE_MY_DEV "my-dev"
typedef struct MyDev MyDev;
DECLARE_INSTANCE_CHECKER(MyDev, STATIC_TYPE,
                         TYPE_MY_DEV)

#define TYPE_REENTRANT_REALIZATION "reentrant-realization"

struct MyDev {
    DeviceState parent_obj;

    uint32_t prop_u32;
    char *prop_string;
    uint32_t *prop_array_u32;
    uint32_t prop_array_u32_nb;
    uint16_t realization_count;
    uint16_t unrealization_count;
    Error *realization_err;
};

static const Property my_dev_props[] = {
    DEFINE_PROP_UINT32("u32", MyDev, prop_u32, 100),
    DEFINE_PROP_STRING("string", MyDev, prop_string),
    DEFINE_PROP_ARRAY("array-u32", MyDev, prop_array_u32_nb, prop_array_u32,
                     qdev_prop_uint32, uint32_t),
};

static void my_dev_realize(DeviceState *dev, Error **errp)
{
    MyDev *mt = STATIC_TYPE(dev);
    mt->realization_count++;
    error_propagate(errp, g_steal_pointer(&mt->realization_err));
}

static void my_dev_unrealize(DeviceState *dev)
{
    MyDev *mt = STATIC_TYPE(dev);
    mt->unrealization_count++;
}

static void my_dev_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = my_dev_realize;
    dc->unrealize = my_dev_unrealize;
    device_class_set_props(dc, my_dev_props);
}

static const TypeInfo my_dev_type_info = {
    .name = TYPE_MY_DEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MyDev),
    .class_init = my_dev_class_init,
};

static void reentrant_realization_realize(DeviceState *dev, Error **errp)
{
    g_assert_false(qdev_realize(dev, NULL, NULL));
}

static void reentrant_realization_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = reentrant_realization_realize;
}

static const TypeInfo reentrant_realization_type_info = {
    .name = TYPE_REENTRANT_REALIZATION,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DeviceState),
    .class_init = reentrant_realization_class_init,
};

/*
 * Initialize a fake machine, being prepared for future tests.
 *
 * Realization of anonymous qdev (with no parent object) requires both
 * the machine object and its "unattached" container to be at least present.
 */
static void test_init_machine(void)
{
    /* This is a fake machine - it doesn't need to be a machine object */
    Object *machine = object_property_add_new_container(
        object_get_root(), "machine");

    /* This container must exist for anonymous qdevs to realize() */
    object_property_add_new_container(machine, "unattached");
}

static void test_qdev_free_properties(void)
{
    MyDev *mt;

    mt = STATIC_TYPE(object_new(TYPE_MY_DEV));
    object_set_props(OBJECT(mt), &error_fatal,
                     "string", "something",
                     "array-u32", "12,13",
                     NULL);
    qdev_realize(DEVICE(mt), NULL, &error_fatal);

    g_assert_cmpuint(mt->prop_u32, ==, 100);
    g_assert_cmpstr(mt->prop_string, ==, "something");
    g_assert_cmpuint(mt->prop_array_u32_nb, ==, 2);
    g_assert_cmpuint(mt->prop_array_u32[0], ==, 12);
    g_assert_cmpuint(mt->prop_array_u32[1], ==, 13);

    object_unparent(OBJECT(mt));
    object_unref(mt);
}

static void test_qdev_double_realization(void)
{
    MyDev *mt = STATIC_TYPE(object_new(TYPE_MY_DEV));

    g_assert_cmpint(mt->realization_count, ==, 0);
    qdev_realize(DEVICE(mt), NULL, &error_fatal);
    g_assert_cmpint(mt->realization_count, ==, 1);
    qdev_realize(DEVICE(mt), NULL, &error_fatal);
    g_assert_cmpint(mt->realization_count, ==, 1);
    object_unparent(OBJECT(mt));
    object_unref(OBJECT(mt));
}

static void test_qdev_realize_after_unrealization(void)
{
    Object *mt = object_new(TYPE_MY_DEV);

    qdev_unrealize(DEVICE(mt));
    g_assert_false(qdev_realize(DEVICE(mt), NULL, NULL));
    object_unparent(mt);
    object_unref(mt);
}

static void test_qdev_reentrant_realization(void)
{
    Object *obj = object_new(TYPE_REENTRANT_REALIZATION);

    qdev_realize(DEVICE(obj), NULL, &error_fatal);
    object_unparent(OBJECT(obj));
    object_unref(obj);
}

static void test_qdev_retry_realization(void)
{
    MyDev *mt = STATIC_TYPE(object_new(TYPE_MY_DEV));

    error_setg(&mt->realization_err, "error");
    g_assert_false(qdev_realize(DEVICE(mt), NULL, NULL));
    g_assert_false(qdev_realize(DEVICE(mt), NULL, NULL));
    object_unparent(OBJECT(mt));
    object_unref(OBJECT(mt));
}

static void test_qdev_unrealize_after_realization(void)
{
    MyDev *mt = STATIC_TYPE(object_new(TYPE_MY_DEV));

    qdev_realize(DEVICE(mt), NULL, &error_fatal);
    g_assert_cmpint(mt->unrealization_count, ==, 0);
    qdev_unrealize(DEVICE(mt));
    g_assert_cmpint(mt->unrealization_count, ==, 1);
    object_unparent(OBJECT(mt));
    object_unref(OBJECT(mt));
}

static void test_qdev_unrealize_without_realization(void)
{
    MyDev *mt = STATIC_TYPE(object_new(TYPE_MY_DEV));

    g_assert_cmpint(mt->unrealization_count, ==, 0);
    qdev_unrealize(DEVICE(mt));
    g_assert_cmpint(mt->unrealization_count, ==, 0);
    object_unparent(OBJECT(mt));
    object_unref(OBJECT(mt));
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&my_dev_type_info);
    type_register_static(&reentrant_realization_type_info);
    test_init_machine();

    g_test_add_func("/qdev/free-properties",
                    test_qdev_free_properties);

    g_test_add_func("/qdev/double-realization",
                    test_qdev_double_realization);

    g_test_add_func("/qdev/realize-after-unrealization",
                    test_qdev_realize_after_unrealization);

    g_test_add_func("/qdev/reentrant-realization",
                    test_qdev_reentrant_realization);

    g_test_add_func("/qdev/retry-realization",
                    test_qdev_retry_realization);

    g_test_add_func("/qdev/unrealize-after-realization",
                    test_qdev_unrealize_after_realization);

    g_test_add_func("/qdev/unrealize-without-realization",
                    test_qdev_unrealize_without_realization);

    g_test_run();

    return 0;
}
