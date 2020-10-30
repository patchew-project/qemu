#ifndef QEMU_QDEV_PROPERTIES_H
#define QEMU_QDEV_PROPERTIES_H

#include "hw/qdev-core.h"
#include "qom/static-property.h"

/*
 * Set properties between creation and realization.
 *
 * Returns: %true on success, %false on error.
 */
bool qdev_prop_set_drive_err(DeviceState *dev, const char *name,
                             BlockBackend *value, Error **errp);

/*
 * Set properties between creation and realization.
 * @value must be valid.  Each property may be set at most once.
 */
void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value);
void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value);
void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value);
void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value);
void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value);
void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value);
void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value);
void qdev_prop_set_chr(DeviceState *dev, const char *name, Chardev *value);
void qdev_prop_set_netdev(DeviceState *dev, const char *name, NetClientState *value);
void qdev_prop_set_drive(DeviceState *dev, const char *name,
                         BlockBackend *value);
void qdev_prop_set_macaddr(DeviceState *dev, const char *name,
                           const uint8_t *value);
void qdev_prop_set_enum(DeviceState *dev, const char *name, int value);

void qdev_prop_register_global(GlobalProperty *prop);
const GlobalProperty *qdev_find_global_prop(Object *obj,
                                            const char *name);
int qdev_prop_check_globals(void);
void qdev_prop_set_globals(DeviceState *dev);

/**
 * qdev_property_add_static:
 * @dev: Device to add the property to.
 * @prop: The qdev property definition.
 *
 * Add a static QOM property to @dev for qdev property @prop.
 * On error, store error in @errp.  Static properties access data in a struct.
 * The type of the QOM property is derived from prop->info.
 */
void qdev_property_add_static(DeviceState *dev, Property *prop);

/**
 * qdev_alias_all_properties: Create aliases on source for all target properties
 * @target: Device which has properties to be aliased
 * @source: Object to add alias properties to
 *
 * Add alias properties to the @source object for all qdev properties on
 * the @target DeviceState.
 *
 * This is useful when @target is an internal implementation object
 * owned by @source, and you want to expose all the properties of that
 * implementation object as properties on the @source object so that users
 * of @source can set them.
 */
void qdev_alias_all_properties(DeviceState *target, Object *source);

/**
 * @qdev_prop_set_after_realize:
 * @dev: device
 * @name: name of property
 * @errp: indirect pointer to Error to be set
 * Set the Error object to report that an attempt was made to set a property
 * on a device after it has already been realized. This is a utility function
 * which allows property-setter functions to easily report the error in
 * a friendly format identifying both the device and the property.
 */
void qdev_prop_set_after_realize(DeviceState *dev, const char *name,
                                 Error **errp);

/**
 * qdev_prop_allow_set_link_before_realize:
 *
 * Set the #Error object if an attempt is made to set the link after realize.
 * This function should be used as the check() argument to
 * object_property_add_link().
 */
void qdev_prop_allow_set_link_before_realize(const Object *obj,
                                             const char *name,
                                             Object *val, Error **errp);

#endif
