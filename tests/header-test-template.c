/*
 * Template for make check-headers
 *
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

/*
 * Some headers define static functions that real includers all use,
 * but this one of course doesn't.  Suppress the resulting warning:
 */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "@header@"
/* Include a second time to catch missing header guard */
#include "@header@"
