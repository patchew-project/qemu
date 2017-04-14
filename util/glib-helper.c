/*
 * Implementation for GLIB helpers
 * this file is intented to commulate and later reuse
 * additional glib functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.

 */

#include "glib/glib-helper.h"

gint g_int_cmp64(gconstpointer a, gconstpointer b,
        gpointer __attribute__((unused)) user_data)
{
    guint64 ua = GPOINTER_TO_UINT64(a);
    guint64 ub = GPOINTER_TO_UINT64(b);
    return (ua > ub) - (ua < ub);
}

/*
 * return 1 in case of a > b, -1 otherwise and 0 if equeal
 */
gint g_int_cmp(gconstpointer a, gconstpointer b,
        gpointer __attribute__((unused)) user_data)
{
    return g_int_cmp64(a, b, user_data);
}

