/*
 * QEMU Builtin Random Number Generator Backend
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_RNG_BUILTIN_H
#define QEMU_RNG_BUILTIN_H

#include "qom/object.h"

#define TYPE_RNG_BUILTIN "rng-builtin"
#define RNG_BUILTIN(obj) OBJECT_CHECK(RngBuiltin, (obj), TYPE_RNG_BUILTIN)

typedef struct RngBuiltin RngBuiltin;

#endif
