#ifndef HW_RESETTABLE_H
#define HW_RESETTABLE_H

#include "qom/object.h"

#define TYPE_RESETTABLE_INTERFACE "resettable"

#define RESETTABLE_CLASS(class) \
    OBJECT_CLASS_CHECK(ResettableClass, (class), TYPE_RESETTABLE_INTERFACE)

typedef struct ResetState ResetState;

/**
 * ResetType:
 * Types of reset.
 *
 * + Cold: reset resulting from a power cycle of the object.
 *
 * TODO: Support has to be added to handle more types. In particular,
 * ResetState structure needs to be expanded.
 */
typedef enum ResetType {
    RESET_TYPE_COLD,
} ResetType;

/*
 * ResettableClass:
 * Interface for resettable objects.
 *
 * See docs/devel/reset.rst for more detailed information about
 * how QEMU models reset.
 *
 * All objects which can be reset must implement this interface;
 * it is usually provided by a base class such as DeviceClass or BusClass.
 * Every Resettable object must maintain some state tracking the
 * progress of a reset operation by providing a ResetState structure.
 * The functions defined in this module take care of updating the
 * state of the reset.
 * The base class implementation of the interface provides this
 * state and implements the associated method: get_state.
 *
 * Concrete object implementations (typically specific devices
 * such as a UART model) should provide the functions
 * for the phases.init, phases.hold and phases.exit methods, which
 * they can set in their class init function, either directly or
 * by calling resettable_class_set_parent_phases().
 * The phase methods are guaranteed to only only ever be called once
 * for any reset event, in the order 'init', 'hold', 'exit'.
 * An object will always move quickly from 'init' to 'hold'
 * but might remain in 'hold' for an arbitrary period of time
 * before eventually reset is deasserted and the 'exit' phase is called.
 * Object implementations should be prepared for functions handling
 * inbound connections from other devices (such as qemu_irq handler
 * functions) to be called at any point during reset after their
 * 'init' method has been called.
 *
 * Users of a resettable object should not call these methods
 * directly, but instead use the function resettable_reset().
 *
 * @phases.init: This phase is called when the object enters reset. It
 * should reset local state of the object, but it must not do anything that
 * has a side-effect on other objects, such as raising or lowering a qemu_irq
 * line or reading or writing guest memory. It takes the reset's type as
 * argument.
 *
 * @phases.hold: This phase is called for entry into reset, once every object
 * in the system which is being reset has had its @phases.init method called.
 * At this point devices can do actions that affect other objects.
 *
 * @phases.exit: This phase is called when the object leaves the reset state.
 * Actions affecting other objects are permitted.
 *
 * @get_state: Mandatory method which must return a pointer to a ResetState.
 *
 * @foreach_child: Executes a given function on every Resettable child. Child
 * in this context means a child in the qbus tree, so the children of a qbus
 * are the devices on it, and the children of a device are all the buses it
 * owns. This is not the same as the QOM object hierarchy. The function takes
 * an additional ResetType argument which is passed to foreach_child.
 */
typedef void (*ResettableInitPhase)(Object *obj, ResetType type);
typedef void (*ResettableHoldPhase)(Object *obj);
typedef void (*ResettableExitPhase)(Object *obj);
typedef ResetState * (*ResettableGetState)(Object *obj);
typedef void (*ResettableForeachChild)(Object *obj,
                                       void (*func)(Object *, ResetType type),
                                       ResetType type);
typedef struct ResettableClass {
    InterfaceClass parent_class;

    struct ResettablePhases {
        ResettableInitPhase init;
        ResettableHoldPhase hold;
        ResettableExitPhase exit;
    } phases;

    ResettableGetState get_state;
    ResettableForeachChild foreach_child;
} ResettableClass;
typedef struct ResettablePhases ResettablePhases;

/**
 * ResetState:
 * Structure holding reset related state. The fields should not be accessed
 * directly, the definition is here to allow further inclusion into other
 * objects.
 *
 * @count: Number of reset level the object is into. It is incremented when
 * the reset operation starts and decremented when it finishes.
 * @hold_phase_needed: flag which indicates that we need to invoke the 'hold'
 * phase handler for this object.
 */
struct ResetState {
    uint32_t count;
    bool hold_phase_needed;
};

/**
 * resettable_is_resetting:
 * Return true if @obj is under reset.
 *
 * @obj must implement Resettable interface.
 */
bool resettable_is_resetting(Object *obj);

/**
 * resettable_reset:
 * Trigger a reset on a object @obj of type @type. @obj must implement
 * Resettable interface.
 *
 * Calling this function is equivalent to calling @resettable_assert_reset then
 * @resettable_deassert_reset.
 */
void resettable_reset(Object *obj, ResetType type);

/**
 * resettable_cold_reset_fn:
 * Helper to call resettable_reset((Object *) opaque, RESET_TYPE_COLD).
 *
 * This function is typically useful to register a reset handler with
 * qemu_register_reset.
 */
void resettable_cold_reset_fn(void *opaque);

/**
 * resettable_class_set_parent_phases:
 *
 * Save @rc current reset phases into @parent_phases and override @rc phases
 * by the given new methods (@init, @hold and @exit).
 * Each phase is overridden only if the new one is not NULL allowing to
 * override a subset of phases.
 */
void resettable_class_set_parent_phases(ResettableClass *rc,
                                        ResettableInitPhase init,
                                        ResettableHoldPhase hold,
                                        ResettableExitPhase exit,
                                        ResettablePhases *parent_phases);

#endif
