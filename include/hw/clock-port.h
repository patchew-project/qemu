#ifndef CLOCK_PORT_H
#define CLOCK_PORT_H

#include "qom/object.h"
#include "hw/qdev-core.h"
#include "qemu/queue.h"

#define TYPE_CLOCK_PORT "clock-port"
#define CLOCK_PORT(obj) OBJECT_CHECK(ClockPort, (obj), TYPE_CLOCK_PORT)
#define TYPE_CLOCK_IN "clock-in"
#define CLOCK_IN(obj) OBJECT_CHECK(ClockIn, (obj), TYPE_CLOCK_IN)
#define TYPE_CLOCK_OUT "clock-out"
#define CLOCK_OUT(obj) OBJECT_CHECK(ClockOut, (obj), TYPE_CLOCK_OUT)

typedef struct ClockState {
    uint64_t frequency; /* frequency of the clock in Hz */
    bool domain_reset; /* flag indicating if clock domain is under reset */
} ClockState;

typedef void ClockCallback(void *opaque, ClockState *clk);

typedef struct ClockPort {
    /*< private >*/
    Object parent_obj;
    /*< public >*/
    char *canonical_path; /* clock path shadow */
} ClockPort;

typedef struct ClockOut ClockOut;
typedef struct ClockIn ClockIn;

struct ClockIn {
    /*< private >*/
    ClockPort parent_obj;
    /*< private >*/
    ClockOut *driver; /* clock output controlling this clock */
    ClockCallback *callback; /* local callback */
    void *callback_opaque; /* opaque argument for the callback */
    QLIST_ENTRY(ClockIn) sibling;  /* entry in a followers list */
};

struct ClockOut {
    /*< private >*/
    ClockPort parent_obj;
    /*< private >*/
    QLIST_HEAD(, ClockIn) followers; /* list of registered clocks */
};

/*
 * clock_setup_canonical_path:
 * @clk: clock
 *
 * compute the canonical path of the clock (used by log messages)
 */
void clock_setup_canonical_path(ClockPort *clk);

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
 * clock_connect:
 * @clkin: the drived clock.
 * @clkout: the driving clock.
 *
 * Setup @clkout to drive @clkin: Any @clkout update will be propagated
 * to @clkin.
 */
void clock_connect(ClockIn *clkin, ClockOut *clkout);

/**
 * clock_set:
 * @clk: the clock to update.
 * @state: the new clock's state.
 *
 * Update the @clk to the new @state.
 * This change will be propagated through registered clock inputs.
 */
void clock_set(ClockOut *clk, ClockState *state);

/**
 * clock_state_get_frequency:
 * @clk: a clock state
 *
 * @return: the current frequency of @clk in Hz. If @clk is NULL, return 0.
 */
static inline uint64_t clock_state_get_frequency(const ClockState *clk)
{
    return clk ? clk->frequency : 0;
}

/**
 * clock_state_is_enabled:
 * @clk: a clock state
 *
 * @return: true if the clock is running. If @clk is NULL return false.
 */
static inline bool clock_state_is_enabled(const ClockState *clk)
{
    return clock_state_get_frequency(clk) != 0;
}

/**
 * clock_state_get_domain_reset:
 * @clk: a clock state
 *
 * @return: true if the domain reset is asserted. If @clk is NULL return false.
 */
static inline bool clock_state_get_domain_reset(const ClockState *clk)
{
    return clk ? clk->domain_reset : false;
}

/**
 * clock_state_is_domain_running:
 * @clk: a clock state
 *
 * @return: true if the clock is running and reset not asserted. If @clk is
 * NULL return false.
 */
static inline bool clock_state_is_domain_running(const ClockState *clk)
{
    return clock_state_is_enabled(clk) && !clock_state_get_domain_reset(clk);
}

/**
 * clock_state_copy:
 * @dst the clock state destination
 * @src: the clock state source
 *
 * Transfer the source clock state into the destination.
 */
static inline void clock_state_copy(ClockState *dst, const ClockState *src)
{
    dst->frequency = clock_state_get_frequency(src);
    dst->domain_reset = clock_state_get_domain_reset(src);
}

#endif /* CLOCK_PORT_H */
