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
 * "Entering the reset state" corresponds to the init and hold phases.
 * "Leaving the reset state" corresponds to the exit phase.
 */
typedef void (*ResettableInitPhase)(Object *obj, bool cold);
typedef void (*ResettableHoldPhase)(Object *obj);
typedef void (*ResettableExitPhase)(Object *obj);
typedef struct ResettableClass {
    InterfaceClass parent_class;

    struct ResettablePhases {
        ResettableInitPhase init;
        ResettableHoldPhase hold;
        ResettableExitPhase exit;
    } phases;
} ResettableClass;
typedef struct ResettablePhases ResettablePhases;

/*
 * Helpers to do a single phase of a Resettable.
 * Call the corresponding ResettableClass method if it is not NULL.
 */
void resettable_init_phase(Object *obj, bool cold);
void resettable_hold_phase(Object *obj);
void resettable_exit_phase(Object *obj);

/**
 * resettable_assert_reset:
 * Put an object in reset state.
 * Each time resettable_assert_reset is called, resettable_deassert_reset
 * must be eventually called once and only once.
 *
 * @obj object to reset, must implement Resettable interface.
 * @cold boolean indicating the type of reset (cold or warm)
 */
void resettable_assert_reset(Object *obj, bool cold);

/**
 * resettable_deassert_reset:
 * End the reset state if an object.
 *
 * @obj object to reset, must implement Resettable interface.
 */
void resettable_deassert_reset(Object *obj);

/**
 * resettable_reset:
 * Calling this function is equivalent to call @assert_reset then
 * @deassert_reset.
 */
static inline void resettable_reset(Object *obj, bool cold)
{
    resettable_assert_reset(obj, cold);
    resettable_deassert_reset(obj);
}

#endif
