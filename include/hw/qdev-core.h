#ifndef QDEV_CORE_H
#define QDEV_CORE_H

#include "qemu/queue.h"
#include "qemu/bitmap.h"
#include "qom/object.h"
#include "hw/irq.h"
#include "hw/hotplug.h"
#include "sysemu/sysemu.h"
#include "hw/resettable.h"

enum {
    DEV_NVECTORS_UNSPECIFIED = -1,
};

#define TYPE_DEVICE "device"
#define DEVICE(obj) OBJECT_CHECK(DeviceState, (obj), TYPE_DEVICE)
#define DEVICE_CLASS(klass) OBJECT_CLASS_CHECK(DeviceClass, (klass), TYPE_DEVICE)
#define DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(DeviceClass, (obj), TYPE_DEVICE)

typedef enum DeviceCategory {
    DEVICE_CATEGORY_BRIDGE,
    DEVICE_CATEGORY_USB,
    DEVICE_CATEGORY_STORAGE,
    DEVICE_CATEGORY_NETWORK,
    DEVICE_CATEGORY_INPUT,
    DEVICE_CATEGORY_DISPLAY,
    DEVICE_CATEGORY_SOUND,
    DEVICE_CATEGORY_MISC,
    DEVICE_CATEGORY_CPU,
    DEVICE_CATEGORY_MAX
} DeviceCategory;

typedef void (*DeviceRealize)(DeviceState *dev, Error **errp);
typedef void (*DeviceUnrealize)(DeviceState *dev, Error **errp);
typedef void (*DeviceReset)(DeviceState *dev);
typedef void (*BusRealize)(BusState *bus, Error **errp);
typedef void (*BusUnrealize)(BusState *bus, Error **errp);

struct VMStateDescription;

/**
 * DeviceClass:
 * @props: Properties accessing state fields.
 * @realize: Callback function invoked when the #DeviceState:realized
 * property is changed to %true.
 * @unrealize: Callback function invoked when the #DeviceState:realized
 * property is changed to %false.
 * @hotpluggable: indicates if #DeviceClass is hotpluggable, available
 * as readonly "hotpluggable" property of #DeviceState instance
 *
 * # Realization #
 * Devices are constructed in two stages,
 * 1) object instantiation via object_initialize() and
 * 2) device realization via #DeviceState:realized property.
 * The former may not fail (and must not abort or exit, since it is called
 * during device introspection already), and the latter may return error
 * information to the caller and must be re-entrant.
 * Trivial field initializations should go into #TypeInfo.instance_init.
 * Operations depending on @props static properties should go into @realize.
 * After successful realization, setting static properties will fail.
 *
 * As an interim step, the #DeviceState:realized property can also be
 * set with qdev_init_nofail().
 * In the future, devices will propagate this state change to their children
 * and along busses they expose.
 * The point in time will be deferred to machine creation, so that values
 * set in @realize will not be introspectable beforehand. Therefore devices
 * must not create children during @realize; they should initialize them via
 * object_initialize() in their own #TypeInfo.instance_init and forward the
 * realization events appropriately.
 *
 * Any type may override the @realize and/or @unrealize callbacks but needs
 * to call the parent type's implementation if keeping their functionality
 * is desired. Refer to QOM documentation for further discussion and examples.
 *
 * <note>
 *   <para>
 * Since TYPE_DEVICE doesn't implement @realize and @unrealize, types
 * derived directly from it need not call their parent's @realize and
 * @unrealize.
 * For other types consult the documentation and implementation of the
 * respective parent types.
 *   </para>
 * </note>
 */
typedef struct DeviceClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    DECLARE_BITMAP(categories, DEVICE_CATEGORY_MAX);
    const char *fw_name;
    const char *desc;
    Property *props;

    /*
     * Can this device be instantiated with -device / device_add?
     * All devices should support instantiation with device_add, and
     * this flag should not exist.  But we're not there, yet.  Some
     * devices fail to instantiate with cryptic error messages.
     * Others instantiate, but don't work.  Exposing users to such
     * behavior would be cruel; clearing this flag will protect them.
     * It should never be cleared without a comment explaining why it
     * is cleared.
     * TODO remove once we're there
     */
    bool user_creatable;
    bool hotpluggable;

    /* callbacks */
    /*
     * Reset method here is deprecated and replaced by methods in the
     * resettable class interface to implement a multi-phase reset.
     * TODO: remove once every reset callback is unused
     */
    DeviceReset reset;
    DeviceRealize realize;
    DeviceUnrealize unrealize;

    /* device state */
    const struct VMStateDescription *vmsd;
    const struct VMStateDescription *vmsd_ext;

    /* Private to qdev / bus.  */
    const char *bus_type;
} DeviceClass;

typedef struct NamedGPIOList NamedGPIOList;

struct NamedGPIOList {
    char *name;
    qemu_irq *in;
    int num_in;
    int num_out;
    QLIST_ENTRY(NamedGPIOList) node;
};

typedef enum DeviceResetActiveType {
    DEVICE_RESET_ACTIVE_LOW,
    DEVICE_RESET_ACTIVE_HIGH,
} DeviceResetActiveType;

/**
 * DeviceResetInputState:
 * @exists: tell if io exists
 * @type: tell whether the io active low or high
 * @state: true if reset is currently active
 */
typedef struct DeviceResetInputState {
    bool exists;
    DeviceResetActiveType type;
    bool state;
} DeviceResetInputState;

/**
 * DeviceState:
 * @realized: Indicates whether the device has been fully constructed.
 * @resetting: Indicates whether the device is under reset. Also
 * used to count how many times reset has been initiated on the device.
 * @reset_is_cold: If the device is under reset, indicates whether it is cold
 * or warm.
 * @cold_reset_input: state data for cold reset io
 * @warm_reset_input: state data for warm reset io
 *
 * This structure should not be accessed directly.  We declare it here
 * so that it can be embedded in individual device state structures.
 */
struct DeviceState {
    /*< private >*/
    Object parent_obj;
    /*< public >*/

    const char *id;
    char *canonical_path;
    bool realized;
    bool pending_deleted_event;
    QemuOpts *opts;
    int hotplugged;
    BusState *parent_bus;
    QLIST_HEAD(, NamedGPIOList) gpios;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    int instance_id_alias;
    int alias_required_for_version;
    uint32_t resetting;
    bool reset_is_cold;
    bool reset_hold_needed;
    DeviceResetInputState cold_reset_input;
    DeviceResetInputState warm_reset_input;
};

struct DeviceListener {
    void (*realize)(DeviceListener *listener, DeviceState *dev);
    void (*unrealize)(DeviceListener *listener, DeviceState *dev);
    QTAILQ_ENTRY(DeviceListener) link;
};

#define TYPE_BUS "bus"
#define BUS(obj) OBJECT_CHECK(BusState, (obj), TYPE_BUS)
#define BUS_CLASS(klass) OBJECT_CLASS_CHECK(BusClass, (klass), TYPE_BUS)
#define BUS_GET_CLASS(obj) OBJECT_GET_CLASS(BusClass, (obj), TYPE_BUS)

struct BusClass {
    ObjectClass parent_class;

    /* FIXME first arg should be BusState */
    void (*print_dev)(Monitor *mon, DeviceState *dev, int indent);
    char *(*get_dev_path)(DeviceState *dev);
    /*
     * This callback is used to create Open Firmware device path in accordance
     * with OF spec http://forthworks.com/standards/of1275.pdf. Individual bus
     * bindings can be found at http://playground.sun.com/1275/bindings/.
     */
    char *(*get_fw_dev_path)(DeviceState *dev);
    void (*reset)(BusState *bus);
    BusRealize realize;
    BusUnrealize unrealize;

    /* maximum devices allowed on the bus, 0: no limit. */
    int max_dev;
    /* number of automatically allocated bus ids (e.g. ide.0) */
    int automatic_ids;
};

typedef struct BusChild {
    DeviceState *child;
    int index;
    QTAILQ_ENTRY(BusChild) sibling;
} BusChild;

#define QDEV_HOTPLUG_HANDLER_PROPERTY "hotplug-handler"

/**
 * BusState:
 * @hotplug_handler: link to a hotplug handler associated with bus.
 * @resetting: Indicates whether the bus is under reset. Also
 * used to count how many times reset has been initiated on the bus.
 * @reset_is_cold: If the bus is under reset, indicates whether it is cold
 * or warm.
 */
struct BusState {
    Object obj;
    DeviceState *parent;
    char *name;
    HotplugHandler *hotplug_handler;
    int max_index;
    bool realized;
    int num_children;
    QTAILQ_HEAD(, BusChild) children;
    QLIST_ENTRY(BusState) sibling;
    uint32_t resetting;
    bool reset_is_cold;
    bool reset_hold_needed;
};

/**
 * Property:
 * @set_default: true if the default value should be set from @defval,
 *    in which case @info->set_default_value must not be NULL
 *    (if false then no default value is set by the property system
 *     and the field retains whatever value it was given by instance_init).
 * @defval: default value for the property. This is used only if @set_default
 *     is true.
 */
struct Property {
    const char   *name;
    const PropertyInfo *info;
    ptrdiff_t    offset;
    uint8_t      bitnr;
    bool         set_default;
    union {
        int64_t i;
        uint64_t u;
    } defval;
    int          arrayoffset;
    const PropertyInfo *arrayinfo;
    int          arrayfieldsize;
    const char   *link_type;
};

struct PropertyInfo {
    const char *name;
    const char *description;
    const QEnumLookup *enum_table;
    int (*print)(DeviceState *dev, Property *prop, char *dest, size_t len);
    void (*set_default_value)(Object *obj, const Property *prop);
    void (*create)(Object *obj, Property *prop, Error **errp);
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyRelease *release;
};

/**
 * GlobalProperty:
 * @used: Set to true if property was used when initializing a device.
 * @optional: If set to true, GlobalProperty will be skipped without errors
 *            if the property doesn't exist.
 *
 * An error is fatal for non-hotplugged devices, when the global is applied.
 */
typedef struct GlobalProperty {
    const char *driver;
    const char *property;
    const char *value;
    bool used;
    bool optional;
} GlobalProperty;

static inline void
compat_props_add(GPtrArray *arr,
                 GlobalProperty props[], size_t nelem)
{
    int i;
    for (i = 0; i < nelem; i++) {
        g_ptr_array_add(arr, (void *)&props[i]);
    }
}

/*** Board API.  This should go away once we have a machine config file.  ***/

DeviceState *qdev_create(BusState *bus, const char *name);
DeviceState *qdev_try_create(BusState *bus, const char *name);
void qdev_init_nofail(DeviceState *dev);
void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version);
HotplugHandler *qdev_get_bus_hotplug_handler(DeviceState *dev);
HotplugHandler *qdev_get_machine_hotplug_handler(DeviceState *dev);
/**
 * qdev_get_hotplug_handler: Get handler responsible for device wiring
 *
 * Find HOTPLUG_HANDLER for @dev that provides [pre|un]plug callbacks for it.
 *
 * Note: in case @dev has a parent bus, it will be returned as handler unless
 * machine handler overrides it.
 *
 * Returns: pointer to object that implements TYPE_HOTPLUG_HANDLER interface
 *          or NULL if there aren't any.
 */
HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev);
void qdev_unplug(DeviceState *dev, Error **errp);
void qdev_simple_device_unplug_cb(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp);
void qdev_machine_creation_done(void);
bool qdev_machine_modified(void);

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
qemu_irq qdev_get_gpio_in_named(DeviceState *dev, const char *name, int n);

void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);
void qdev_connect_gpio_out_named(DeviceState *dev, const char *name, int n,
                                 qemu_irq pin);
qemu_irq qdev_get_gpio_out_connector(DeviceState *dev, const char *name, int n);
qemu_irq qdev_intercept_gpio_out(DeviceState *dev, qemu_irq icpt,
                                 const char *name, int n);

BusState *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

/* Register device properties.  */
/* GPIO inputs also double as IRQ sinks.  */
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);
void qdev_init_gpio_out_named(DeviceState *dev, qemu_irq *pins,
                              const char *name, int n);
/**
 * qdev_init_gpio_in_named_with_opaque: create an array of input GPIO lines
 *   for the specified device
 *
 * @dev: Device to create input GPIOs for
 * @handler: Function to call when GPIO line value is set
 * @opaque: Opaque data pointer to pass to @handler
 * @name: Name of the GPIO input (must be unique for this device)
 * @n: Number of GPIO lines in this input set
 */
void qdev_init_gpio_in_named_with_opaque(DeviceState *dev,
                                         qemu_irq_handler handler,
                                         void *opaque,
                                         const char *name, int n);

/**
 * qdev_init_gpio_in_named: create an array of input GPIO lines
 *   for the specified device
 *
 * Like qdev_init_gpio_in_named_with_opaque(), but the opaque pointer
 * passed to the handler is @dev (which is the most commonly desired behaviour).
 */
static inline void qdev_init_gpio_in_named(DeviceState *dev,
                                           qemu_irq_handler handler,
                                           const char *name, int n)
{
    qdev_init_gpio_in_named_with_opaque(dev, handler, dev, name, n);
}

void qdev_pass_gpios(DeviceState *dev, DeviceState *container,
                     const char *name);

/**
 * qdev_init_reset_gpio_in_named:
 * Create a gpio controlling the warm or cold reset of the device.
 *
 * @cold: specify whether it triggers cold or warm reset
 * @type: what kind of reset io it is
 *
 * Note: the io is considered created in its inactive state. No reset
 * is started by this function.
 */
void qdev_init_reset_gpio_in_named(DeviceState *dev, const char *name,
                                   bool cold, DeviceResetActiveType type);

/**
 * qdev_init_warm_reset_gpio:
 * Create the input to control the device warm reset.
 */
static inline void qdev_init_warm_reset_gpio(DeviceState *dev,
                                             const char *name,
                                             DeviceResetActiveType type)
{
    qdev_init_reset_gpio_in_named(dev, name, false, type);
}

/**
 * qdev_init_cold_reset_gpio:
 * Create the input to control the device cold reset.
 * It can also be used as a power gate control.
 */
static inline void qdev_init_cold_reset_gpio(DeviceState *dev,
                                             const char *name,
                                             DeviceResetActiveType type)
{
    qdev_init_reset_gpio_in_named(dev, name, true, type);
}

BusState *qdev_get_parent_bus(DeviceState *dev);

/*** BUS API. ***/

DeviceState *qdev_find_recursive(BusState *bus, const char *id);

/* Returns 0 to walk children, > 0 to skip walk, < 0 to terminate walk. */
typedef int (qbus_walkerfn)(BusState *bus, void *opaque);
typedef int (qdev_walkerfn)(DeviceState *dev, void *opaque);

void qbus_create_inplace(void *bus, size_t size, const char *typename,
                         DeviceState *parent, const char *name);
BusState *qbus_create(const char *typename, DeviceState *parent, const char *name);
/* Returns > 0 if either devfn or busfn skip walk somewhere in cursion,
 *         < 0 if either devfn or busfn terminate walk somewhere in cursion,
 *           0 otherwise. */
int qbus_walk_children(BusState *bus,
                       qdev_walkerfn *pre_devfn, qbus_walkerfn *pre_busfn,
                       qdev_walkerfn *post_devfn, qbus_walkerfn *post_busfn,
                       void *opaque);
int qdev_walk_children(DeviceState *dev,
                       qdev_walkerfn *pre_devfn, qbus_walkerfn *pre_busfn,
                       qdev_walkerfn *post_devfn, qbus_walkerfn *post_busfn,
                       void *opaque);

/**
 * device_reset:
 * Resets the device @dev, @cold tell whether to do a cold or warm reset.
 * Uses the ressetable interface.
 * Base behavior is to reset the device and its qdev/qbus subtree.
 */
void device_reset(DeviceState *dev, bool cold);

static inline void device_reset_warm(DeviceState *dev)
{
    device_reset(dev, false);
}

static inline void device_reset_cold(DeviceState *dev)
{
    device_reset(dev, true);
}

/**
 * bus_reset:
 * Resets the bus @bus, @cold tell whether to do a cold or warm reset.
 * Uses the ressetable interface.
 * Base behavior is to reset the bus and its qdev/qbus subtree.
 */
void bus_reset(BusState *bus, bool cold);

static inline void bus_reset_warm(BusState *bus)
{
    bus_reset(bus, false);
}

static inline void bus_reset_cold(BusState *bus)
{
    bus_reset(bus, true);
}

/**
 * device_is_resetting:
 * Tell whether the device @dev is currently under reset.
 */
bool device_is_resetting(DeviceState *dev);

/**
 * device_is_reset_cold:
 * Tell whether the device @dev is currently under reset cold or warm reset.
 *
 * Note: only valid when device_is_resetting returns true.
 */
bool device_is_reset_cold(DeviceState *dev);

/**
 * bus_is_resetting:
 * Tell whether the bus @bus is currently under reset.
 */
bool bus_is_resetting(BusState *bus);

/**
 * bus_is_reset_cold:
 * Tell whether the bus @bus is currently under reset cold or warm reset.
 *
 * Note: only valid when bus_is_resetting returns true.
 */
bool bus_is_reset_cold(BusState *bus);

/**
 * qbus/qdev_reset_all:
 * @bus/dev: Bus/Device to be reset.
 *
 * Reset @bus/dev and perform a bus-level reset of all devices/buses connected
 * to it, including recursive processing of all buses below @bus itself.  A
 * hard reset means that qbus_reset_all will reset all state of the device.
 * For PCI devices, for example, this will include the base address registers
 * or configuration space.
 *
 * Theses functions are deprecated, please use device/bus_reset or
 * resettable_reset_* instead
 * TODO: remove them when all occurence are removed
 */
void qdev_reset_all(DeviceState *dev);
void qdev_reset_all_fn(void *opaque);
void qbus_reset_all(BusState *bus);
void qbus_reset_all_fn(void *opaque);

/* This should go away once we get rid of the NULL bus hack */
BusState *sysbus_get_default(void);

char *qdev_get_fw_dev_path(DeviceState *dev);
char *qdev_get_own_fw_dev_path_from_handler(BusState *bus, DeviceState *dev);

/**
 * @qdev_machine_init
 *
 * Initialize platform devices before machine init.  This is a hack until full
 * support for composition is added.
 */
void qdev_machine_init(void);

/**
 * device_legacy_reset:
 *
 * Reset a single device (by calling the reset method).
 *
 * This function is deprecated, please use device_reset() instead.
 * TODO: remove the function when all occurences are removed.
 */
void device_legacy_reset(DeviceState *dev);

/**
 * device_class_set_parent_reset:
 * TODO: remove the function when DeviceClass's reset method
 * is not used anymore.
 */
void device_class_set_parent_reset(DeviceClass *dc,
                                   DeviceReset dev_reset,
                                   DeviceReset *parent_reset);
void device_class_set_parent_realize(DeviceClass *dc,
                                     DeviceRealize dev_realize,
                                     DeviceRealize *parent_realize);
void device_class_set_parent_unrealize(DeviceClass *dc,
                                       DeviceUnrealize dev_unrealize,
                                       DeviceUnrealize *parent_unrealize);

const struct VMStateDescription *qdev_get_vmsd(DeviceState *dev);

void device_class_build_extended_vmsd(DeviceClass *dc);

const char *qdev_fw_name(DeviceState *dev);

Object *qdev_get_machine(void);

/* FIXME: make this a link<> */
void qdev_set_parent_bus(DeviceState *dev, BusState *bus);

extern bool qdev_hotplug;
extern bool qdev_hot_removed;

char *qdev_get_dev_path(DeviceState *dev);

GSList *qdev_build_hotpluggable_device_list(Object *peripheral);

void qbus_set_hotplug_handler(BusState *bus, Object *handler, Error **errp);

void qbus_set_bus_hotplug_handler(BusState *bus, Error **errp);

static inline bool qbus_is_hotpluggable(BusState *bus)
{
   return bus->hotplug_handler;
}

void device_listener_register(DeviceListener *listener);
void device_listener_unregister(DeviceListener *listener);

VMChangeStateEntry *qdev_add_vm_change_state_handler(DeviceState *dev,
                                                     VMChangeStateHandler *cb,
                                                     void *opaque);

#endif
