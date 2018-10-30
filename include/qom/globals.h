#ifndef QOM_GLOBALS
#define QOM_GLOBALS

#include "qom/object.h"

/**
 * GlobalProperty:
 * @user_provided: Set to true if property comes from user-provided config
 * (command-line or config file).
 * @used: Set to true if property was used when initializing a device.
 * @errp: Error destination, used like first argument of error_setg()
 *        in case property setting fails later. If @errp is NULL, we
 *        print warnings instead of ignoring errors silently. For
 *        hotplugged devices, errp is always ignored and warnings are
 *        printed instead.
 */
typedef struct GlobalProperty {
    const char *driver;
    const char *property;
    const char *value;
    bool user_provided;
    bool used;
    Error **errp;
} GlobalProperty;

void object_property_register_global(GlobalProperty *prop);

void object_property_set_globals(Object *obj);

int object_property_check_globals(void);

#endif
