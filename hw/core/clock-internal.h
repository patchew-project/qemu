/*
 * Hardware Clocks
 *
 * Copyright GreenSocs 2016-2020
 *
 * Authors:
 *  Frederic Konrad
 *  Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_HW_CLOCK_INTERNAL_H
#define QEMU_HW_CLOCK_INTERNAL_H

#include "hw/clock.h"

/**
 * clock_new:
 * @parent: the clock parent
 * @name: the clock object name
 *
 * Helper function to create a new clock and parent it to @parent. There is no
 * need to call clock_setup_canonical_path on the returned clock as it is done
 * by this function.
 *
 * @return the newly created clock
 */
Clock *clock_new(Object *parent, const char *name);

#endif
