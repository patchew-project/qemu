#ifndef CLOCK_PORT_H
#define CLOCK_PORT_H

#include "qom/object.h"
#include "hw/qdev-core.h"
#include "qemu/queue.h"
#include "migration/vmstate.h"

#define TYPE_CLOCK_IN "clock-in"
#define CLOCK_IN(obj) OBJECT_CHECK(ClockIn, (obj), TYPE_CLOCK_IN)
#define TYPE_CLOCK_OUT "clock-out"
#define CLOCK_OUT(obj) OBJECT_CHECK(ClockOut, (obj), TYPE_CLOCK_OUT)

typedef void ClockCallback(void *opaque);

typedef struct ClockOut ClockOut;
typedef struct ClockIn ClockIn;

struct ClockIn {
    /*< private >*/
    Object parent_obj;
    /*< private >*/
    uint64_t frequency;
    char *canonical_path; /* clock path cache */
    ClockOut *driver; /* clock output controlling this clock */
    ClockCallback *callback; /* local callback */
    void *callback_opaque; /* opaque argument for the callback */
    QLIST_ENTRY(ClockIn) sibling;  /* entry in a followers list */
};

struct ClockOut {
    /*< private >*/
    Object parent_obj;
    /*< private >*/
    char *canonical_path; /* clock path cache */
    QLIST_HEAD(, ClockIn) followers; /* list of registered clocks */
};

extern const VMStateDescription vmstate_clockin;

/*
 * vmstate description entry to be added in device vmsd.
 */
#define VMSTATE_CLOCKIN(_field, _state) \
    VMSTATE_CLOCKIN_V(_field, _state, 0)
#define VMSTATE_CLOCKIN_V(_field, _state, _version) \
    VMSTATE_STRUCT_POINTER_V(_field, _state, _version, vmstate_clockin, ClockIn)

/**
 * clock_out_setup_canonical_path:
 * @clk: clock
 *
 * compute the canonical path of the clock (used by log messages)
 */
void clock_out_setup_canonical_path(ClockOut *clk);

/**
 * clock_in_setup_canonical_path:
 * @clk: clock
 *
 * compute the canonical path of the clock (used by log messages)
 */
void clock_in_setup_canonical_path(ClockIn *clk);

/**
 * clock_add_callback:
 * @clk: the clock to register the callback into
 * @cb: the callback function
 * @opaque: the argument to the callback
 *
 * Register a callback called on every clock update.
 */
void clock_set_callback(ClockIn *clk, ClockCallback *cb, void *opaque);

/**
 * clock_clear_callback:
 * @clk: the clock to delete the callback from
 *
 * Unregister the callback registered with clock_set_callback.
 */
void clock_clear_callback(ClockIn *clk);

/**
 * clock_init_frequency:
 * @clk: the clock to initialize.
 * @freq: the clock's frequency in Hz or 0 if unclocked.
 *
 * Initialize the local cached frequency value of @clk to @freq.
 * Note: this function must only be called during device inititialization
 * or migration.
 */
void clock_init_frequency(ClockIn *clk, uint64_t freq);

/**
 * clock_connect:
 * @clkin: the drived clock.
 * @clkout: the driving clock.
 *
 * Setup @clkout to drive @clkin: Any @clkout update will be propagated
 * to @clkin.
 */
void clock_connect(ClockIn *clkin, ClockOut *clkout);

/**
 * clock_set_frequency:
 * @clk: the clock to update.
 * @freq: the new clock's frequency in Hz or 0 if unclocked.
 *
 * Update the @clk to the new @freq.
 * This change will be propagated through registered clock inputs.
 */
void clock_set_frequency(ClockOut *clk, uint64_t freq);

/**
 * clock_get_frequency:
 * @clk: the clk to fetch the clock
 *
 * @return: the current frequency of @clk in Hz. If @clk is NULL, return 0.
 */
static inline uint64_t clock_get_frequency(const ClockIn *clk)
{
    return clk ? clk->frequency : 0;
}

/**
 * clock_is_enabled:
 * @clk: a clock state
 *
 * @return: true if the clock is running. If @clk is NULL return false.
 */
static inline bool clock_is_enabled(const ClockIn *clk)
{
    return clock_get_frequency(clk) != 0;
}

#endif /* CLOCK_PORT_H */
