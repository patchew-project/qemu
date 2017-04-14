/*
 * Helpers for GLIB
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_GLIB_HELPER_H
#define QEMU_GLIB_HELPER_H


#include "glib/glib-compat.h"

#define GPOINTER_TO_UINT64(a) ((guint64) (a))

/*
 * return 1 in case of a > b, -1 otherwise and 0 if equeal
 */
gint g_int_cmp64(gconstpointer a, gconstpointer b,
        gpointer __attribute__((unused)) user_data);

/*
 * return 1 in case of a > b, -1 otherwise and 0 if equeal
 */
int g_int_cmp(gconstpointer a, gconstpointer b,
        gpointer __attribute__((unused)) user_data);

#endif /* QEMU_GLIB_HELPER_H */

