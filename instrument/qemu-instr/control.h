/*
 * Main instrumentation interface for QEMU.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QI__CONTROL_H
#define QI__CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>


/**
 * SECTION:control
 * @section_id: qi-control
 * @title: Event control API for QEMU event instrumentation
 */

typedef void (*qi_fini_fn)(void *arg);

/**
 * qi_set_fini:
 * @fn: Finalization function.
 * @data: Argument to pass to the finalization function.
 *
 * Set the function to call when finalizing (unloading) the instrumentation
 * library.
 *
 * NOTE: Calls to printf() might not be shown if the library is unloaded when
 *       QEMU terminates.
 */
void qi_set_fini(qi_fini_fn fn, void *data);

#ifdef __cplusplus
}
#endif

#endif  /* QI__CONTROL_H */
