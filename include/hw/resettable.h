#ifndef HW_RESETTABLE_H
#define HW_RESETTABLE_H

#include "qom/object.h"

#define TYPE_RESETTABLE "resettable"

#define RESETTABLE_CLASS(class) \
    OBJECT_CLASS_CHECK(ResettableClass, (class), TYPE_RESETTABLE)

/*
 * ResettableClass:
 * Interface for resettable objects.
 *
 * The reset operation is divided in several phases each represented by a
 * method.
 *
 * Each Ressetable must maintain a reset counter in its state, 3 methods allows
 * to interact with it.
 *
 * @phases.init: should reset local state only. Takes a bool @cold argument
 * specifying whether the reset is cold or warm. It must not do side-effect
 * on others objects.
 *
 * @phases.hold: side-effects action on others objects due to staying in a
 * resetting state.
 *
 * @phases.exit: leave the reset state, may do side-effects action on others
 * objects.
 *
 * @set_cold: Set whether the current reset is cold or warm. Return the
 * previous flag value. Return value has no meaning if @get_count returns
 * a zero value.
 *
 * @set_hold_needed: Set hold_needed flag. Return the previous flag value.
 *
 * @get_count: Get the current reset count
 * @increment_count: Increment the reset count, returns the new count
 * @decrement_count: decrement the reset count, returns the new count
 *
 * @foreach_child: Executes a given function on every Resettable child.
 * A child is not a QOM child, but a child a reset meaning.
 */
typedef void (*ResettableInitPhase)(Object *obj);
typedef void (*ResettableHoldPhase)(Object *obj);
typedef void (*ResettableExitPhase)(Object *obj);
typedef bool (*ResettableSetCold)(Object *obj, bool cold);
typedef bool (*ResettableSetHoldNeeded)(Object *obj, bool hold_needed);
typedef uint32_t (*ResettableGetCount)(Object *obj);
typedef uint32_t (*ResettableIncrementCount)(Object *obj);
typedef uint32_t (*ResettableDecrementCount)(Object *obj);
typedef void (*ResettableForeachChild)(Object *obj, void (*visitor)(Object *));
typedef struct ResettableClass {
    InterfaceClass parent_class;

    struct ResettablePhases {
        ResettableInitPhase init;
        ResettableHoldPhase hold;
        ResettableExitPhase exit;
    } phases;

    ResettableSetCold set_cold;
    ResettableSetHoldNeeded set_hold_needed;
    ResettableGetCount get_count;
    ResettableIncrementCount increment_count;
    ResettableDecrementCount decrement_count;
    ResettableForeachChild foreach_child;
} ResettableClass;
typedef struct ResettablePhases ResettablePhases;

/**
 * resettable_assert_reset:
 * Increments the reset count and executes the init and hold phases.
 * Each time resettable_assert_reset is called, resettable_deassert_reset
 * must eventually be called once.
 * It will also impact reset children.
 *
 * @obj object to reset, must implement Resettable interface.
 * @cold boolean indicating the type of reset (cold or warm)
 */
void resettable_assert_reset(Object *obj, bool cold);

/**
 * resettable_deassert_reset:
 * Decrements the reset count by one and executes the exit phase if it hits
 * zero.
 * It will also impact reset children.
 *
 * @obj object to reset, must implement Resettable interface.
 */
void resettable_deassert_reset(Object *obj);

/**
 * resettable_reset:
 * Calling this function is equivalent to call @assert_reset then
 * @deassert_reset.
 */
void resettable_reset(Object *obj, bool cold);

/**
 * resettable_reset_warm_fn:
 * Helper to call resettable_reset(opaque, false)
 */
void resettable_reset_warm_fn(void *opaque);

/**
 * resettable_reset_cold_fn:
 * Helper to call resettable_reset(opaque, true)
 */
void resettable_reset_cold_fn(void *opaque);

/**
 * resettable_class_set_parent_reset_phases:
 *
 * Save @rc current reset phases into @parent_phases and override @rc phases
 * by the given new methods (@init, @hold and @exit).
 * Each phase is overriden only if the new one is not NULL allowing to
 * override a subset of phases.
 */
void resettable_class_set_parent_reset_phases(ResettableClass *rc,
                                              ResettableInitPhase init,
                                              ResettableHoldPhase hold,
                                              ResettableExitPhase exit,
                                              ResettablePhases *parent_phases);

#endif
